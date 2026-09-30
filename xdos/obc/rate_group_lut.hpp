// SPDX-License-Identifier: Apache-2.0
// Rate-group Lookup Table (LUT).
//
// The rate-group command set is a LOOKUP TABLE, not compiled logic, so it can
// be edited in one place and uplinked to the OBC when it changes.  Crucially,
// the OBC has NO subsystem Database: the ground station owns the Database,
// pre-builds each poll command's exact TC bytes, and stores them in the LUT.
// The OBC simply REPLAYS those opaque bytes on its 50 ms timer — it never has
// to understand a command to send it.
//
// Each row is fully self-contained so the OBC can act on it with no lookup:
//
//   order     slot position (1..N) — also the pack order in the TM frame
//   subsystem which link/port to send on (EPS/GPS/XLINK)
//   tc_hex    the exact native TC bytes to send, pre-built by the ground
//   resp_size how many payload bytes to store for this command's slot
//
// There is deliberately NO command id in the OBC LUT: the OBC never needs a
// label to replay bytes and store a payload by position, so carrying id
// strings would only waste OBC memory.  The ground keeps a matching full LUT
// (with ids + response names) and supplies labels by slot position at decode.
//
// The table carries a layout signature so a GS decoder can detect a frame that
// was produced against a different LUT revision.

#ifndef XDOS_OBC_RATE_GROUP_LUT_HPP
#define XDOS_OBC_RATE_GROUP_LUT_HPP

#include "common/types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace xdos::obc {

struct RateGroupLutRow {
    int         order    = 0;        // 1-based slot / pack position (the ONLY
                                     // link to the GS-side label — no id here)
    Subsystem   sub      = Subsystem::EPS;
    Bytes       tc_bytes;            // pre-built native TC to replay
    int         resp_size = 0;       // payload bytes to store in this slot
};

struct RateGroupLut {
    int         revision  = 0;       // bump on every ground edit
    std::string generated;           // ISO timestamp from the generator
    std::uint16_t signature = 0;     // layout signature (from offsets/sizes)
    std::vector<RateGroupLutRow> rows;

    int total_payload() const {      // sum of resp_size (the used region)
        int t = 0; for (const auto& r : rows) t += r.resp_size; return t;
    }

    // Load from a LUT JSON file (the default table, or an uplinked one).
    static bool load_file(const std::string& path, RateGroupLut& out,
                          std::string* err = nullptr);
    // Parse from an in-memory JSON string (used by the uplink receiver).
    static bool load_json(const std::string& json_text, RateGroupLut& out,
                          std::string* err = nullptr);

    // Recompute the signature from the current rows (offset/size rolling sum).
    // Matches the GS-side layout signature so drift is detectable.
    std::uint16_t compute_signature() const;
};


// ─── GS-SIDE decode table ────────────────────────────────────────────────────
// The ground keeps a fuller table (id + response name per slot) so it can
// LABEL and DECODE the frame.  It is loaded from the GS LUT file
// (rate_group_lut_gs.json), which is matched to the OBC LUT by 'order' and
// shares the same 'signature'.  This replaces the old hardcoded command list.
struct RateGroupGsSlot {
    int         order = 0;
    std::string id;                  // "EP-003"
    Subsystem   sub   = Subsystem::EPS;
    int         offset = 0;          // byte offset in the 1280-B TM DATA field
    int         size   = 0;          // payload bytes
    std::string rsp_name;            // TM response message name (for decode)
};

struct RateGroupGsTable {
    int           revision  = 0;
    std::uint16_t signature = 0;
    std::vector<RateGroupGsSlot> slots;

    // Load from the GS LUT json.  Offsets are derived from the cumulative
    // resp_size in 'order' sequence.
    static bool load_file(const std::string& path, RateGroupGsTable& out,
                          std::string* err = nullptr);

    const RateGroupGsSlot* find(const std::string& id) const;
};

}  // namespace xdos::obc
#endif
