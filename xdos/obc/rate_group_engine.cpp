// SPDX-License-Identifier: Apache-2.0
#include "rate_group_engine.hpp"

#include "framing/eps_framing.hpp"
#include "framing/gps_framing.hpp"

#include <algorithm>
#include <chrono>
#include <set>

namespace xdos::obc {

using namespace std::chrono;
using namespace xdos::lut;

RateGroupEngine::RateGroupEngine(ccsds::CcsdsTmCodec& tm_codec, Config cfg,
                                 PushSink sink)
    : tm_(tm_codec), cfg_(cfg), sink_(std::move(sink)) {
    // Default subsystem endpoints (the standard sim ports).
    endpoints_[Subsystem::EPS]   = { "127.0.0.1", 10010 };
    endpoints_[Subsystem::ADCS]  = { "127.0.0.1", 10020 };
    endpoints_[Subsystem::GPS]   = { "127.0.0.1", 10030 };
    endpoints_[Subsystem::Payload] = { "127.0.0.1", 10040 };
    endpoints_[Subsystem::XLINK] = { "127.0.0.1", 10050 };
    endpoints_[Subsystem::UHF]   = { "127.0.0.1", 10060 };
    for (int t = 0; t < 4; ++t) block_[t] = Bytes(kBlockBudget[t], 0x00);
}

RateGroupEngine::~RateGroupEngine() { stop(); }

void RateGroupEngine::set_endpoint(Subsystem s, const std::string& host, int port) {
    std::lock_guard<std::mutex> g(lut_mu_);
    endpoints_[s] = { host, port };
}

// ── 16-position tier schedule ───────────────────────────────────────────────
// Tier t entry i is polled once per block span, in the slots where
// slot % span == i % span.  VH lands in every slot; H entries alternate
// between the two subframes of their block; M and L spread the same way.
std::array<std::vector<RateGroupEngine::PollTask>, kSubframesPerMaster>
RateGroupEngine::build_schedule(const BinaryRateLut& lut) {
    std::array<std::vector<PollTask>, kSubframesPerMaster> sched;
    for (int k = 0; k < kSubframesPerMaster; ++k)
        for (int t = 0; t < 4; ++t)
            for (int i = 0; i < (int)lut.tiers[t].size(); ++i)
                if (k % kBlockSpan[t] == i % kBlockSpan[t])
                    sched[k].push_back({t, i});
    return sched;
}

bool RateGroupEngine::swap_lut(const BinaryRateLut& lut, std::string* err) {
    if (lut.empty()) { if (err) *err = "LUT has no entries"; return false; }
    for (int t = 0; t < 4; ++t)
        for (const auto& e : lut.tiers[t])
            for (const auto& p : e.picks)
                if (p.block + p.size > kBlockBudget[t]) {
                    if (err) *err = std::string("pick outside ")
                                    + kTierName[t] + " block budget";
                    return false;
                }
    std::lock_guard<std::mutex> lg(lut_mu_);
    std::lock_guard<std::mutex> bg(block_mu_);
    lut_ = lut;
    schedule_ = build_schedule(lut_);
    // (Re)initialise the blocks: unassigned budget bytes are zero-padded,
    // every entry's pick ranges start as 0xFF ("down") so the very first
    // push is well-formed before any poll completes.
    for (int t = 0; t < 4; ++t) {
        block_[t].assign(kBlockBudget[t], 0x00);
        for (const auto& e : lut_.tiers[t])
            for (const auto& p : e.picks)
                std::fill(block_[t].begin() + p.block,
                          block_[t].begin() + p.block + p.size, 0xFF);
    }
    have_lut_.store(true);
    return true;
}

std::uint16_t RateGroupEngine::lut_signature() const {
    std::lock_guard<std::mutex> g(lut_mu_); return lut_.crc;
}
int RateGroupEngine::command_count() const {
    std::lock_guard<std::mutex> g(lut_mu_); return lut_.entry_count();
}
int RateGroupEngine::payload_bytes() const {
    std::lock_guard<std::mutex> g(lut_mu_); return lut_.pick_bytes();
}
int RateGroupEngine::t_sf_ms() const {
    std::lock_guard<std::mutex> g(lut_mu_); return lut_.t_sf_ms;
}
int RateGroupEngine::max_polls_per_window() const {
    std::lock_guard<std::mutex> g(lut_mu_);
    std::size_t m = 0;
    for (const auto& s : schedule_) m = std::max(m, s.size());
    return (int)m;
}

void RateGroupEngine::start() {
    if (running_.exchange(true)) return;
    poll_th_ = std::thread([this] { poll_loop(); });
    push_th_ = std::thread([this] { push_loop(); });
}

void RateGroupEngine::stop() {
    if (!running_.exchange(false)) return;
    if (poll_th_.joinable()) poll_th_.join();
    if (push_th_.joinable()) push_th_.join();
    conns_.clear();
}

// ── protocol-only reply de-framing (NO Database) ────────────────────────────
//   EPS   : Base64 line frame 0x00 <b64(body)> 0x01 — decode to src|msg|data|crc,
//           the payload is `data`.
//   GPS   : NovAtel OEM7 binary — payload per the header length fields.
//   XLINK : 20-byte header (key + service event) then payload; drop 20 bytes.
Bytes RateGroupEngine::strip_reply(Subsystem s, const Bytes& reply) {
    if (reply.empty()) return {};
    if (s == Subsystem::EPS) {
        auto d = framing::eps::decode(reply);
        return d.data;
    }
    if (s == Subsystem::GPS) {
        auto d = framing::gps::decode(reply);
        return d.data;
    }
    if (s == Subsystem::XLINK) {
        if (reply.size() > 20) return Bytes(reply.begin() + 20, reply.end());
        return {};
    }
    if (s == Subsystem::UHF) {
        // ESP frame: 22 69 | len | 6-byte header | data — keep the data.
        if (reply.size() > 9 && reply[0] == 0x22 && reply[1] == 0x69)
            return Bytes(reply.begin() + 9,
                         reply.begin() + std::min<std::size_t>(
                             reply.size(), 3 + reply[2]));
        return {};
    }
    return reply;
}

// ── poll one entry: replay its TC bytes, receive, strip to payload ──────────
Bytes RateGroupEngine::poll_one(Subsystem sub, const Bytes& tc_bytes) {
    Endpoint ep;
    { std::lock_guard<std::mutex> g(lut_mu_);
      auto it = endpoints_.find(sub);
      if (it == endpoints_.end()) return {};
      ep = it->second; }

    auto& conn = conns_[sub];
    if (!conn) conn = std::make_unique<transport::TcpClient>();
    if (!conn->connected() && !conn->connect(ep.host, ep.port, 300)) {
        conn.reset();
        return {};
    }

    if (!conn->send(tc_bytes)) { conn.reset(); return {}; }

    // Read the reply (subsystem replies are small; one recv is enough, but
    // give a short grace window for any trailing segment).
    Bytes reply = conn->recv(4096, 300);
    if (reply.empty()) return {};
    for (int i = 0; i < 2; ++i) {
        auto more = conn->recv(4096, 20);
        if (more.empty()) break;
        reply.insert(reply.end(), more.begin(), more.end());
    }
    return strip_reply(sub, reply);
}

// ── route a poll result into the tier's block via the entry's picks ─────────
void RateGroupEngine::apply_result(int tier, int entry, const Bytes& payload) {
    std::lock_guard<std::mutex> lg(lut_mu_);
    std::lock_guard<std::mutex> bg(block_mu_);
    if (entry >= (int)lut_.tiers[tier].size()) return;   // raced a hot-swap
    const auto& e = lut_.tiers[tier][entry];
    for (const auto& p : e.picks) {
        auto dst = block_[tier].begin() + p.block;
        if (payload.empty()) {                           // down marker
            std::fill(dst, dst + p.size, 0xFF);
            continue;
        }
        const int avail = (int)payload.size() > p.src
                              ? std::min<int>(p.size, (int)payload.size() - p.src)
                              : 0;
        if (avail > 0)
            std::copy(payload.begin() + p.src,
                      payload.begin() + p.src + avail, dst);
        if (avail < p.size)                              // short reply: zero-fill
            std::fill(dst + avail, dst + p.size, 0x00);
    }
}

// ── tier-scheduled poll loop: fill slot k's data during window k ────────────
void RateGroupEngine::poll_loop() {
    const double scale = cfg_.time_scale > 0 ? cfg_.time_scale : 1.0;
    while (running_.load()) {
        if (!have_lut_.load()) {
            std::this_thread::sleep_for(milliseconds(50));
            continue;
        }
        const auto window_start = steady_clock::now();
        // Slot of the next push — the subframe this window's polls feed.
        const int k = tm_.current_sub(tm_.rate_group_apid())
                      % kSubframesPerMaster;
        std::vector<PollTask> tasks;
        long long t_win_ms = kTsfMs;
        { std::lock_guard<std::mutex> g(lut_mu_);
          tasks = schedule_[k];
          t_win_ms = (long long)(lut_.t_sf_ms / scale); }
        const auto window_end = window_start + milliseconds(t_win_ms);
        // Gap = T_sf / number of polls in this window.  Pacing is derived
        // from the table, never charged at a fixed cost per command, so the
        // window always fits however dense the LUT is (a fixed 50 ms per poll
        // is what made 33 polls demand 1650 ms of a 1000 ms window).
        // Simulation model only: on flight hardware the OBC's bus driver owns
        // the real inter-poll delay.
        const std::size_t n_polls = std::max<std::size_t>(1, tasks.size());
        const auto gap = microseconds(t_win_ms * 1000 / (long long)n_polls);

        std::set<Subsystem> dead_this_window;
        for (std::size_t i = 0; i < tasks.size() && running_.load(); ++i) {
            Subsystem sub; Bytes tc;
            { std::lock_guard<std::mutex> g(lut_mu_);
              if (tasks[i].entry >= (int)lut_.tiers[tasks[i].tier].size())
                  continue;                              // raced a hot-swap
              const auto& e = lut_.tiers[tasks[i].tier][tasks[i].entry];
              sub = e.sub; tc = e.tc_bytes; }

            Bytes payload;
            const bool skip = dead_this_window.count(sub) > 0;
            if (!skip) {
                payload = poll_one(sub, tc);
                // Connect/send failure resets the connection — mark the
                // subsystem dead for the rest of the window so its 300 ms
                // timeouts can't blow every remaining poll.
                if (payload.empty() && !conns_[sub])
                    dead_this_window.insert(sub);
            }
            apply_result(tasks[i].tier, tasks[i].entry, payload);
            { std::lock_guard<std::mutex> g(stats_mu_);
              ++stats_.polls;
              if (!payload.empty()) ++stats_.ok_by_sub[(int)sub];
              else                  ++stats_.fail_by_sub[(int)sub]; }

            if (!skip && i + 1 < tasks.size())
                std::this_thread::sleep_until(window_start + gap * (long)(i + 1));
        }
        if (steady_clock::now() > window_end) {
            std::lock_guard<std::mutex> g(stats_mu_);
            ++stats_.overruns;                           // run late, never skip
        } else {
            std::this_thread::sleep_until(window_end);
        }
    }
}

// ── subframe assembly: fixed tier partition, block slice by slot ────────────
Bytes RateGroupEngine::pack_subframe(const std::array<Bytes,4>& blocks, int k) {
    Bytes d(kSubframeSize, 0x00);
    for (int t = 0; t < 4; ++t) {
        const int slice = (k % kBlockSpan[t]) * kSegBytes[t];
        std::copy(blocks[t].begin() + slice,
                  blocks[t].begin() + slice + kSegBytes[t],
                  d.begin() + kSegOffset[t]);
    }
    return d;                                            // bytes 1275..1279 stay 0
}

void RateGroupEngine::push_loop() {
    const double scale = cfg_.time_scale > 0 ? cfg_.time_scale : 1.0;
    auto next = steady_clock::now();
    while (running_.load()) {
        if (!have_lut_.load()) {
            // NO LUT -> NO frames.  The OBC is silent until the first uplink.
            std::this_thread::sleep_for(milliseconds(50));
            next = steady_clock::now();
            continue;
        }
        long long period_ms;
        { std::lock_guard<std::mutex> g(lut_mu_);
          period_ms = (long long)(lut_.t_sf_ms / scale); }
        next += milliseconds(period_ms);
        std::this_thread::sleep_until(next);
        if (!running_.load() || !have_lut_.load()) continue;

        const int k = tm_.current_sub(tm_.rate_group_apid())
                      % kSubframesPerMaster;
        Bytes tm_data;
        { std::lock_guard<std::mutex> g(block_mu_);
          tm_data = pack_subframe(block_, k); }
        Bytes frame = tm_.encode_apid(tm_.rate_group_apid(), tm_data,
                                      Bytes{}, cfg_.rm_id);
        if (sink_) sink_(frame);
        std::lock_guard<std::mutex> g(stats_mu_);
        ++stats_.pushes;
    }
}

RateGroupEngine::Stats RateGroupEngine::stats() const {
    std::lock_guard<std::mutex> g(stats_mu_);
    return stats_;
}

}  // namespace xdos::obc
