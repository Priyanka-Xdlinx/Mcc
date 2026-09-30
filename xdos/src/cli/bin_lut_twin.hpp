// SPDX-License-Identifier: Apache-2.0
// GS twin of the binary rate-group LUT (rate_group_lut_bin_gs.json).
//
// The OBC binary carries no names or ids; this ground-only twin (written by
// BinaryLutBuilder next to the .bin) holds the labels, response names, and
// pick parameter lists the viewer needs to decode the tiered master frame.
// Entry position within a tier is the OBC<->GS link; the shared crc16 lets
// the operator eyeball that both sides hold the same table.

#pragma once

#include "common/types.hpp"
#include "lut/lut_format.hpp"

#include <array>
#include <string>
#include <vector>

namespace xdos::cli {

struct TwinPick {
    int src = 0, size = 0, block = 0;
    std::vector<std::string> params;   // names covered; "<full payload>"/"<bytes>"
};

struct TwinEntry {
    std::string id, tc, rsp;
    Subsystem   sub = Subsystem::EPS;
    int apid = 0, tm_raw_len = 0, gps_log_id = -1;
    std::vector<TwinPick> picks;
    int pick_bytes() const {
        int t = 0; for (const auto& p : picks) t += p.size; return t;
    }
};

struct BinLutTwin {
    std::uint16_t crc16 = 0;
    int subframe_size = lut::kSubframeSize;
    std::array<std::vector<TwinEntry>,4> tiers;   // VH, H, M, L (positional)

    int entry_count() const {
        int n = 0; for (const auto& t : tiers) n += (int)t.size(); return n;
    }
    static bool load_file(const std::string& path, BinLutTwin& out,
                          std::string* err = nullptr);
};

}  // namespace xdos::cli
