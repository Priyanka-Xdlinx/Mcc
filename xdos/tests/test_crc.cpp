// SPDX-License-Identifier: Apache-2.0
#include "common/crc.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace xdos;

TEST_CASE("CRC-8/poly=0x07 known vectors", "[crc]") {
    // empty
    REQUIRE(crc::crc8(Bytes{}) == 0x00);
    // single byte 0x00
    REQUIRE(crc::crc8(Bytes{0x00}) == 0x00);
    // single byte 0xFF
    REQUIRE(crc::crc8(Bytes{0xFF}) == 0xF3);
    // standard CRC-8 reference: "123456789" -> 0xF4
    Bytes ref{'1','2','3','4','5','6','7','8','9'};
    REQUIRE(crc::crc8(ref) == 0xF4);
}

TEST_CASE("CRC-32 (OEM7) known vectors", "[crc]") {
    // NovAtel OEM7 CRC-32: poly 0xEDB88320, init 0, no XOR-out.
    // For ASCII "123456789" this gives 0x2DFD2D88 (NOT the standard
    // 0xCBF43926, which uses init=0xFFFFFFFF and final XOR).
    Bytes ref{'1','2','3','4','5','6','7','8','9'};
    REQUIRE(crc::crc32_oem7(ref) == 0x2DFD2D88u);

    // empty
    REQUIRE(crc::crc32_oem7(Bytes{}) == 0u);

    // single byte 0x00 -> 0
    REQUIRE(crc::crc32_oem7(Bytes{0x00}) == 0u);
}
