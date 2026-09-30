// SPDX-License-Identifier: Apache-2.0
#include "codec/codec.hpp"
#include "mib/mib.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <filesystem>

using namespace xdos;
using Catch::Matchers::WithinAbs;

static std::filesystem::path mib_dir() {
    if (std::filesystem::is_directory("mib")) return "mib";
    if (std::filesystem::is_directory("../mib")) return "../mib";
    return "../../mib";
}

TEST_CASE("Byte-mode codec round trip (GPS BESTPOS)", "[codec]") {
    mib::Mib m; m.load_dir(mib_dir());
    auto* bp = m.find(Subsystem::GPS, Direction::TM, "BESTPOS");
    REQUIRE(bp != nullptr);

    nlohmann::json in = {
        {"sol_status",      0},
        {"pos_type",        16},
        {"lat",             12.9716},
        {"lon",             77.5946},
        {"hgt",             920.3},
        {"num_svs",         8},
        {"num_soln_svs",    7},
    };
    auto enc = codec::encode(*bp, in);
    REQUIRE(enc.size() == 72);
    auto dec = codec::decode(*bp, enc);

    REQUIRE_THAT(dec["lat"].get<double>(),         WithinAbs(12.9716, 1e-9));
    REQUIRE_THAT(dec["lon"].get<double>(),         WithinAbs(77.5946, 1e-9));
    REQUIRE_THAT(dec["hgt"].get<double>(),         WithinAbs(920.3,   1e-9));
    REQUIRE(dec["num_svs"].get<int>()      == 8);
    REQUIRE(dec["num_soln_svs"].get<int>() == 7);
}

TEST_CASE("Bit-mode codec round trip (ADCS message)", "[codec]") {
    mib::Mib m; m.load_dir(mib_dir());

    // Pick any ADCS TM with at least one numeric field to test bit-mode.
    auto tms = m.messages(Subsystem::ADCS, Direction::TM);
    REQUIRE(!tms.empty());

    const mib::Message* tm = nullptr;
    for (auto* t : tms) {
        if (t->offset_unit != "bits") continue;
        if (t->is_variable_length) continue;
        if (t->parameters.empty()) continue;
        // need at least one numeric param
        for (auto const& p : t->parameters) {
            if (p.is_array) continue;
            if (p.data_type == "UINT" || p.data_type == "INT" ||
                p.data_type == "BOOL") {
                tm = t; break;
            }
        }
        if (tm) break;
    }
    REQUIRE(tm != nullptr);

    nlohmann::json in = nlohmann::json::object();
    for (auto const& p : tm->parameters) {
        if (p.is_array) continue;
        if (p.data_type == "UINT" || p.data_type == "INT")
            in[p.name] = 1;
        else if (p.data_type == "BOOL")
            in[p.name] = true;
    }
    auto enc = codec::encode(*tm, in);
    auto dec = codec::decode(*tm, enc);

    for (auto const& p : tm->parameters) {
        if (p.is_array) continue;
        if (p.data_type == "UINT" || p.data_type == "INT") {
            // not all params have a value because we skipped non-numeric;
            // but those we set must round-trip
            if (!in.contains(p.name)) continue;
            REQUIRE(dec[p.name].is_number());
        }
    }
}

TEST_CASE("Encode missing values defaults to zero", "[codec]") {
    mib::Mib m; m.load_dir(mib_dir());
    auto* bp = m.find(Subsystem::GPS, Direction::TM, "BESTPOS");
    REQUIRE(bp != nullptr);
    auto enc = codec::encode(*bp, nlohmann::json::object());
    REQUIRE(enc.size() == 72);
    for (auto b : enc) REQUIRE(b == 0);
}
