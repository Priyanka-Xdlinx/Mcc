// SPDX-License-Identifier: Apache-2.0
// Binary rate-group LUT builder — format lock-down tests.
#include <catch2/catch_test_macros.hpp>

#include "lut/binary_lut_builder.hpp"
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
static uint16_t rd16(const Bytes& b, size_t o) { return b[o] | (b[o+1] << 8); }
static uint32_t rd32(const Bytes& b, size_t o) {
    return b[o] | (b[o+1]<<8) | (b[o+2]<<16) | (uint32_t(b[o+3])<<24);
}

TEST_CASE("CRC-16-CCITT known vector", "[blut]") {
    Bytes v { '1','2','3','4','5','6','7','8','9' };
    REQUIRE(BinaryLutBuilder::crc16_ccitt(v) == 0x29B1);
}

TEST_CASE("Header layout, budgets, and signature", "[blut]") {
    BinaryLutBuilder b(test_mib());
    json src = { {"version",1}, {"tiers", {
        {"VH", json::array({ { {"id","EP-003"}, {"subsystem","EPS"},
                               {"tc","Get_PDU_CH_State"},
                               {"rsp","Get_PDU_CH_State_RSP"},
                               {"picks", json::array()} } })},
        {"H", json::array()}, {"M", json::array()}, {"L", json::array()} } } };
    auto r = b.build(src);
    for (auto& e : r.errors) INFO(e);
    REQUIRE(r.ok());
    const Bytes& f = r.file;
    REQUIRE(f.size() >= kHeaderSize);
    REQUIRE((f[0]=='X' && f[1]=='L' && f[2]=='U' && f[3]=='T'));
    REQUIRE(f[4] == kFormatVersion);
    REQUIRE(rd16(f, 6)  == 1280);
    REQUIRE(rd16(f, 8)  == 400);   // VH
    REQUIRE(rd16(f, 10) == 400);   // H
    REQUIRE(rd16(f, 12) == 300);   // M
    REQUIRE(rd16(f, 14) == 175);   // L
    REQUIRE(f[16] == 5);           // reserved bytes
    REQUIRE(rd16(f, 18) == 1000);  // T_sf = 1000 ms
    REQUIRE(rd16(f, 20) == 1);     // VH entry count
    REQUIRE(rd32(f, 28) == kHeaderSize);           // VH segment offset
    REQUIRE(rd32(f, 44) == f.size());              // file length
    // signature: crc over the file with the crc field zeroed
    Bytes z = f; z[48] = z[49] = 0;
    REQUIRE(rd16(f, 48) == BinaryLutBuilder::crc16_ccitt(z));
    REQUIRE(rd16(f, 48) == r.crc);
    // entry: sync + EPS apid, full-payload pick of the 2-byte response
    size_t e = kHeaderSize;
    REQUIRE(rd16(f, e) == kEntrySync);
    REQUIRE(f[e+2] == 3);                          // EPS hardware APID
    uint16_t tc_len = rd16(f, e+4);
    REQUIRE(tc_len > 0);
    size_t p = e + 6 + tc_len;
    REQUIRE(rd16(f, p) == 2);                      // tm_raw_len (EP-003 = 2 B)
    REQUIRE(f[p+2] == 1);                          // one pick
    REQUIRE(rd16(f, p+3) == 0);                    // src
    REQUIRE(rd16(f, p+5) == 2);                    // size
    REQUIRE(rd16(f, p+7) == 0);                    // block off
}

TEST_CASE("Multiple picks per TM: non-adjacent kept, adjacent coalesced", "[blut]") {
    BinaryLutBuilder b(test_mib());
    auto pmap = b.response_param_map(Subsystem::GPS, "BESTPOS");
    REQUIRE(pmap.size() >= 4);
    // choose params 0 and 2 (non-adjacent -> two picks), then 0 and 1
    // (adjacent -> one coalesced pick)
    json two = { {"id","G"}, {"subsystem","GPS"}, {"tc","LOG"}, {"gps_log_id",42},
                 {"rsp","BESTPOS"},
                 {"picks", json::array({ { {"params",
                     json::array({ pmap[0].name, pmap[2].name })} } })} };
    json src = { {"version",1}, {"tiers", { {"VH", json::array({two})},
                 {"H", json::array()}, {"M", json::array()}, {"L", json::array()} } } };
    auto r = b.build(src);
    for (auto& e : r.errors) INFO(e);
    REQUIRE(r.ok());
    const auto& picks = r.segs[0].entries[0].picks;
    REQUIRE(picks.size() == 2);
    REQUIRE(picks[0].src  == pmap[0].off);
    REQUIRE(picks[0].size == pmap[0].size);
    REQUIRE(picks[1].src  == pmap[2].off);
    REQUIRE(picks[1].size == pmap[2].size);
    REQUIRE(picks[0].block == 0);
    REQUIRE(picks[1].block == picks[0].size);      // packed back-to-back
    REQUIRE(r.segs[0].used == picks[0].size + picks[1].size);

    json adj = two;
    adj["picks"] = json::array({ { {"params",
        json::array({ pmap[0].name, pmap[1].name })} } });
    src["tiers"]["VH"] = json::array({adj});
    auto r2 = b.build(src);
    REQUIRE(r2.ok());
    REQUIRE(r2.segs[0].entries[0].picks.size() == 1);
    REQUIRE(r2.segs[0].entries[0].picks[0].size
            == pmap[0].size + pmap[1].size);
}

TEST_CASE("Budget overflow rejected with the tier named", "[blut]") {
    BinaryLutBuilder b(test_mib());
    // XLINK HK is 120 B raw; four full-payload copies = 480 > VH budget 400
    json xl = { {"id","XL-008"}, {"subsystem","XLINK"}, {"tc","HOUSE_KEEPING"},
                {"rsp","HOUSE_KEEPING_RSP"}, {"picks", json::array()} };
    json src = { {"version",1}, {"tiers", {
        {"VH", json::array({xl, xl, xl, xl})},
        {"H", json::array()}, {"M", json::array()}, {"L", json::array()} } } };
    auto r = b.build(src);
    REQUIRE_FALSE(r.ok());
    bool named = false;
    for (auto& e : r.errors) if (e.find("VH block overflow") != std::string::npos) named = true;
    REQUIRE(named);
}

TEST_CASE("Deterministic: identical source -> byte-identical file", "[blut]") {
    BinaryLutBuilder b(test_mib());
    // Composed inline, mirroring the CLI composer's output — the static
    // input file no longer exists in the workflow.
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
        {"L", json::array()} } } };
    auto r1 = b.build(src);
    auto r2 = b.build(src);
    for (auto& e : r1.errors) INFO(e);
    REQUIRE(r1.ok());
    REQUIRE(r1.file == r2.file);
    REQUIRE(r1.crc == r2.crc);
    // ADCS entry encodes as a plain TM-request frame: 1F 7F <id> 1F FF
    const auto& ad = r1.segs[0].entries[0];
    REQUIRE(ad.tc_bytes == Bytes{0x1F, 0x7F, 0x96, 0x1F, 0xFF});
    REQUIRE(ad.tm_raw_len == 5);
}

// Regression: the composer's resume-for-edit path rebuilds its source table
// straight from the GS twin, and the twin stores the DISPLAY subsystem name.
// to_string(Subsystem::XLINK) is "XLINK-S", so a builder that only accepted
// "XLINK" dropped every XLINK entry on resume ("bad entry: unknown subsystem:
// XLINK-S") and silently rebuilt a short table.
TEST_CASE("Source json accepts every spelling of a subsystem name", "[blut]") {
    auto one = [](const char* spelling) {
        return json{ {"version",1}, {"tiers", {
            {"VH", json::array({ { {"id","XL-008"}, {"subsystem", spelling},
                                   {"tc","HOUSE_KEEPING"},
                                   {"rsp","HOUSE_KEEPING_RSP"},
                                   {"picks", json::array()} } })},
            {"H", json::array()}, {"M", json::array()}, {"L", json::array()} } } };
    };
    BinaryLutBuilder b1(test_mib());
    auto twin_name = b1.build(one("XLINK-S"));     // what the twin writes
    for (auto& e : twin_name.errors) INFO(e);
    REQUIRE(twin_name.ok());

    BinaryLutBuilder b2(test_mib());
    auto csv_name = b2.build(one("XLINK"));        // what the CSV writes
    for (auto& e : csv_name.errors) INFO(e);
    REQUIRE(csv_name.ok());

    // Both spellings name the same subsystem, so they must compile identically.
    REQUIRE(twin_name.file == csv_name.file);

    // A name that is real but has no rate-group encoder still fails, clearly.
    BinaryLutBuilder b3(test_mib());
    auto unsupported = b3.build(one("Payload"));
    REQUIRE_FALSE(unsupported.ok());
}
