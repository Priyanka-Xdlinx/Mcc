// SPDX-License-Identifier: Apache-2.0
// Flight-realism regression tests added per NL16U spec review:
//   - EPS TM frames now have dest=OBC1 (201), src=device address
//   - All NL16U PDU/BMS1/BMS2/BMS3/MPPT1 nodes resolve in the MIB
//   - Channel map (from NL Spacehawk Power Mapping Sheet) loaded from MIB

#include "framing/eps_framing.hpp"
#include "mib/mib.hpp"
#include "responders/responder.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>

using namespace xdos;

static std::filesystem::path mib_dir() {
    for (auto p : { "mib", "../mib", "../../mib" })
        if (std::filesystem::is_directory(p)) return p;
    return "mib";
}

TEST_CASE("EPS TM frame has correct dest/src per NL spec", "[eps][flight_realism]") {
    mib::Mib m; m.load_dir(mib_dir());

    // sim::wrap() is used by the EPS sim to build TM replies.  A TM from
    // PDU1 to the OBC must have dest=201 (OBC1), src=1 (PDU1).
    Bytes tm_body = {0xAA, 0xBB};
    Bytes wire = sim::wrap(Subsystem::EPS, /*wire_id*/2, tm_body, m,
                           std::string_view("PDU1"));

    auto d = framing::eps::decode(wire);
    REQUIRE(d.framed_ok);
    REQUIRE(d.crc_ok);
    REQUIRE(d.dest_addr == framing::eps::kObc1Addr);   // 201
    REQUIRE(d.src_addr  == 1);                          // PDU1
    REQUIRE(d.msg_id    == 2);
    REQUIRE(d.data      == tm_body);
}

TEST_CASE("EPS TM frame from BMS3 has src=11", "[eps][flight_realism]") {
    mib::Mib m; m.load_dir(mib_dir());
    Bytes wire = sim::wrap(Subsystem::EPS, /*wire_id*/14, Bytes{0x01, 0x02},
                           m, std::string_view("BMS3"));
    auto d = framing::eps::decode(wire);
    REQUIRE(d.dest_addr == framing::eps::kObc1Addr);   // 201
    REQUIRE(d.src_addr  == 11);                         // BMS3
}

TEST_CASE("EPS channel map loaded from MIB (NL Spacehawk 16U)", "[eps][channels]") {
    mib::Mib m; m.load_dir(mib_dir());
    const auto& chans = m.channels(Subsystem::EPS);
    REQUIRE(chans.size() == 16);     // NL16U: 14 wired + 2 reserved

    // Spot-check channels per NL Spacehawk Power Mapping Sheet
    auto* ch1 = m.channel(Subsystem::EPS, 1);
    REQUIRE(ch1 != nullptr);
    REQUIRE(ch1->subsystem == "OBC-A");
    REQUIRE(ch1->wired());

    auto* ch4 = m.channel(Subsystem::EPS, 4);
    REQUIRE(ch4 != nullptr);
    REQUIRE(ch4->subsystem == "ADCS");

    auto* ch10 = m.channel(Subsystem::EPS, 10);
    REQUIRE(ch10 != nullptr);
    REQUIRE(ch10->subsystem == "MSI_Payload");
    REQUIRE(ch10->voltage.has_value());
    REQUIRE(*ch10->voltage == "12V");

    auto* ch14 = m.channel(Subsystem::EPS, 14);
    REQUIRE(ch14 != nullptr);
    REQUIRE(ch14->subsystem == "AIS_BurnWire");

    // CH15 and CH16 are reserved
    auto* ch15 = m.channel(Subsystem::EPS, 15);
    REQUIRE(ch15 != nullptr);
    REQUIRE_FALSE(ch15->wired());
    REQUIRE(ch15->subsystem == "RESERVED");

    auto* ch16 = m.channel(Subsystem::EPS, 16);
    REQUIRE(ch16 != nullptr);
    REQUIRE_FALSE(ch16->wired());

    // Out-of-range
    REQUIRE(m.channel(Subsystem::EPS, 17) == nullptr);
    REQUIRE(m.channel(Subsystem::EPS, 100) == nullptr);
}

TEST_CASE("Non-EPS subsystems have empty channel list", "[channels]") {
    mib::Mib m; m.load_dir(mib_dir());
    REQUIRE(m.channels(Subsystem::ADCS).empty());
    REQUIRE(m.channels(Subsystem::GPS).empty());
    REQUIRE(m.channels(Subsystem::Payload).empty());
}
