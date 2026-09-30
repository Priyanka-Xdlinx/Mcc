// SPDX-License-Identifier: Apache-2.0
// XDOS Ground Station — CRC implementations used by subsystem framers.
//
//  crc8        — poly 0x07, init 0x00, no reflection, no XOR-out.
//                Used by EPS RS485 framing (NL EPS protocol "Msg Structure"
//                sheet). The same polynomial as CRC-8/CCITT.
//
//  crc32_oem7  — NovAtel OEM7 32-bit CRC. Reflected, init 0, poly 0xEDB88320
//                in reversed form (== 0x04C11DB7 standard). Used by every
//                OEM7 binary log and command.
#pragma once

#include "common/types.hpp"

namespace xdos::crc {

std::uint8_t  crc8(const Byte* data, std::size_t len, std::uint8_t poly = 0x07);
std::uint32_t crc32_oem7(const Byte* data, std::size_t len);

inline std::uint8_t  crc8(const Bytes& b, std::uint8_t poly = 0x07) {
    return crc8(b.data(), b.size(), poly);
}
inline std::uint32_t crc32_oem7(const Bytes& b) {
    return crc32_oem7(b.data(), b.size());
}

} // namespace xdos::crc
