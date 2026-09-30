// SPDX-License-Identifier: Apache-2.0
#include "common/hex.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace xdos;

TEST_CASE("hex encode/decode round trip", "[hex]") {
    Bytes ref{0x1F, 0x7F, 0x80, 0xCA, 0xFE, 0x00, 0x01, 0xBA};
    auto s = hex::encode(ref);
    REQUIRE(s == "1f7f80cafe0001ba");
    auto sp = hex::encode_spaced(ref);
    REQUIRE(sp == "1F 7F 80 CA FE 00 01 BA");

    auto d = hex::decode(s);
    REQUIRE(d.has_value());
    REQUIRE(*d == ref);

    auto d2 = hex::decode(sp);
    REQUIRE(d2.has_value());
    REQUIRE(*d2 == ref);
}

TEST_CASE("hex decode rejects malformed input", "[hex]") {
    REQUIRE(!hex::decode("g0").has_value());      // bad nibble
    REQUIRE(!hex::decode("aabbc").has_value());   // odd
}

TEST_CASE("hex decode handles separators and comments", "[hex]") {
    auto d = hex::decode("a5,aa : 80\n86  # tail comment\nfe");
    REQUIRE(d.has_value());
    REQUIRE(*d == Bytes{0xa5, 0xaa, 0x80, 0x86, 0xfe});
}
