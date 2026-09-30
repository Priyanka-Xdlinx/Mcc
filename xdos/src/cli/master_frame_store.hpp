// SPDX-License-Identifier: Apache-2.0
// Master Frame store (GS side) — tiered binary-LUT decode.
//
// Receives the OBC's autonomous rate-group subframes (1432-byte TM frames,
// SOF FCA1, APID 600) and reassembles the tiered master frame the binary LUT
// defines:
//
//   subframe 1280 B = VH[0:400] H[400:800] M[800:1100] L[1100:1275] RSVD[5]
//   master frame    = 16 subframes; an H block spans 2 subframes (400+400),
//                     M spans 4 (300x4), L spans 8 (175x8)
//
// The TM header's per-APID sub-frame count is the position key:
//   slot  = SFC % 16   (which subframe of the master frame)
//   mf id = SFC / 16   (which master frame)
// so a dropped frame leaves a visible gap instead of silently shifting the
// alignment.  Entry labels/picks come from the GS twin of the binary LUT
// (config/rate_group_lut_bin_gs.json); without a twin the store still
// collects frames and the viewer falls back to raw hex.
//
// A background thread connects to the OBC's rate-group port and feeds frames
// in; the CLI's browser reads the accumulated master frames.

#ifndef XDOS_CLI_MASTER_FRAME_STORE_HPP
#define XDOS_CLI_MASTER_FRAME_STORE_HPP

#include "cli/bin_lut_twin.hpp"
#include "codec/ccsds_tm_codec.hpp"
#include "mib/mib.hpp"
#include "common/types.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace xdos::cli {

// One received subframe = one rate-group push.
struct SubFrame {
    std::uint32_t master_count = 0;   // from TM header (global counter)
    std::uint32_t sub_count    = 0;   // per-APID counter — the position key
    int           rm_id        = 0;
    int           slot  = 0;          // sub_count % 16
    std::uint32_t mf_id = 0;          // sub_count / 16
    // Timestamp from the frame's CCS Time Code header:
    std::uint16_t year = 0; std::uint8_t month=0, day=0, hour=0, minute=0, second=0;
    std::uint16_t ms = 0;
    bool crc_ok = true;
    Bytes tm_data;                    // the 1280-B TM DATA field
    Bytes raw;                        // full 1432-byte frame (for hex view)
    // Entry summary for list lines (computed at ingest; -1 = no twin):
    int up_count = -1, down_count = 0, partial_count = 0, entry_total = 0;

    std::string time_hms() const;     // "17:00:00"
    std::string date_ymd() const;     // "2026-07-07"
};

// A master frame = 16 slot-positioned subframes (gaps stay visible).
struct MasterFrame {
    int           index = 0;          // 1-based display number (monotonic)
    std::uint32_t mf_id = 0;          // sub_count / 16
    std::array<std::optional<SubFrame>,16> by_slot;
    int  filled() const {
        int n = 0; for (const auto& s : by_slot) if (s) ++n; return n;
    }
    bool complete() const { return filled() >= 16; }
    // Span from first/last present subframe timestamps:
    std::string start_time() const;
    std::string end_time() const;
    std::string date() const;
};

// One LUT entry decoded from a master frame's block data.
struct EntryView {
    const TwinEntry* entry = nullptr;
    int  tier = 0;
    enum Status { UP, DOWN, PARTIAL } status = PARTIAL;
        // DOWN    = every picked byte is 0xFF (subsystem not responding)
        // PARTIAL = a subframe slice this entry's picks need is missing
    Bytes raw;          // tm_raw_len bytes, sparse: picked ranges filled in
    Bytes pick_bytes;   // the picked bytes back-to-back (hex view)
};

// Per-tier coverage of ONE subframe — the "is every tier arriving?" debug.
//
// decode_entries() answers "what do the blocks containing this slot say", but
// a tier whose block spans several subframes only completes on its last slice,
// so a plain entry list leaves H/M/L invisible on most subframes.  This is the
// view that is filled in for EVERY tier on EVERY subframe: which slice of the
// tier's block this subframe carried, which slices have arrived so far, and
// how the tier's entries currently decode.
struct TierCoverage {
    int tier    = 0;              // 0..3 = VH/H/M/L
    int entries = 0;              // LUT entries in this tier
    int up = 0, down = 0, partial = 0;
    int slice = 0, span = 1;      // this subframe carries slice/span of the block
    std::array<bool,8> have {};   // which slices of the block are present
    bool block_complete = false;  // every slice present
    int  missing_slices() const {
        int n = 0; for (int j = 0; j < span; ++j) if (!have[j]) ++n; return n;
    }
};

class MasterFrameStore {
public:
    MasterFrameStore(const mib::Mib& mib, ccsds::CcsdsTmCodec& tm);

    // Load the GS twin of the binary LUT.  A missing twin is NOT fatal —
    // the store runs in raw-hex mode (has_twin() == false).
    bool init(std::string* err = nullptr);
    bool init(const std::string& twin_path_override, std::string* err = nullptr);
    bool has_twin() const { return has_twin_; }
    const BinLutTwin& twin() const { return twin_; }

    // Start/stop the background receiver connected to host:port (10002).
    void start(const std::string& host, int port);
    void stop();

    // Feed one raw 1432-byte frame (used by the receiver and by tests).
    void ingest(const Bytes& frame);

    // Snapshot for the browser.
    std::vector<MasterFrame> master_frames() const;
    std::optional<MasterFrame> master_by_id(std::uint32_t mf_id) const;
    std::size_t master_count() const;
    bool connected() const { return connected_.load(); }
    std::uint64_t frames_received() const { return frames_rx_.load(); }

    // Flat list of the most recent subframes (newest last), for the TM
    // response window.  Capped to a rolling window.
    std::vector<SubFrame> recent_subframes(std::size_t max = 200) const;

    // Assemble tier t's block containing slot k from the subframes present
    // in mf.  out is kBlockBudget[t]-sized; have[j] marks slice j present.
    static void assemble_block(const MasterFrame& mf, int tier, int k,
                               Bytes& out, std::array<bool,8>& have);

    // Decode every LUT entry against the blocks containing slot k.
    // Requires a twin; returns empty otherwise.
    std::vector<EntryView> decode_entries(const MasterFrame& mf, int k) const;

    // All four tiers' coverage of slot k — one row per tier, ALWAYS, whether
    // or not that tier's block completes here.  Requires a twin.
    std::vector<TierCoverage> tier_coverage(const MasterFrame& mf, int k) const;

    // Auto-logging: every received subframe is appended (hex + decoded) to a
    // dedicated plain-text file.  Enable once at startup.
    void enable_logging(const std::string& path);
    const std::string& log_path() const { return log_path_; }

    // The store can log HEX by itself, but DECODED values need the Database +
    // formatter that live in the CLI.  The CLI installs this callback: given
    // an entry and its (sparse) raw payload, return indented decoded lines.
    using DecodeTextFn =
        std::function<std::string(const TwinEntry&, const Bytes& raw)>;
    void set_decode_text_fn(DecodeTextFn fn) { decode_text_fn_ = std::move(fn); }

    // Called once per ingested subframe, after it has been positioned in its
    // master frame — the hook the MCC forwarder hangs off.  Runs on the
    // receiver thread, so the callee must not block.
    using SubframeFn = std::function<void(const SubFrame&)>;
    void set_subframe_fn(SubframeFn fn) { subframe_fn_ = std::move(fn); }

private:
    void recv_loop(std::string host, int port);
    void log_subframe(const MasterFrame& mf, const SubFrame& sf);

    const mib::Mib&       mib_;
    ccsds::CcsdsTmCodec&  tm_;
    BinLutTwin            twin_;
    bool                  has_twin_ = false;

    mutable std::mutex    mu_;
    std::vector<MasterFrame> masters_;   // rolling cap applied
    static constexpr std::size_t kMaxMasters = 256;
    std::vector<SubFrame> subframes_;    // flat ring for the TM window
    static constexpr std::size_t kMaxSubframes = 512;

    DecodeTextFn          decode_text_fn_;
    SubframeFn            subframe_fn_;
    std::string           log_path_;
    std::ofstream         log_fs_;
    std::mutex            log_mu_;

    std::atomic<bool>     running_ { false };
    std::atomic<bool>     connected_ { false };
    std::atomic<std::uint64_t> frames_rx_ { 0 };
    std::thread           th_;
};

}  // namespace xdos::cli
#endif
