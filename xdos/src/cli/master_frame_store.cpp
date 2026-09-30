// SPDX-License-Identifier: Apache-2.0
#include "cli/master_frame_store.hpp"
#include "transport/tcp_transport.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>

namespace xdos::cli {

using namespace std::chrono;
using namespace xdos::lut;

static std::string hms(int h, int m, int s) {
    char b[16]; std::snprintf(b, sizeof(b), "%02d:%02d:%02d", h, m, s); return b;
}
static std::string ymd(int y, int m, int d) {
    char b[16]; std::snprintf(b, sizeof(b), "%04d-%02d-%02d", y, m, d); return b;
}

std::string SubFrame::time_hms() const { return hms(hour, minute, second); }
std::string SubFrame::date_ymd() const { return ymd(year, month, day); }

static const SubFrame* first_present(const MasterFrame& mf) {
    for (const auto& s : mf.by_slot) if (s) return &*s;
    return nullptr;
}
static const SubFrame* last_present(const MasterFrame& mf) {
    for (int i = 15; i >= 0; --i) if (mf.by_slot[i]) return &*mf.by_slot[i];
    return nullptr;
}
std::string MasterFrame::start_time() const {
    const SubFrame* s = first_present(*this);
    return s ? s->time_hms() : "--:--:--";
}
std::string MasterFrame::end_time() const {
    const SubFrame* s = last_present(*this);
    return s ? s->time_hms() : "--:--:--";
}
std::string MasterFrame::date() const {
    const SubFrame* s = first_present(*this);
    return s ? s->date_ymd() : "----------";
}

MasterFrameStore::MasterFrameStore(const mib::Mib& mib, ccsds::CcsdsTmCodec& tm)
    : mib_(mib), tm_(tm) {}

bool MasterFrameStore::init(std::string* err) {
    for (const auto& p : { "config/rate_group_lut_bin_gs.json",
                           "../config/rate_group_lut_bin_gs.json",
                           "../../config/rate_group_lut_bin_gs.json" }) {
        if (std::filesystem::exists(p)) return init(p, err);
    }
    if (err) *err = "GS twin (config/rate_group_lut_bin_gs.json) not found — "
                    "raw-hex mode";
    has_twin_ = false;
    return true;                       // missing twin is not fatal
}

bool MasterFrameStore::init(const std::string& twin_path, std::string* err) {
    has_twin_ = BinLutTwin::load_file(twin_path, twin_, err);
    return has_twin_;
}

void MasterFrameStore::start(const std::string& host, int port) {
    if (running_.exchange(true)) return;
    th_ = std::thread([this, host, port] { recv_loop(host, port); });
}

void MasterFrameStore::stop() {
    if (!running_.exchange(false)) return;
    if (th_.joinable()) th_.join();
}

// ── tier block assembly: gather the slices present in this master frame ─────
void MasterFrameStore::assemble_block(const MasterFrame& mf, int tier, int k,
                                      Bytes& out, std::array<bool,8>& have) {
    const int span = kBlockSpan[tier];
    const int segw = kSegBytes[tier];
    const int base = k - k % span;               // first slot of the block
    out.assign(kBlockBudget[tier], 0x00);
    have.fill(false);
    for (int j = 0; j < span; ++j) {
        const auto& sf = mf.by_slot[base + j];
        if (!sf || (int)sf->tm_data.size() < kSubframeSize) continue;
        std::copy(sf->tm_data.begin() + kSegOffset[tier],
                  sf->tm_data.begin() + kSegOffset[tier] + segw,
                  out.begin() + (std::size_t)j * segw);
        have[j] = true;
    }
}

// ── per-entry decode against the blocks containing slot k ───────────────────
std::vector<EntryView> MasterFrameStore::decode_entries(const MasterFrame& mf,
                                                        int k) const {
    std::vector<EntryView> out;
    if (!has_twin_) return out;
    for (int t = 0; t < 4; ++t) {
        if (twin_.tiers[t].empty()) continue;
        Bytes block; std::array<bool,8> have{};
        assemble_block(mf, t, k, block, have);
        const int segw = kSegBytes[t];
        for (const auto& e : twin_.tiers[t]) {
            EntryView v;
            v.entry = &e;
            v.tier  = t;
            v.raw.assign(std::max(0, e.tm_raw_len), 0x00);
            bool missing = false, all_ff = true;
            for (const auto& p : e.picks) {
                if (p.size <= 0 || p.block < 0
                    || p.block + p.size > (int)block.size()) continue;
                for (int j = p.block / segw; j <= (p.block + p.size - 1) / segw; ++j)
                    if (!have[j]) missing = true;
                for (int i = 0; i < p.size; ++i) {
                    const Byte b = block[p.block + i];
                    if (b != 0xFF) all_ff = false;
                    if (p.src + i < (int)v.raw.size()) v.raw[p.src + i] = b;
                    v.pick_bytes.push_back(b);
                }
            }
            v.status = missing ? EntryView::PARTIAL
                     : all_ff  ? EntryView::DOWN
                               : EntryView::UP;
            out.push_back(std::move(v));
        }
    }
    return out;
}

void MasterFrameStore::ingest(const Bytes& frame) {
    auto d = tm_.decode(frame);
    if (!d) return;

    SubFrame sf;
    sf.raw = frame;
    sf.master_count = d->master_frame_count;
    sf.sub_count    = d->sub_frame_count;
    sf.rm_id        = d->radio_module_id;
    sf.slot         = (int)(d->sub_frame_count % kSubframesPerMaster);
    sf.mf_id        = d->sub_frame_count / kSubframesPerMaster;
    sf.year   = d->time_code.year;   sf.month = d->time_code.month;
    sf.day    = d->time_code.day;    sf.hour  = d->time_code.hour;
    sf.minute = d->time_code.minute; sf.second = d->time_code.second;
    sf.ms     = d->time_code.milliseconds;
    sf.crc_ok = d->crc_ok;
    sf.tm_data = d->tm_data;

    std::lock_guard<std::mutex> g(mu_);
    ++frames_rx_;
    // Position by the per-APID sub-frame count: slot within the MF, MF by id.
    // Any mf_id change (loss, OBC restart, u16 wrap) starts a new MF; the
    // gaps it leaves in by_slot stay visible.
    if (masters_.empty() || masters_.back().mf_id != sf.mf_id) {
        MasterFrame mf;
        mf.index = masters_.empty() ? 1 : masters_.back().index + 1;
        mf.mf_id = sf.mf_id;
        masters_.push_back(std::move(mf));
        if (masters_.size() > kMaxMasters)
            masters_.erase(masters_.begin());   // rolling window
    }
    MasterFrame& mf = masters_.back();
    mf.by_slot[sf.slot] = sf;                   // duplicates overwrite

    // Entry summary for the list lines (needs the containing MF's slices).
    if (has_twin_) {
        auto views = decode_entries(mf, sf.slot);
        SubFrame& stored = *mf.by_slot[sf.slot];
        stored.up_count = stored.down_count = stored.partial_count = 0;
        stored.entry_total = (int)views.size();
        for (const auto& v : views) {
            if      (v.status == EntryView::UP)   ++stored.up_count;
            else if (v.status == EntryView::DOWN) ++stored.down_count;
            else                                  ++stored.partial_count;
        }
        sf = stored;                            // keep the ring copy in sync
    }

    subframes_.push_back(sf);
    if (subframes_.size() > kMaxSubframes)
        subframes_.erase(subframes_.begin());
    log_subframe(mf, sf);

    // Hand the positioned subframe on (MCC forwarding).  Done last so a slow
    // or dead downstream peer cannot delay the store's own bookkeeping.
    if (subframe_fn_) subframe_fn_(sf);
}

std::vector<SubFrame> MasterFrameStore::recent_subframes(std::size_t max) const {
    std::lock_guard<std::mutex> g(mu_);
    if (subframes_.size() <= max) return subframes_;
    return std::vector<SubFrame>(subframes_.end() - max, subframes_.end());
}

void MasterFrameStore::enable_logging(const std::string& path) {
    std::lock_guard<std::mutex> g(log_mu_);
    log_path_ = path;
    log_fs_.open(path, std::ios::out | std::ios::app);
    if (log_fs_) {
        log_fs_ << "# XDOS Rate Group Command TM Response log\n"
                << "# Every received subframe (1432-byte TM frame, APID "
                << tm_.rate_group_apid() << ") is recorded below.\n"
                << "# Layout per subframe: VH[0:400] H[400:800] M[800:1100] "
                   "L[1100:1275] RSVD[5].\n"
                << "# Each subframe opens with a [TIER ..] line for ALL FOUR "
                   "tiers, so every\n"
                << "# subframe shows whether VH/H/M/L are arriving.  'slice j/n "
                   "[x..]' is the\n"
                << "# block slice this subframe carried and which slices have "
                   "arrived ('.' = not\n"
                << "# yet / dropped).  UP/DOWN/PARTIAL count the tier's entries "
                   "(DOWN = all picked\n"
                << "# bytes 0xFF = subsystem silent; PARTIAL = a needed slice is "
                   "missing).\n"
                << "# The per-entry value dump follows for tiers whose block "
                   "completes on this slot.\n\n";
        log_fs_.flush();
    }
}

// Plain-text (no ANSI) record of one subframe: header + the entries whose
// tier is due at this slot (VH always; H/M/L when their block completes).
void MasterFrameStore::log_subframe(const MasterFrame& mf, const SubFrame& sf) {
    std::lock_guard<std::mutex> g(log_mu_);
    if (!log_fs_) return;
    char hdr[176];
    std::snprintf(hdr, sizeof(hdr),
        "==== Subframe  %s %s.%03u   MF#%u slot %d/16  MFC=%u SFC=%u RM=%d  CRC %s ====\n",
        sf.date_ymd().c_str(), sf.time_hms().c_str(), sf.ms,
        sf.mf_id, sf.slot + 1, sf.master_count, sf.sub_count, sf.rm_id,
        sf.crc_ok ? "OK" : "BAD");
    log_fs_ << hdr;
    if (!has_twin_) {
        log_fs_ << "  (no GS twin loaded — raw frames only)\n\n";
        log_fs_.flush();
        return;
    }
    // Tier coverage FIRST, for every tier, on every subframe — this is the
    // "did VH/H/M/L all arrive" debug.  The per-entry dump below still only
    // prints a tier on the slot where its block completes (that is when its
    // values are final); this table is what makes the other slots readable.
    for (const auto& c : tier_coverage(mf, sf.slot)) {
        char line[200];
        char slices[16] = {0};
        for (int j = 0; j < c.span && j < 8; ++j) slices[j] = c.have[j] ? 'x' : '.';
        const std::string blk =
            c.block_complete ? "COMPLETE"
                             : "FILLING (" + std::to_string(c.missing_slices())
                               + " slice(s) missing)";
        std::snprintf(line, sizeof(line),
            "  [TIER %-2s] entries %2d  slice %d/%d [%-8s]  UP %2d  DOWN %2d  "
            "PARTIAL %2d  block %s\n",
            kTierName[c.tier], c.entries, c.slice + 1, c.span, slices,
            c.up, c.down, c.partial, blk.c_str());
        log_fs_ << line;
    }

    auto views = decode_entries(mf, sf.slot);
    for (const auto& v : views) {
        // VH completes every slot; other tiers on their block's last slot.
        const int span = kBlockSpan[v.tier];
        if (span > 1 && sf.slot % span != span - 1) continue;
        const auto& e = *v.entry;
        log_fs_ << "  [" << kTierName[v.tier] << "] " << e.id << "  "
                << to_string(e.sub) << "  "
                << (v.status == EntryView::UP   ? "UP"
                  : v.status == EntryView::DOWN ? "DOWN" : "PARTIAL") << "\n";
        log_fs_ << "    HEX:";
        if (v.status == EntryView::DOWN) {
            log_fs_ << " (all picked bytes 0xFF — subsystem not responding)";
        } else {
            char b[4];
            for (std::size_t i = 0; i < v.pick_bytes.size(); ++i) {
                std::snprintf(b, sizeof(b), " %02x", v.pick_bytes[i]);
                log_fs_ << b;
            }
        }
        log_fs_ << "\n";
        if (v.status == EntryView::UP && decode_text_fn_) {
            std::string dec = decode_text_fn_(e, v.raw);
            if (!dec.empty()) log_fs_ << dec;   // callback returns indented lines
        }
    }
    log_fs_ << "\n";
    log_fs_.flush();
}

// Coverage of every tier at slot k.  decode_entries() already decodes all
// four tiers against the blocks containing k; this groups those results per
// tier and adds the block-slice picture from assemble_block(), so a caller
// can see H/M/L on subframes where their block has not completed yet.
std::vector<TierCoverage> MasterFrameStore::tier_coverage(const MasterFrame& mf,
                                                          int k) const {
    std::vector<TierCoverage> out;
    if (!has_twin_) return out;
    auto views = decode_entries(mf, k);
    for (int t = 0; t < 4; ++t) {
        TierCoverage c;
        c.tier  = t;
        c.span  = kBlockSpan[t];
        c.slice = k % c.span;
        c.entries = (int)twin_.tiers[t].size();
        for (const auto& v : views) {
            if (v.tier != t) continue;
            switch (v.status) {
                case EntryView::UP:      ++c.up;      break;
                case EntryView::DOWN:    ++c.down;    break;
                case EntryView::PARTIAL: ++c.partial; break;
            }
        }
        Bytes block;
        assemble_block(mf, t, k, block, c.have);
        c.block_complete = (c.missing_slices() == 0);
        out.push_back(c);
    }
    return out;
}

void MasterFrameStore::recv_loop(std::string host, int port) {
    while (running_.load()) {
        transport::TcpClient c;
        if (!c.connect(host, port, 500)) {
            connected_.store(false);
            std::this_thread::sleep_for(milliseconds(500));
            continue;
        }
        connected_.store(true);
        Bytes acc;
        while (running_.load()) {
            auto chunk = c.recv(8192, 300);
            if (chunk.empty()) continue;      // timeout; keep waiting
            acc.insert(acc.end(), chunk.begin(), chunk.end());
            // Extract complete 1432-byte frames from the stream.
            while (acc.size() >= ccsds::CcsdsTmCodec::kFrameLen) {
                // Resync to SOF FCA1 if needed.
                if (!(acc[0] == 0xFC && acc[1] == 0xA1)) {
                    std::size_t k = 1;
                    while (k + 1 < acc.size() && !(acc[k]==0xFC && acc[k+1]==0xA1)) ++k;
                    acc.erase(acc.begin(), acc.begin() + k);
                    if (acc.size() < ccsds::CcsdsTmCodec::kFrameLen) break;
                }
                Bytes frame(acc.begin(), acc.begin() + ccsds::CcsdsTmCodec::kFrameLen);
                acc.erase(acc.begin(), acc.begin() + ccsds::CcsdsTmCodec::kFrameLen);
                ingest(frame);
            }
        }
        connected_.store(false);
    }
}

std::vector<MasterFrame> MasterFrameStore::master_frames() const {
    std::lock_guard<std::mutex> g(mu_);
    return masters_;
}
std::optional<MasterFrame> MasterFrameStore::master_by_id(std::uint32_t mf_id) const {
    std::lock_guard<std::mutex> g(mu_);
    for (auto it = masters_.rbegin(); it != masters_.rend(); ++it)
        if (it->mf_id == mf_id) return *it;
    return std::nullopt;
}
std::size_t MasterFrameStore::master_count() const {
    std::lock_guard<std::mutex> g(mu_);
    return masters_.size();
}

}  // namespace xdos::cli
