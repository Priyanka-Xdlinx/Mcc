// SPDX-License-Identifier: Apache-2.0
#include "mib/mib.hpp"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>

using namespace xdos;

static std::filesystem::path mib_dir() {
    // tests are launched from build/ with -E or directly; resolve relative to source root
    if (std::filesystem::is_directory("mib")) return "mib";
    if (std::filesystem::is_directory("../mib")) return "../mib";
    return "../../mib";
}

TEST_CASE("Mib loads every subsystem JSON file", "[mib]") {
    mib::Mib m;
    m.load_dir(mib_dir());
    auto c = m.counts();
    REQUIRE(c.tc    == 156);     // 54 ADCS (CP9) + 41 EPS + 3 GPS + 23 XLINK-S + 5 Payload + 25 UHF + 5 SENSOR
    REQUIRE(c.tm    == 213);     // 107 ADCS (CP9) + 41 EPS + 16 GPS + 23 XLINK-S + 5 Payload + 16 UHF + 5 SENSOR
    REQUIRE(c.event == 64);      // 59 ADCS (CP9) + 5 SENSOR NACKs

    REQUIRE(m.subsystem(Subsystem::ADCS) != nullptr);
    REQUIRE(m.subsystem(Subsystem::EPS)  != nullptr);
    REQUIRE(m.subsystem(Subsystem::GPS)  != nullptr);
    REQUIRE(m.subsystem(Subsystem::XLINK)!= nullptr);
    REQUIRE(m.subsystem(Subsystem::Payload) != nullptr);
    REQUIRE(m.subsystem(Subsystem::UHF)  != nullptr);
    REQUIRE(m.subsystem(Subsystem::SENSOR) != nullptr);
}

TEST_CASE("EPS msg-id collision resolved by node lookup", "[mib]") {
    mib::Mib m; m.load_dir(mib_dir());

    // msg_id 2 exists for PDU, BMS and MPPT — must disambiguate via node.
    auto* pdu = m.by_node_wire(Subsystem::EPS, Direction::TC,
                               std::string_view("PDU"), 2);
    auto* bms = m.by_node_wire(Subsystem::EPS, Direction::TC,
                               std::string_view("BMS"), 2);
    auto* mppt = m.by_node_wire(Subsystem::EPS, Direction::TC,
                                std::string_view("MPPT"), 2);

    REQUIRE(pdu  != nullptr);
    REQUIRE(bms  != nullptr);
    REQUIRE(mppt != nullptr);
    REQUIRE(pdu->name  != bms->name);
    REQUIRE(pdu->name  != mppt->name);
    REQUIRE(bms->name  != mppt->name);
}

TEST_CASE("GPS BESTPOS structure", "[mib]") {
    mib::Mib m; m.load_dir(mib_dir());
    auto* bp = m.find(Subsystem::GPS, Direction::TM, "BESTPOS");
    REQUIRE(bp != nullptr);
    REQUIRE(bp->offset_unit == "bytes");
    REQUIRE(bp->length_bytes.value_or(0) == 72);
    REQUIRE(bp->parameters.size() == 21);
}

TEST_CASE("EPS node addresses", "[mib]") {
    mib::Mib m; m.load_dir(mib_dir());
    // Per NL16U design doc and EPS protocol spec:
    //   PDU1=1, BMS1=9, BMS2=10, BMS3=11, MPPT1=12
    //   "PDU"/"BMS"/"MPPT" are back-compat aliases for PDU1/BMS1/MPPT1.
    REQUIRE(m.node_address(Subsystem::EPS, std::string_view("PDU1"))  == 1);
    REQUIRE(m.node_address(Subsystem::EPS, std::string_view("BMS1"))  == 9);
    REQUIRE(m.node_address(Subsystem::EPS, std::string_view("BMS2"))  == 10);
    REQUIRE(m.node_address(Subsystem::EPS, std::string_view("BMS3"))  == 11);
    REQUIRE(m.node_address(Subsystem::EPS, std::string_view("MPPT1")) == 12);

    // Back-compat aliases still resolve to the canonical address
    REQUIRE(m.node_address(Subsystem::EPS, std::string_view("PDU"))  == 1);
    REQUIRE(m.node_address(Subsystem::EPS, std::string_view("BMS"))  == 9);
    REQUIRE(m.node_address(Subsystem::EPS, std::string_view("MPPT")) == 12);

    // Reverse lookup returns the canonical (first-declared) name
    auto pdu  = m.node_by_address(Subsystem::EPS, 1);
    auto bms2 = m.node_by_address(Subsystem::EPS, 10);
    auto mppt = m.node_by_address(Subsystem::EPS, 12);
    REQUIRE(pdu);  REQUIRE(*pdu  == "PDU1");
    REQUIRE(bms2); REQUIRE(*bms2 == "BMS2");
    REQUIRE(mppt); REQUIRE(*mppt == "MPPT1");
}
