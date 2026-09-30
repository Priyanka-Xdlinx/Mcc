// SPDX-License-Identifier: Apache-2.0
// Binary rate-group LUT builder (GROUND-SIDE, Database-driven).
//
// Turns the human-editable source config (config/rate_group_commands.json,
// tiers -> commands -> picks) into:
//
//   * rate_group_lut.bin        — the OBC binary (Database-free, id-less)
//   * rate_group_lut_bin_gs.json— the GS twin (ids, names, pick params)
//
// per docs/binary_lut_design.md (Master Frame rev. 2026-08-03):
//
//   Subframe 1280 B = VH[0:400] H[400:800] M[800:1100] L[1100:1275] RSVD[5]
//   Block budgets   = VH 400, H 800 (400x2), M 1200 (300x4), L 1400 (175x8)
//
// Picks address the RAW response payload (Database byte space, framing
// excluded).  The operator selects PARAMETERS in the GS CLI; this builder
// resolves them to byte ranges, merges overlapping/adjacent ranges, and
// assigns block-relative destination offsets.  A TM may carry MULTIPLE
// picks — each becomes one 6-byte triplet in the entry.
//
// File layout (all little-endian):
//
//   Header (50 B):
//     0  "XLUT"                      4  magic
//     4  u8  version = 1
//     5  u8  flags   = 0
//     6  u16 subframe_size = 1280
//     8  u16 seg_bytes VH = 400
//    10  u16 seg_bytes H  = 400
//    12  u16 seg_bytes M  = 300
//    14  u16 seg_bytes L  = 175
//    16  u8  reserved_bytes = 5
//    17  u8  pad = 0
//    18  u16 t_sf_ms = 1000         (subframe period T_sf; 0 in legacy files)
//    20  u16 entry_count VH/H/M/L   (4 x u16)
//    28  u32 seg_offset  VH/H/M/L   (4 x u32, absolute file offsets)
//    44  u32 file_length
//    48  u16 crc16-ccitt            (poly 0x1021 init 0xFFFF, crc field
//                                    zeroed during computation)
//   Entry:
//     u16 sync = 0xA55A
//     u8  apid            (hardware APID: ADCS 1, EPS 3, GPS 4, XLINK-S 6)
//     u8  flags = 0
//     u16 tc_len
//     ..  tc bytes        (pre-built NATIVE frame, replayed verbatim)
//     u16 tm_raw_len      (raw response payload length per Database)
//     u8  pick_count
//     ..  pick_count x { u16 src_off, u16 size, u16 block_off }
//
// No command ids anywhere in the binary: entry position within its segment
// is the only OBC<->GS link, protected by the shared CRC-16 signature.

#pragma once

#include "common/types.hpp"
#include "lut/lut_format.hpp"
#include "mib/mib.hpp"
#include "subsystems/xlink_codec.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace xdos::lut {

// ── source config ────────────────────────────────────────────────────────────
struct PickSpec {                       // one operator pick request
    std::vector<std::string> params;    // parameter names (may be several)
    std::optional<std::pair<int,int>> bytes;  // raw {offset,size} escape hatch
};
struct SourceCmd {
    std::string id;                     // human label ("EP-003") — GS twin only
    Subsystem   sub = Subsystem::EPS;
    std::string tc_name;                // TC / XLink event ("LOG" for GPS)
    std::string rsp_name;               // response TM in the Database
    int         gps_log_id = -1;        // GPS: which log; -1 otherwise
    std::vector<PickSpec> picks;        // empty => one full-payload pick
};

// ── build products ───────────────────────────────────────────────────────────
struct BuiltPick {
    uint16_t src = 0, size = 0, block = 0;
    std::vector<std::string> params;    // names covered (GS twin / report)
};
struct BuiltEntry {
    SourceCmd src;
    int       apid = 0;
    Bytes     tc_bytes;                 // native frame, replayed verbatim
    uint16_t  tm_raw_len = 0;
    std::vector<BuiltPick> picks;
    int pick_bytes() const { int t=0; for (auto& p:picks) t+=p.size; return t; }
};
struct Segment {
    std::vector<BuiltEntry> entries;
    int used = 0;                       // pick bytes packed into the block
    int budget = 0;
};
struct BuildResult {
    std::array<Segment,4> segs;
    Bytes          file;                // the complete binary image
    uint16_t       crc = 0;
    nlohmann::json gs_twin;
    std::vector<std::string> errors;    // non-empty => build REJECTED
    std::vector<std::string> warnings;
    bool ok() const { return errors.empty(); }
};

// ── builder ──────────────────────────────────────────────────────────────────
class BinaryLutBuilder {
public:
    explicit BinaryLutBuilder(const mib::Mib& mib) : mib_(mib) {}

    // Product key baked into XLINK entries' TC bytes (replayed verbatim by
    // the OBC).  Defaults to all-zero — accepted by the simulator only; load
    // the real key from the mib/xlink_keys/ catalog for bench/flight builds.
    void set_xlink_key(const xlink::codec::ProductKey& k) { xlink_key_ = k; }

    // Parse config/rate_group_commands.json.  On error returns false.
    static bool load_source(const std::string& path, nlohmann::json& out,
                            std::string* err = nullptr);

    // Full pipeline: resolve TCs + responses via the Database, resolve picks
    // (names -> byte ranges, merge overlapping/adjacent), pack block offsets,
    // validate budgets, emit the binary image + GS twin.  Deterministic:
    // identical source + Database => byte-identical file.
    BuildResult build(const nlohmann::json& source) const;

    static bool write(const BuildResult& r, const std::string& bin_path,
                      const std::string& gs_json_path, std::string* err = nullptr);

    // CRC-16-CCITT (poly 0x1021, init 0xFFFF) — also the file signature.
    static uint16_t crc16_ccitt(const Bytes& data);

    // Response parameter map for the CLI pick editor:
    // {byte_off, byte_size, name, data_type, unit} per Database parameter.
    struct ParamByte { int off=0, size=0; std::string name, type, unit; };
    std::vector<ParamByte> response_param_map(Subsystem sub,
                                              const std::string& rsp_name) const;
    int raw_response_len(Subsystem sub, const std::string& rsp_name) const;

private:
    const mib::Mib& mib_;
    xlink::codec::ProductKey xlink_key_{};   // zero default (sim-only)
    Bytes encode_tc(const SourceCmd& c, std::string& err) const;
    bool  resolve_picks(const SourceCmd& c, uint16_t tm_len,
                        std::vector<BuiltPick>& out,
                        std::vector<std::string>& errors) const;
};

} // namespace xdos::lut
