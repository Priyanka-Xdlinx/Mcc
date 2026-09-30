// SPDX-License-Identifier: Apache-2.0
#include "framing/gps_framing.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace xdos;
namespace gps = xdos::framing::gps;

TEST_CASE("GPS encode/decode round trip", "[gps][framing]") {
    Bytes body(72, 0xAB);
    auto wire = gps::encode(42 /*BESTPOS*/, body, {2300, 360000});
    // Sync bytes
    REQUIRE(wire[0] == gps::kSync0);
    REQUIRE(wire[1] == gps::kSync1);
    REQUIRE(wire[2] == gps::kSync2);
    REQUIRE(wire[3] == gps::kHeaderLen);

    auto d = gps::decode(wire);
    REQUIRE(d.framed_ok);
    REQUIRE(d.crc_ok);
    REQUIRE(d.msg_id == 42);
    REQUIRE(d.week   == 2300);
    REQUIRE(d.ms     == 360000);
    REQUIRE(d.data   == body);
}

TEST_CASE("GPS bad CRC detected", "[gps][framing]") {
    Bytes body(10, 0x11);
    auto wire = gps::encode(99, body);
    // mangle the last CRC byte
    wire.back() ^= 0xFF;
    auto d = gps::decode(wire);
    REQUIRE(d.framed_ok);
    REQUIRE(!d.crc_ok);
}

TEST_CASE("GPS truncated frame", "[gps][framing]") {
    Bytes wire{gps::kSync0, gps::kSync1, gps::kSync2, gps::kHeaderLen};
    auto d = gps::decode(wire);
    REQUIRE(!d.framed_ok);
}
