// SPDX-License-Identifier: Apache-2.0
#include "common/crc.hpp"

namespace xdos::crc {

std::uint8_t crc8(const Byte* data, std::size_t len, std::uint8_t poly) {
    std::uint8_t crc = 0;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc & 0x80) ? static_cast<std::uint8_t>((crc << 1) ^ poly)
                               : static_cast<std::uint8_t>(crc << 1);
        }
    }
    return crc;
}

std::uint32_t crc32_oem7(const Byte* data, std::size_t len) {
    std::uint32_t crc = 0;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
        }
    }
    return crc;
}

} // namespace xdos::crc
