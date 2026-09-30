// SPDX-License-Identifier: Apache-2.0
// CCSDS codec v2 round-trip + boundary tests (per sheet v2).
//
//   SOF(2) + TF(5) + Pkt(6) + GS-ID(1) + Time(8) + AncLen(2)
//         + TC-ID(2) + TC-Len(2) + TC-Data(N) + CRC(2)  =  30 + N bytes

#include "codec/ccsds_codec.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>

using namespace xdos;
using namespace xdos::ccsds;

static std::string profile_path() {
    for (const auto& p : { "mib/ccsds_tc.json",
                           "../mib/ccsds_tc.json",
                           "../../mib/ccsds_tc.json" }) {
        if (std::filesystem::exists(p)) return p;
    }
    throw std::runtime_error("test: cannot find mib/ccsds_tc.json");
}

TEST_CASE("CCSDS v2 profile loads from JSON", "[ccsds][load]") {
    CcsdsCodec codec;
    REQUIRE(codec.load(profile_path()));
    REQUIRE(codec.profile_name() == "xdos_ccsds_tc_v2");
    REQUIRE(codec.spacecraft_id() == 0x271);
    REQUIRE(codec.default_vcid()  == 0);
    REQUIRE(codec.ground_station_id() == 0);
    REQUIRE(codec.enabled_by_default() == true);

    REQUIRE(codec.subsystem_endpoint(Subsystem::ADCS)->port == 10020);
    REQUIRE(codec.subsystem_endpoint(Subsystem::EPS)->port  == 10010);
    REQUIRE(codec.subsystem_endpoint(Subsystem::GPS)->port  == 10030);

    REQUIRE(codec.apid_to_subsystem(0x001) == Subsystem::ADCS);
    REQUIRE(codec.apid_to_subsystem(0x003) == Subsystem::EPS);
    REQUIRE(codec.apid_to_subsystem(0x004) == Subsystem::GPS);
    REQUIRE_FALSE(codec.apid_to_subsystem(0xFFF).has_value());
}

TEST_CASE("CCSDS v2 wire framing — SOF 1A CF, no EOF, expected size", "[ccsds][wire]") {
    CcsdsCodec codec;
    REQUIRE(codec.load(profile_path()));

    // ADCS GET Identification → TC ID = 0x0080, no TC Data
    Bytes wire = codec.encode(Subsystem::ADCS, CcsdsDirection::TC, 0x0080, {});

    REQUIRE(wire.size() == 30);                    // 30 + 0 = 30
    REQUIRE(wire[0] == 0x1A);
    REQUIRE(wire[1] == 0xCF);
    // Last 2 bytes are CRC — no EOF
    REQUIRE(codec.looks_like_ccsds(wire));
}

TEST_CASE("CCSDS v2 wire framing — TC Data adds bytes", "[ccsds][wire]") {
    CcsdsCodec codec;
    REQUIRE(codec.load(profile_path()));

    // 5-byte TC payload
    Bytes tc_data = {0x11, 0x22, 0x33, 0x44, 0x55};
    Bytes wire = codec.encode(Subsystem::ADCS, CcsdsDirection::TC, 0x0080, tc_data);

    REQUIRE(wire.size() == 35);                    // 30 + 5

    auto d = codec.decode(wire);
    REQUIRE(d.has_value());
    REQUIRE(d->crc_ok);
    REQUIRE(d->tc_id == 0x0080);
    REQUIRE(d->tc_len == 5);
    REQUIRE(d->tc_data == tc_data);
}

TEST_CASE("CCSDS v2 round-trip — ADCS, EPS, GPS", "[ccsds][roundtrip]") {
    CcsdsCodec codec;
    REQUIRE(codec.load(profile_path()));

    SECTION("ADCS GET Identification — empty payload") {
        Bytes wire = codec.encode(Subsystem::ADCS, CcsdsDirection::TC, 0x0080, {});
        auto d = codec.decode(wire);
        REQUIRE(d.has_value());
        REQUIRE(d->crc_ok);
        REQUIRE(d->apid == 0x001);
        REQUIRE(d->subsystem == Subsystem::ADCS);
        REQUIRE(d->spacecraft_id == 0x271);
        REQUIRE(d->tc_id == 0x0080);
        REQUIRE(d->tc_len == 0);
        REQUIRE(d->tc_data.empty());
        REQUIRE(d->ground_station_id == 0);
    }

    SECTION("EPS PDU_Get_CH_Enable_Disable — TC ID 3, empty payload") {
        Bytes wire = codec.encode(Subsystem::EPS, CcsdsDirection::TC, 0x0003, {});
        auto d = codec.decode(wire);
        REQUIRE(d.has_value());
        REQUIRE(d->crc_ok);
        REQUIRE(d->apid == 0x003);
        REQUIRE(d->subsystem == Subsystem::EPS);
        REQUIRE(d->tc_id == 0x0003);
    }

    SECTION("GPS LOG BESTPOS — TC ID 42, 4-byte payload") {
        Bytes tc_data = {0x04, 0x00, 0x00, 0x00};        // trigger=ONCE
        Bytes wire = codec.encode(Subsystem::GPS, CcsdsDirection::TC, 42, tc_data);
        auto d = codec.decode(wire);
        REQUIRE(d.has_value());
        REQUIRE(d->crc_ok);
        REQUIRE(d->apid == 0x004);
        REQUIRE(d->subsystem == Subsystem::GPS);
        REQUIRE(d->tc_id == 42);
        REQUIRE(d->tc_len == 4);
        REQUIRE(d->tc_data == tc_data);
    }
}

TEST_CASE("CCSDS v2 direction bit flips TC vs TM", "[ccsds][direction]") {
    CcsdsCodec codec;
    REQUIRE(codec.load(profile_path()));

    Bytes tc = codec.encode(Subsystem::ADCS, CcsdsDirection::TC, 0x0080, {});
    Bytes tm = codec.encode(Subsystem::ADCS, CcsdsDirection::TM, 0x0080, {0xAA, 0xBB});

    auto dtc = codec.decode(tc);
    auto dtm = codec.decode(tm);
    REQUIRE(dtc.has_value());
    REQUIRE(dtm.has_value());
    REQUIRE(dtc->direction == CcsdsDirection::TC);
    REQUIRE(dtm->direction == CcsdsDirection::TM);
    REQUIRE(dtm->tc_data.size() == 2);
}

TEST_CASE("CCSDS v2 counters advance per encode call", "[ccsds][counters]") {
    CcsdsCodec codec;
    REQUIRE(codec.load(profile_path()));
    codec.reset_counters();

    auto seqs = [&](Subsystem s) {
        Bytes w = codec.encode(s, CcsdsDirection::TC, 1, {});
        auto d = codec.decode(w);
        REQUIRE(d.has_value());
        return std::make_pair(d->frame_sequence_num, d->packet_sequence_cnt);
    };

    auto [f0, p0] = seqs(Subsystem::ADCS);
    auto [f1, p1] = seqs(Subsystem::ADCS);
    auto [f2, p2] = seqs(Subsystem::EPS);
    auto [f3, p3] = seqs(Subsystem::ADCS);

    REQUIRE(f0 == 0); REQUIRE(f1 == 1); REQUIRE(f2 == 2); REQUIRE(f3 == 3);
    REQUIRE(p0 == 0); REQUIRE(p1 == 1); REQUIRE(p2 == 0); REQUIRE(p3 == 2);
}

TEST_CASE("CCSDS v2 CRC catches single-bit corruption", "[ccsds][crc]") {
    CcsdsCodec codec;
    REQUIRE(codec.load(profile_path()));
    Bytes wire = codec.encode(Subsystem::ADCS, CcsdsDirection::TC, 0x0080, {});

    auto good = codec.decode(wire);
    REQUIRE(good.has_value());
    REQUIRE(good->crc_ok);

    Bytes corrupted = wire;
    corrupted[15] ^= 0x01;
    auto bad = codec.decode(corrupted);
    REQUIRE(bad.has_value());
    REQUIRE_FALSE(bad->crc_ok);
}

TEST_CASE("CCSDS v2 decode handles malformed input safely", "[ccsds][malformed]") {
    CcsdsCodec codec;
    REQUIRE(codec.load(profile_path()));

    REQUIRE_FALSE(codec.decode({}).has_value());
    REQUIRE_FALSE(codec.decode({0x1A, 0xCF, 0x00}).has_value());

    Bytes bad_sof(40, Byte{0});
    bad_sof[0] = 0xDE; bad_sof[1] = 0xAD;
    REQUIRE_FALSE(codec.decode(bad_sof).has_value());

    // Native frame (no SOF) is not mistaken for CCSDS
    Bytes native = {0x1F, 0x7F, 0x80, 0x1F, 0xFF};
    REQUIRE_FALSE(codec.looks_like_ccsds(native));
    REQUIRE_FALSE(codec.decode(native).has_value());
}

TEST_CASE("CCSDS v2 summary() includes TC ID and SCID", "[ccsds][summary]") {
    CcsdsCodec codec;
    REQUIRE(codec.load(profile_path()));
    Bytes wire = codec.encode(Subsystem::ADCS, CcsdsDirection::TC, 0x0080, {});
    auto d = codec.decode(wire);
    REQUIRE(d.has_value());
    auto s = d->summary();
    REQUIRE(s.find("[TC]")    != std::string::npos);
    REQUIRE(s.find("0x271")   != std::string::npos);    // SCID
    REQUIRE(s.find("0x001")   != std::string::npos);    // APID
    REQUIRE(s.find("0x0080")  != std::string::npos);    // TC ID
    REQUIRE(s.find("CRC=ok")  != std::string::npos);
}
