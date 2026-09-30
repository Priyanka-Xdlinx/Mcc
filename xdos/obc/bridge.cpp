// SPDX-License-Identifier: Apache-2.0
#include "bridge.hpp"
#include "common/hex.hpp"
#include "common/egse.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace xdos::obc {

using namespace std::chrono;

Bridge::Bridge(EndpointMap subsystems) : endpoints_(std::move(subsystems)) {}
Bridge::~Bridge() { stop(); }

std::optional<Subsystem> Bridge::sniff_subsystem(const Bytes& f) {
    if (f.empty()) return std::nullopt;
    // EPS: starts with 0x00, ends with 0x01 (base64-wrapped)
    if (f.front() == 0x00 && f.back() == 0x01) return Subsystem::EPS;
    // ADCS: starts with 0x1F 0x7F (CubeSpace)
    if (f.size() >= 2 && f[0] == 0x1F && f[1] == 0x7F) return Subsystem::ADCS;
    // GPS: NovAtel OEM7 sync 0xAA 0x44 0x12
    if (f.size() >= 3 && f[0] == 0xAA && f[1] == 0x44 && f[2] == 0x12)
        return Subsystem::GPS;
    // UHF: OpenLST ESP frame sync 0x22 0x69.  Checked BEFORE the loose
    // XLINK nibble heuristic so a ≥20-byte UHF frame can't be misrouted.
    if (f.size() >= 2 && f[0] == 0x22 && f[1] == 0x69)
        return Subsystem::UHF;
    // Sensor controller: SOF A5 AA, and the length byte must agree with the
    // trailing EOF.  Checked BEFORE the loose XLINK nibble heuristic.
    if (f.size() >= 6 && f[0] == 0xA5 && f[1] == 0xAA) {
        std::size_t total = static_cast<std::size_t>(f[4]) + 6;
        if (total == f.size() && f.back() == 0xBA) return Subsystem::SENSOR;
    }
    // XLINK-S: XLink Service Server frame — ≥ 20 bytes, service_event field in
    // bytes 16..19 has a recognisable GET/SET/ACK/NAK/BUSY nibble in byte 19.
    // The 16-byte product_key ahead of it is arbitrary but the flag nibble is
    // structural: valid values are 0x10 0x20 0x40 0x80 0xC0.
    if (f.size() >= 20) {
        std::uint8_t nib = f[19] & 0xF0;
        if (nib == 0x10 || nib == 0x20 || nib == 0x40 ||
            nib == 0x80 || nib == 0xC0)
            return Subsystem::XLINK;
    }
    return std::nullopt;
}

BridgeStats Bridge::stats() const {
    std::lock_guard<std::mutex> g(m_);
    return stats_;
}

std::vector<Bridge::LogEntry> Bridge::recent_log(size_t n) const {
    std::lock_guard<std::mutex> g(m_);
    if (log_.size() <= n) return log_;
    return std::vector<LogEntry>(log_.end() - n, log_.end());
}

void Bridge::log_frame(const std::string& dir, const Bytes& f) {
    LogEntry e;
    auto now = system_clock::now();
    std::time_t t = system_clock::to_time_t(now);
    std::ostringstream ts; ts << std::put_time(std::localtime(&t), "%H:%M:%S");
    e.when = ts.str();
    e.direction = dir;
    e.size = f.size();
    e.preview = hex::encode_spaced(
        f.size() > 24 ? Bytes(f.begin(), f.begin() + 24) : f);
    std::lock_guard<std::mutex> g(m_);
    log_.push_back(std::move(e));
    if (log_.size() > kLogCap) log_.erase(log_.begin(),
                                          log_.begin() + (log_.size() - kLogCap));
}

bool Bridge::ensure_connected(Subsystem s) {
    auto it = downlinks_.find(s);
    if (it != downlinks_.end() && it->second && it->second->connected())
        return true;
    auto ep = endpoints_.find(s);
    if (ep == endpoints_.end()) return false;
    auto cli = std::make_unique<transport::TcpClient>();
    if (!cli->connect(ep->second.ip, ep->second.port, /*timeout_ms*/ 500))
        return false;
    downlinks_[s] = std::move(cli);
    return true;
}

void Bridge::handle_gs_client(int fd, const std::string& /*peer*/) {
    while (running_.load()) {
        auto first = transport::fd_recv_blocking(fd, 8192);
        if (first.empty()) break;   // peer closed or real socket error
        // collect a tail in case frame arrived in pieces
        auto until = steady_clock::now() + milliseconds(20);
        while (steady_clock::now() < until) {
            auto more = transport::fd_recv(fd, 8192, 5);
            if (more.empty()) break;
            first.insert(first.end(), more.begin(), more.end());
        }

        {
            std::lock_guard<std::mutex> g(m_);
            ++stats_.frames_in;
        }

        // ── EGSE umbilical: always answered, even unpowered ────────────────
        if (egse_handler_ && egse::is_egse(first)) {
            auto rep = egse_handler_(first);
            if (!rep.empty()) transport::fd_send(fd, rep);
            continue;
        }

        // ── Flight power gate: unpowered/booting OBC bridges nothing ──────
        if (!enabled_.load()) {
            log_frame("GS→(OBC unpowered — dropped)", first);
            continue;
        }

        // Notify observer (used by --validate to dump frame validation).
        if (observer_) observer_(first);

        // ── CCSDS detection & unwrap ────────────────────────────────────────
        // If the GS sent a CCSDS-wrapped frame, unwrap to get the native
        // subsystem frame.  The native bytes were placed in tc_data by
        // Session::transact when CCSDS was enabled.  APID gives us the
        // subsystem directly (no prefix sniffing needed).
        bool                              ccsds_in = false;
        std::uint16_t                     ccsds_tc_id = 0;
        std::optional<Subsystem>          ccsds_apid_sub;
        Bytes                             native_for_sim = first;
        if (ccsds_ && ccsds_->looks_like_ccsds(first)) {
            auto d = ccsds_->decode(first);
            if (!d || !d->crc_ok) {
                std::lock_guard<std::mutex> g(m_);
                ++stats_.ccsds_bad;
                log_frame("GS→??? [CCSDS bad]", first);
                continue;
            }
            ccsds_in       = true;
            ccsds_tc_id    = d->tc_id;
            ccsds_apid_sub = d->subsystem;
            native_for_sim = d->tc_data;        // OBC forwards the native frame
            std::lock_guard<std::mutex> g(m_);
            ++stats_.ccsds_in;
        }

        // ── Subsystem resolution: prefer APID, fall back to prefix sniff ───
        std::optional<Subsystem> sub =
            ccsds_apid_sub ? ccsds_apid_sub : sniff_subsystem(native_for_sim);
        if (!sub) {
            std::lock_guard<std::mutex> g(m_);
            ++stats_.unrecognised;
            log_frame("GS→???", first);
            continue;
        }
        log_frame(std::string(ccsds_in ? "GS→" : "GS→")
                  + std::string(to_string(*sub))
                  + (ccsds_in ? " [CCSDS]" : ""), first);

        if (!ensure_connected(*sub)) {
            auto ep = endpoints_.find(*sub);
            std::string detail = " [no downlink";
            if (ep != endpoints_.end()) {
                detail += " — " + ep->second.ip + ":"
                       +  std::to_string(ep->second.port) + " refused";
            }
            detail += "]";
            log_frame("GS→" + std::string(to_string(*sub)) + detail, first);
            continue;
        }
        if (!downlinks_[*sub]->send(native_for_sim)) {
            // Downlink may have died silently. Drop and reconnect once.
            downlinks_[*sub]->close();
            downlinks_.erase(*sub);
            if (!ensure_connected(*sub) || !downlinks_[*sub]->send(native_for_sim)) {
                log_frame("GS→" + std::string(to_string(*sub)) + " [send fail]", first);
                continue;
            }
        }

        // Remember the native TC we forwarded so it can be echoed in the
        // downlink TM frame's "GS TC cmd" field.
        {
            std::lock_guard<std::mutex> g(echo_mu_);
            last_tc_[*sub] = native_for_sim;
        }

        // Wait for the reply.  Read until quiet for ~30 ms or 1.5 s elapses.
        Bytes resp;
        auto deadline = steady_clock::now() + milliseconds(1500);
        while (steady_clock::now() < deadline) {
            auto chunk = downlinks_[*sub]->recv(8192, 200);
            if (!chunk.empty()) {
                resp.insert(resp.end(), chunk.begin(), chunk.end());
                // small grace window for trailing bytes
                auto tail_deadline = steady_clock::now() + milliseconds(30);
                while (steady_clock::now() < tail_deadline) {
                    auto more = downlinks_[*sub]->recv(8192, 5);
                    if (more.empty()) break;
                    resp.insert(resp.end(), more.begin(), more.end());
                }
                break;
            }
        }
        if (resp.empty()) {
            log_frame(std::string(to_string(*sub)) + "→GS [no response]", {});
            continue;
        }

        // ── If incoming was CCSDS, wrap the TM response ────────────────────
        Bytes outbound = resp;
        if (ccsds_in && tm_ && tm_frames_.load()) {
            // Fixed 1432-byte TM downlink frame (SOF FCA1). Native TM in
            // TM DATA, forwarded TC echoed in GS TC cmd.
            Bytes echo;
            {
                std::lock_guard<std::mutex> g(echo_mu_);
                auto it = last_tc_.find(*sub);
                if (it != last_tc_.end()) echo = it->second;
            }
            try {
                outbound = tm_->encode(*sub, resp, echo);
            } catch (const std::exception& e) {
                        throw;
            }
            if (tm_observer_) tm_observer_(outbound, *sub);
            std::lock_guard<std::mutex> g(m_);
            ++stats_.ccsds_out;
        } else if (ccsds_in && ccsds_) {
            outbound = ccsds_->encode(*sub, ccsds::CcsdsDirection::TM,
                                      ccsds_tc_id, resp);
            std::lock_guard<std::mutex> g(m_);
            ++stats_.ccsds_out;
        }

        log_frame(std::string(to_string(*sub)) + "→GS"
                  + (ccsds_in ? " [CCSDS]" : ""), outbound);
        {
            std::lock_guard<std::mutex> g(m_);
            ++stats_.routed[static_cast<int>(*sub)];
            ++stats_.frames_out;
        }
        transport::fd_send(fd, outbound);
    }
}

bool Bridge::start(const std::string& bind_ip, int port) {
    running_ = true;
    return srv_.start(bind_ip, port,
        [this](int fd, const std::string& peer) {
            handle_gs_client(fd, peer);
        });
}

void Bridge::stop() {
    running_ = false;
    srv_.stop();
    for (auto& [s, c] : downlinks_) if (c) c->close();
}

} // namespace xdos::obc
