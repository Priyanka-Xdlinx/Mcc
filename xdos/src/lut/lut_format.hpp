// SPDX-License-Identifier: Apache-2.0
// Binary rate-group LUT — shared on-disk format constants (GROUND + OBC).
//
// Single source of truth for the XLUT layout described in
// docs/binary_lut_design.md and src/lut/binary_lut_builder.hpp.  This header
// is dependency-free (no MIB, no JSON) so the Database-free OBC and the
// ground tools compile against the very same numbers.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace xdos::lut {

inline constexpr int      kSubframeSize   = 1280;
inline constexpr int      kReservedBytes  = 5;
inline constexpr int      kHeaderSize     = 50;
inline constexpr uint16_t kEntrySync      = 0xA55A;
inline constexpr uint8_t  kFormatVersion  = 1;
inline constexpr uint16_t kTsfMs          = 1000;   // subframe period T_sf
inline constexpr int      kSubframesPerMaster = 16;
// per-subframe segment widths / in-subframe offsets, per-BLOCK spans + budgets
inline constexpr std::array<int,4> kSegBytes   { 400, 400, 300, 175 };
inline constexpr std::array<int,4> kSegOffset  {   0, 400, 800,1100 };
inline constexpr std::array<int,4> kBlockSpan  {   1,   2,   4,   8 };
inline constexpr std::array<int,4> kBlockBudget{ 400, 800,1200,1400 };
inline constexpr std::array<const char*,4> kTierName { "VH","H","M","L" };

// CRC-16-CCITT (poly 0x1021, init 0xFFFF) — the file signature at offset 48,
// computed over the whole image with the CRC field zeroed.
inline uint16_t crc16_ccitt(const uint8_t* d, std::size_t n) {
    uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < n; ++i) {
        crc ^= static_cast<uint16_t>(d[i]) << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

} // namespace xdos::lut
