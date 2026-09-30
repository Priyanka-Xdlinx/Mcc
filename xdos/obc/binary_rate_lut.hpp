// SPDX-License-Identifier: Apache-2.0
// OBC-side binary rate-group LUT — parser for the uplinked XLUT image.
//
// The OBC has NO subsystem Database.  This module turns the opaque binary
// built on the ground (src/lut/binary_lut_builder.*) into an executable
// table: four tiers (VH/H/M/L) of entries, each carrying the exact native
// TC bytes to replay and the pick triplets that route response bytes into
// the tier's block buffer.  Every structural property is validated here —
// magic, version, fixed partition, CRC-16, segment offsets, entry sync
// words, pick bounds — so the engine can trust the table blindly.
//
// Format reference: src/lut/binary_lut_builder.hpp (and lut_format.hpp for
// the shared constants).  Error strings are operator-facing: they are sent
// back verbatim in the uplink reply.

#ifndef XDOS_OBC_BINARY_RATE_LUT_HPP
#define XDOS_OBC_BINARY_RATE_LUT_HPP

#include "common/types.hpp"
#include "lut/lut_format.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace xdos::obc {

struct BinPick {
    std::uint16_t src   = 0;   // byte offset in the raw response payload
    std::uint16_t size  = 0;   // bytes to copy
    std::uint16_t block = 0;   // destination offset inside the tier's block
};

struct BinEntry {
    Subsystem     sub  = Subsystem::EPS;   // derived from the hardware APID
    std::uint8_t  apid = 0;
    std::uint8_t  flags = 0;
    Bytes         tc_bytes;                // native frame, replayed verbatim
    std::uint16_t tm_raw_len = 0;          // raw response payload length
    std::vector<BinPick> picks;
    int pick_bytes() const {
        int t = 0; for (const auto& p : picks) t += p.size; return t;
    }
};

struct BinaryRateLut {
    std::uint16_t crc     = 0;             // file signature (header offset 48)
    int           t_sf_ms = lut::kTsfMs;   // subframe period (0 in file -> 1000)
    std::array<std::vector<BinEntry>,4> tiers;   // VH, H, M, L

    int entry_count() const {
        int n = 0; for (const auto& t : tiers) n += (int)t.size(); return n;
    }
    int pick_bytes() const {
        int n = 0;
        for (const auto& t : tiers) for (const auto& e : t) n += e.pick_bytes();
        return n;
    }
    bool empty() const { return entry_count() == 0; }

    // Parse + validate a complete XLUT image.  On failure returns false and
    // sets *err to a short operator-facing reason.
    static bool parse(const Bytes& image, BinaryRateLut& out,
                      std::string* err = nullptr);
    static bool load_file(const std::string& path, BinaryRateLut& out,
                          std::string* err = nullptr);
};

} // namespace xdos::obc

#endif // XDOS_OBC_BINARY_RATE_LUT_HPP
