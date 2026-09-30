// SPDX-License-Identifier: Apache-2.0
// OBC binary rate-group LUT parser — ground<->OBC round-trip contract.
#include <catch2/catch_test_macros.hpp>

#include "lut/binary_lut_builder.hpp"
#include "../obc/binary_rate_lut.hpp"
#include "mib/mib.hpp"

#include <filesystem>

using namespace xdos;
using namespace xdos::lut;
using nlohmann::json;

static const mib::Mib& test_mib() {
    static mib::Mib m; static bool loaded = false;
    if (!loaded) {
        for (auto p : { "mib", "../mib", "../../mib" })
            if (std::filesystem::is_directory(p)) { m.load_dir(p); break; }
        loaded = true;
    }
    return m;
}

// A small multi-tier table exercising all four subsystems.
static BuildResult build_sample() {
    BinaryLutBuilder b(test_mib());
    json src = { {"version",1}, {"tiers", {
        {"VH", json::array({
            { {"id","AD-150"}, {"subsystem","ADCS"},
              {"tc","Control and estimation mode"},
              {"rsp","Control and estimation mode"}, {"picks", json::array()} },
            { {"id","EP-3"}, {"subsystem","EPS"}, {"tc","Get_PDU_CH_State"},
              {"rsp","Get_PDU_CH_State_RSP"}, {"picks", json::array()} } })},
        {"H", json::array({
            { {"id","GPS-42"}, {"subsystem","GPS"}, {"tc","LOG"},
              {"gps_log_id",42}, {"rsp","BESTPOS"}, {"picks", json::array()} } })},
        {"M", json::array({
            { {"id","XL-8"}, {"subsystem","XLINK"}, {"tc","HOUSE_KEEPING"},
              {"rsp","HOUSE_KEEPING_RSP"}, {"picks", json::array()} } })},
        {"L", json::array({
            { {"id","EP-16"}, {"subsystem","EPS"},
              {"tc","Get_PDU_Voltage_Readings"},
              {"rsp","Get_PDU_Voltage_Readings_RSP"},
              {"picks", json::array({ { {"bytes", json::array({0, 4})} } })} } })}
        } } };
    return b.build(src);
}

TEST_CASE("Binary LUT round-trip: builder image parses back identically", "[binlut]") {
    auto r = build_sample();
    for (auto& e : r.errors) INFO(e);
    REQUIRE(r.ok());

    obc::BinaryRateLut lut;
    std::string err;
    REQUIRE(obc::BinaryRateLut::parse(r.file, lut, &err));
    INFO(err);
    REQUIRE(lut.crc == r.crc);
    REQUIRE(lut.t_sf_ms == 1000);
    REQUIRE(lut.entry_count() == 5);

    for (int t = 0; t < 4; ++t) {
        REQUIRE(lut.tiers[t].size() == r.segs[t].entries.size());
        for (size_t i = 0; i < lut.tiers[t].size(); ++i) {
            const auto& got  = lut.tiers[t][i];
            const auto& want = r.segs[t].entries[i];
            REQUIRE((int)got.apid == want.apid);
            REQUIRE(got.tc_bytes == want.tc_bytes);
            REQUIRE(got.tm_raw_len == want.tm_raw_len);
            REQUIRE(got.picks.size() == want.picks.size());
            for (size_t k = 0; k < got.picks.size(); ++k) {
                REQUIRE(got.picks[k].src   == want.picks[k].src);
                REQUIRE(got.picks[k].size  == want.picks[k].size);
                REQUIRE(got.picks[k].block == want.picks[k].block);
            }
        }
    }
    // subsystem mapping from the hardware APIDs
    REQUIRE(lut.tiers[0][0].sub == Subsystem::ADCS);
    REQUIRE(lut.tiers[0][1].sub == Subsystem::EPS);
    REQUIRE(lut.tiers[1][0].sub == Subsystem::GPS);
    REQUIRE(lut.tiers[2][0].sub == Subsystem::XLINK);
    // the L-tier bytes pick survived verbatim
    REQUIRE(lut.tiers[3][0].picks.size() == 1);
    REQUIRE(lut.tiers[3][0].picks[0].size == 4);
}

TEST_CASE("Binary LUT parser rejects corrupted images", "[binlut]") {
    auto r = build_sample();
    REQUIRE(r.ok());
    obc::BinaryRateLut lut;
    std::string err;

    SECTION("flipped CRC byte") {
        Bytes bad = r.file; bad[48] ^= 0xFF;
        REQUIRE_FALSE(obc::BinaryRateLut::parse(bad, lut, &err));
        REQUIRE(err.find("CRC") != std::string::npos);
    }
    SECTION("flipped payload byte (caught by CRC)") {
        Bytes bad = r.file; bad[60] ^= 0x01;
        REQUIRE_FALSE(obc::BinaryRateLut::parse(bad, lut, &err));
        REQUIRE(err.find("CRC") != std::string::npos);
    }
    SECTION("truncated file") {
        Bytes bad(r.file.begin(), r.file.end() - 3);
        REQUIRE_FALSE(obc::BinaryRateLut::parse(bad, lut, &err));
        REQUIRE(err.find("length") != std::string::npos);
    }
    SECTION("short header") {
        Bytes bad(r.file.begin(), r.file.begin() + 10);
        REQUIRE_FALSE(obc::BinaryRateLut::parse(bad, lut, &err));
    }
    SECTION("bad magic") {
        Bytes bad = r.file; bad[0] = 'Y';
        REQUIRE_FALSE(obc::BinaryRateLut::parse(bad, lut, &err));
        REQUIRE(err.find("magic") != std::string::npos);
    }
    SECTION("bad entry sync (CRC recomputed to isolate the check)") {
        Bytes bad = r.file;
        bad[50] = 0x00;                       // first entry's sync low byte
        bad[48] = bad[49] = 0;
        uint16_t crc = lut::crc16_ccitt(bad.data(), bad.size());
        bad[48] = crc & 0xFF; bad[49] = crc >> 8;
        REQUIRE_FALSE(obc::BinaryRateLut::parse(bad, lut, &err));
        REQUIRE(err.find("sync") != std::string::npos);
    }
    SECTION("legacy t_sf_ms 0 normalizes to 1000") {
        Bytes old = r.file;
        old[18] = old[19] = 0;
        old[48] = old[49] = 0;
        uint16_t crc = lut::crc16_ccitt(old.data(), old.size());
        old[48] = crc & 0xFF; old[49] = crc >> 8;
        REQUIRE(obc::BinaryRateLut::parse(old, lut, &err));
        REQUIRE(lut.t_sf_ms == 1000);
    }
}
