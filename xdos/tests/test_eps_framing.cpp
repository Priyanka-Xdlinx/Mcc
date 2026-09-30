// SPDX-License-Identifier: Apache-2.0
#include "framing/eps_framing.hpp"
#include "common/hex.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace xdos;
namespace eps = xdos::framing::eps;

TEST_CASE("Base64 round trip", "[eps][base64]") {
    Bytes a{0x01, 0xC9, 0x06, 0x00, 0xCB};
    auto enc = eps::base64_encode(a);
    auto dec = eps::base64_decode(enc);
    REQUIRE(dec.has_value());
    REQUIRE(*dec == a);
}

TEST_CASE("Base64 known vectors", "[eps][base64]") {
    // Canonical test vector from RFC 4648
    Bytes m{'M','a','n'};
    REQUIRE(eps::base64_encode(m) == "TWFu");

    Bytes p{0x14, 0xfb, 0x9c, 0x03, 0xd9, 0x7e};
    REQUIRE(eps::base64_encode(p) == "FPucA9l+");
}

TEST_CASE("EPS frame encode -> decode round trip (PDU current readings)",
          "[eps][framing]") {
    // dest = 1 (PDU1), msg_id = 6 (Get_CH_Current_Readings), empty body
    auto wire = eps::encode(1, 6, Bytes{});
    // Must be wrapped 0x00 ... 0x01
    REQUIRE(wire.front() == eps::kStartFrame);
    REQUIRE(wire.back()  == eps::kEndFrame);

    auto d = eps::decode(wire);
    REQUIRE(d.framed_ok);
    REQUIRE(d.crc_ok);
    REQUIRE(d.dest_addr == 1);
    REQUIRE(d.src_addr  == eps::kObc1Addr);
    REQUIRE(d.msg_id    == 6);
    REQUIRE(d.data.empty());
}

TEST_CASE("EPS frame with payload", "[eps][framing]") {
    Bytes payload{0x10, 0x20, 0x30, 0x40};
    auto  wire = eps::encode(9 /*BMS1*/, 8 /*BMS_Set_RTC_Time*/, payload);
    auto  d    = eps::decode(wire);
    REQUIRE(d.framed_ok);
    REQUIRE(d.crc_ok);
    REQUIRE(d.dest_addr == 9);
    REQUIRE(d.msg_id    == 8);
    REQUIRE(d.data      == payload);
}

TEST_CASE("EPS bad CRC is detected", "[eps][framing]") {
    Bytes payload{0xAA};
    auto wire = eps::encode(12 /*MPPT1*/, 4, payload);
    // Decode the unwrapped Base64 array, flip the CRC, re-encode
    Bytes inner(wire.begin() + 1, wire.end() - 1);
    auto arr = eps::base64_decode(std::string(inner.begin(), inner.end()));
    REQUIRE(arr.has_value());
    arr->back() ^= 0xFF;                                  // mangle CRC
    Bytes b64v{};
    for (char c : eps::base64_encode(*arr)) b64v.push_back(static_cast<Byte>(c));
    Bytes broken; broken.reserve(b64v.size() + 2);
    broken.push_back(eps::kStartFrame);
    broken.insert(broken.end(), b64v.begin(), b64v.end());
    broken.push_back(eps::kEndFrame);

    auto d = eps::decode(broken);
    REQUIRE(d.framed_ok);
    REQUIRE(!d.crc_ok);
}
