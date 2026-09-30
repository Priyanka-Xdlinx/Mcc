// SPDX-License-Identifier: Apache-2.0
// Scaled-parameter convention.
//
// `scale` is a MULTIPLIER applied once, in the codec:
//     engineering = raw * scale        raw = engineering / scale
//
// This file exists because that was not always true.  The codec divided on
// decode while the CLI multiplied, so the two cancelled and every scaled
// reading in the system was displayed as the raw wire count — EPS
// temperatures, ADCS angles, XLink attenuations alike.  Nothing failed,
// because nothing asserted an engineering value.  These tests do.
#include "codec/codec.hpp"
#include "cli/value_format.hpp"
#include "mib/mib.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <filesystem>

using namespace xdos;
using Catch::Approx;

static std::filesystem::path mib_dir() {
    if (std::filesystem::is_directory("mib"))    return "mib";
    if (std::filesystem::is_directory("../mib")) return "../mib";
    return "../../mib";
}
static const mib::Mib& loaded() {
    static mib::Mib m = [] { mib::Mib x; x.load_dir(mib_dir()); return x; }();
    return m;
}
static const mib::Parameter* param(const mib::Message& m, const char* name) {
    for (auto const& p : m.parameters) if (p.name == name) return &p;
    return nullptr;
}

TEST_CASE("scale is stored as the vendor's own multiplier", "[scale][mib]") {
    // Every MIB must agree on the direction, or the codec cannot serve them
    // all.  A value > 1 means somebody stored a divisor again.
    std::size_t scaled = 0;
    for (auto s : loaded().loaded_subsystems())
        for (auto const& m : loaded().messages(s))
            for (auto const& p : m.parameters) {
                if (!p.scale) continue;
                ++scaled;
                INFO(to_string(s) << " / " << m.name << " / " << p.name
                     << " has scale " << *p.scale);
                REQUIRE(*p.scale > 0.0);
                REQUIRE(*p.scale <= 1.0);
            }
    REQUIRE(scaled > 500);      // ~511 today; guards against the check going no-op
}

TEST_CASE("EPS temperature decodes to degrees, not raw counts", "[scale][codec]") {
    // eps.json documents this field as "deg C x 100", so scale is 0.01.
    const auto* tm = loaded().find(Subsystem::EPS, Direction::TM,
                                   "Get_PDU_Temperature_Readings_RSP");
    REQUIRE(tm != nullptr);
    const auto* p = param(*tm, "SES_A Temperature Reading");
    REQUIRE(p != nullptr);
    REQUIRE(p->scale);
    REQUIRE(*p->scale == Approx(0.01));

    Bytes raw(tm->length_bytes.value_or(8), 0);
    raw[0] = 0xE6; raw[1] = 0x09;                    // 2534 little-endian
    auto v = codec::decode(*tm, raw);
    REQUIRE(v["SES_A Temperature Reading"].get<double>() == Approx(25.34));

    // ...and the operator sees the same number, not 2534 and not 253400.
    auto shown = cli::format_value(v["SES_A Temperature Reading"], *p);
    REQUIRE(shown.find("25.34") != std::string::npos);
    REQUIRE(shown.find("2534") == std::string::npos);
}

TEST_CASE("ADCS bit-packed field decodes to engineering units", "[scale][codec]") {
    // adcs.json states "(formatted value) = RAWVAL*0.01" for this field, and
    // it is bit-offset rather than byte-offset — a separate code path.
    const auto* ev = loaded().find(Subsystem::ADCS, Direction::Event,
                                   "New RPY Reference");
    REQUIRE(ev != nullptr);
    const auto* p = param(*ev, "RPY Roll command");
    REQUIRE(p != nullptr);
    REQUIRE(*p->scale == Approx(0.01));

    Bytes raw(ev->length_bytes.value_or(6), 0);
    raw[0] = 0x94; raw[1] = 0x11;                    // 4500 little-endian
    auto v = codec::decode(*ev, raw);
    REQUIRE(v["RPY Roll command"].get<double>() == Approx(45.0));
}

TEST_CASE("encode and decode are inverses for scaled fields", "[scale][codec]") {
    const auto* tc = loaded().find(Subsystem::EPS, Direction::TM,
                                   "Get_PDU_Temperature_Readings_RSP");
    REQUIRE(tc != nullptr);
    const auto* p = param(*tc, "SES_A Temperature Reading");
    REQUIRE(p != nullptr);

    // Engineering value in, same engineering value out.
    nlohmann::json in = {{"SES_A Temperature Reading", -10.25}};
    auto bytes = codec::encode(*tc, in);
    auto back  = codec::decode(*tc, bytes);
    REQUIRE(back["SES_A Temperature Reading"].get<double>() == Approx(-10.25));

    // The wire carries the raw count the spec sheet describes: -1025.
    auto lo = bytes[0], hi = bytes[1];
    auto word = static_cast<std::int16_t>((hi << 8) | lo);
    REQUIRE(word == -1025);
}
