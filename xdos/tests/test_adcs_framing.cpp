// SPDX-License-Identifier: Apache-2.0
#include "framing/adcs_framing.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace xdos;
namespace adcs = xdos::framing::adcs;

TEST_CASE("ADCS encode/decode basic", "[adcs][framing]") {
    Bytes data{0x01, 0x02, 0x03};
    auto wire = adcs::encode(0x55, data);
    REQUIRE(wire.front() == adcs::kEsc);
    REQUIRE(wire[1]       == adcs::kSom);
    REQUIRE(wire[wire.size()-2] == adcs::kEsc);
    REQUIRE(wire.back()         == adcs::kEom);

    auto d = adcs::decode(wire);
    REQUIRE(d.framed_ok);
    REQUIRE(d.msg_id == 0x55);
    REQUIRE(d.data   == data);
}

TEST_CASE("ADCS escapes 0x1F in payload", "[adcs][framing]") {
    Bytes data{0x1F, 0xAA, 0x1F};        // two 0x1F bytes to escape
    auto wire = adcs::encode(0x10, data);

    // Payload portion of wire (between SOM and trailer) must have each
    // 0x1F doubled, the trailer must be 0x1F 0xFF and not appear earlier.
    int trailers = 0;
    for (std::size_t i = 2; i + 1 < wire.size(); ++i) {
        if (wire[i] == adcs::kEsc && wire[i+1] == adcs::kEom) ++trailers;
    }
    REQUIRE(trailers == 1);

    auto d = adcs::decode(wire);
    REQUIRE(d.framed_ok);
    REQUIRE(d.msg_id == 0x10);
    REQUIRE(d.data   == data);
}

TEST_CASE("ADCS msg_id == 0x1F is also escaped", "[adcs][framing]") {
    Bytes data{0xAA};
    auto wire = adcs::encode(adcs::kEsc, data);
    auto d = adcs::decode(wire);
    REQUIRE(d.framed_ok);
    REQUIRE(d.msg_id == adcs::kEsc);
    REQUIRE(d.data   == data);
}

TEST_CASE("ADCS decode rejects bad SOM", "[adcs][framing]") {
    Bytes bad{0xAA, 0xBB, 0xCC};
    auto d = adcs::decode(bad);
    REQUIRE(!d.framed_ok);
}

TEST_CASE("ADCS raw_body is unescaped and undelimited", "[adcs][framing]") {
    // The flight OBC builds the CubeSpace frame itself — delimiters and the
    // 0x1F doubling both — so the ground hands over the bytes untouched.
    Bytes data{0x01, 0x1F, 0x03};
    auto body = adcs::raw_body(0x55, data);
    REQUIRE(body == Bytes{0x55, 0x01, 0x1F, 0x03});   // 0x1F NOT doubled

    auto d = adcs::split_raw_body(body);
    REQUIRE(d.framed_ok);
    REQUIRE(d.msg_id == 0x55);
    REQUIRE(d.data   == data);

    // The framed form of the same message still escapes, and is longer.
    REQUIRE(adcs::encode(0x55, data) ==
            Bytes{0x1F, 0x7F, 0x55, 0x01, 0x1F, 0x1F, 0x03, 0x1F, 0xFF});
}

TEST_CASE("ADCS TM poll body is a single id byte", "[adcs][framing]") {
    // Rate-group polls carry only a TM id and no data.
    REQUIRE(adcs::raw_body(0x96, {}) == Bytes{0x96});
    REQUIRE(adcs::encode(0x96, {}) == Bytes{0x1F, 0x7F, 0x96, 0x1F, 0xFF});
}

TEST_CASE("ADCS bare body is never mistaken for a frame", "[adcs][framing]") {
    // The RX sniff is "starts with 1F 7F".  No ADCS message id is 0x1F, so a
    // bare body can never open with the SOM pair.
    for (int id = 128; id <= 248; ++id) {
        auto body = adcs::raw_body(static_cast<Byte>(id), Bytes{0x7F, 0x00});
        REQUIRE_FALSE(body[0] == adcs::kEsc);
    }
    REQUIRE(adcs::split_raw_body({}).framed_ok == false);
}
