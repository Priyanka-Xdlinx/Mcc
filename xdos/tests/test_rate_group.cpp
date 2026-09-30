// SPDX-License-Identifier: Apache-2.0
// Rate-group engine — binary-LUT execution: the 16-position tier schedule,
// subframe assembly from the block buffers, and hot-swap validation.
#include <catch2/catch_test_macros.hpp>

#include "obc/binary_rate_lut.hpp"
#include "obc/rate_group_engine.hpp"
#include "codec/ccsds_tm_codec.hpp"
#include "cli/master_frame_store.hpp"
#include "lut/binary_lut_builder.hpp"
#include "mib/mib.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace xdos;
using namespace xdos::lut;

static std::string mib_dir() {
    for (auto p : { "mib", "../mib", "../../mib" })
        if (std::filesystem::is_directory(p)) return p;
    return "mib";
}

// In-memory LUT: nVH/nH/nM/nL entries, each with one full-block-width pick.
static obc::BinaryRateLut make_lut(int nvh, int nh, int nm, int nl) {
    obc::BinaryRateLut lut;
    const int counts[4] = { nvh, nh, nm, nl };
    for (int t = 0; t < 4; ++t)
        for (int i = 0; i < counts[t]; ++i) {
            obc::BinEntry e;
            e.sub = Subsystem::EPS; e.apid = 3;
            e.tc_bytes = { 0x00, 0x41, 0x01 };
            e.tm_raw_len = 4;
            e.picks.push_back({ 0, 4, (std::uint16_t)(i * 4) });
            lut.tiers[t].push_back(std::move(e));
        }
    return lut;
}

TEST_CASE("schedule: per-entry cadence matches the block span", "[rategroup][schedule]") {
    auto lut = make_lut(3, 4, 5, 2);
    auto sched = obc::RateGroupEngine::build_schedule(lut);

    // occurrences per master frame: 16/span for every entry
    int occ[4][8] = {};
    std::size_t total = 0;
    for (int k = 0; k < 16; ++k) {
        total += sched[k].size();
        for (const auto& t : sched[k]) {
            REQUIRE(t.entry < (int)lut.tiers[t.tier].size());
            ++occ[t.tier][t.entry];
            // an entry only appears in its residue slots
            REQUIRE(k % kBlockSpan[t.tier] == t.entry % kBlockSpan[t.tier]);
        }
    }
    for (int t = 0; t < 4; ++t)
        for (int i = 0; i < (int)lut.tiers[t].size(); ++i)
            REQUIRE(occ[t][i] == 16 / kBlockSpan[t]);
    // total per MF = 16*nVH + 8*nH + 4*nM + 2*nL
    REQUIRE(total == 16u*3 + 8u*4 + 4u*5 + 2u*2);
    // per-slot load is balanced: nVH + ceil-ish share of the others
    for (int k = 0; k < 16; ++k) {
        REQUIRE(sched[k].size() >= 3u + 4u/2 + 5u/4 + 2u/8);
        REQUIRE(sched[k].size() <= 3u + (4u+1)/2 + (5u+3)/4 + (2u+7)/8);
    }
}

TEST_CASE("pack_subframe: fixed partition, slice by slot, reserved zeroed", "[rategroup][subframe]") {
    std::array<Bytes,4> blocks;
    for (int t = 0; t < 4; ++t) {
        blocks[t] = Bytes(kBlockBudget[t]);
        // every block byte = its slice index within the block span
        for (int j = 0; j < kBlockBudget[t]; ++j)
            blocks[t][j] = (Byte)(0x10 * (t + 1) + j / kSegBytes[t]);
    }
    for (int k = 0; k < 16; ++k) {
        Bytes d = obc::RateGroupEngine::pack_subframe(blocks, k);
        REQUIRE(d.size() == (std::size_t)kSubframeSize);
        // each tier segment carries the slice selected by k % span
        REQUIRE(d[0]    == (Byte)(0x10 + 0));                 // VH: always slice 0
        REQUIRE(d[400]  == (Byte)(0x20 + k % 2));             // H
        REQUIRE(d[800]  == (Byte)(0x30 + k % 4));             // M
        REQUIRE(d[1100] == (Byte)(0x40 + k % 8));             // L
        REQUIRE(d[399]  == (Byte)(0x10 + 0));
        REQUIRE(d[1099] == (Byte)(0x30 + k % 4));
        REQUIRE(d[1274] == (Byte)(0x40 + k % 8));
        for (int i = 1275; i < 1280; ++i) REQUIRE(d[i] == 0x00);  // reserved
    }
}

TEST_CASE("swap_lut: validation and down-marker initialisation", "[rategroup]") {
    ccsds::CcsdsTmCodec tm;
    REQUIRE(tm.load(mib_dir() + "/ccsds_tm.json"));
    obc::RateGroupEngine eng(tm, {}, nullptr);

    std::string err;
    SECTION("empty LUT rejected; engine stays idle") {
        obc::BinaryRateLut empty;
        REQUIRE_FALSE(eng.swap_lut(empty, &err));
        REQUIRE_FALSE(eng.has_lut());
    }
    SECTION("valid LUT accepted; counters exposed") {
        auto lut = make_lut(2, 1, 1, 1);
        lut.crc = 0xBEEF;
        REQUIRE(eng.swap_lut(lut, &err));
        REQUIRE(eng.has_lut());
        REQUIRE(eng.command_count() == 5);
        REQUIRE(eng.payload_bytes() == 5 * 4);
        REQUIRE(eng.lut_signature() == 0xBEEF);
        REQUIRE(eng.t_sf_ms() == 1000);
        REQUIRE(eng.max_polls_per_window() >= 2);
    }
    SECTION("pick outside the tier budget rejected") {
        auto lut = make_lut(1, 0, 0, 0);
        lut.tiers[0][0].picks[0].block = 398;   // 398+4 > 400
        REQUIRE_FALSE(eng.swap_lut(lut, &err));
        REQUIRE(err.find("budget") != std::string::npos);
    }
}

TEST_CASE("builder image executes: parse -> swap_lut round trip", "[rategroup]") {
    // The shipped binary artifact (built from config/rate_group_commands.csv)
    // must be executable by the engine as-is.
    std::string path;
    for (auto p : { "config/rate_group_lut.bin", "../config/rate_group_lut.bin",
                    "../../config/rate_group_lut.bin" })
        if (std::filesystem::exists(p)) { path = p; break; }
    REQUIRE_FALSE(path.empty());

    obc::BinaryRateLut lut;
    std::string err;
    REQUIRE(obc::BinaryRateLut::load_file(path, lut, &err));
    INFO(err);

    ccsds::CcsdsTmCodec tm;
    REQUIRE(tm.load(mib_dir() + "/ccsds_tm.json"));
    obc::RateGroupEngine eng(tm, {}, nullptr);
    REQUIRE(eng.swap_lut(lut, &err));
    REQUIRE(eng.command_count() == lut.entry_count());
    // Pacing is T_sf / polls-in-the-window, so a window fits by construction
    // however dense the table is — there is no per-poll constant left to
    // overrun.  What remains worth asserting is that the schedule is still
    // resolvable at millisecond granularity: at most one poll per ms of T_sf.
    REQUIRE(eng.max_polls_per_window() >= 1);
    REQUIRE(eng.max_polls_per_window() <= eng.t_sf_ms());
}

// ─── GS side: MasterFrameStore tiered decode ────────────────────────────────

static const mib::Mib& shared_mib() {
    static mib::Mib m;
    static bool loaded = (m.load_dir(mib_dir()), true);
    (void)loaded;
    return m;
}

// A small 4-tier table with knowable byte positions, built with the real
// builder so the twin JSON is authentic.
static xdos::lut::BuildResult build_gs_sample() {
    using nlohmann::json;
    xdos::lut::BinaryLutBuilder b(shared_mib());
    json src = { {"version",1}, {"tiers", {
        {"VH", json::array({
            { {"id","EP-3"}, {"subsystem","EPS"}, {"tc","Get_PDU_CH_State"},
              {"rsp","Get_PDU_CH_State_RSP"}, {"picks", json::array()} } })},
        {"H", json::array({
            { {"id","GPS-42"}, {"subsystem","GPS"}, {"tc","LOG"},
              {"gps_log_id",42}, {"rsp","BESTPOS"}, {"picks", json::array()} } })},
        {"M", json::array({
            { {"id","EP-16"}, {"subsystem","EPS"},
              {"tc","Get_PDU_Voltage_Readings"},
              {"rsp","Get_PDU_Voltage_Readings_RSP"},
              {"picks", json::array({ { {"params",
                  json::array({"Batt Voltage Reading"})} } })} } })},
        {"L", json::array({
            { {"id","XL-8"}, {"subsystem","XLINK"}, {"tc","HOUSE_KEEPING"},
              {"rsp","HOUSE_KEEPING_RSP"}, {"picks", json::array()} } })}
        } } };
    return b.build(src);
}

// Per-tier block buffers with recognisable content:
//   VH: EP-3   = {0xAB, 0xCD}                 (2 B, block 0)
//   H : GPS-42 = i & 0x7F pattern             (72 B, block 0)
//   M : EP-16  = {4,5,6,7}                    (4 B picked from src 16)
//   L : XL-8   = all 0xFF                     (down marker)
static std::array<Bytes,4> gs_sample_blocks() {
    std::array<Bytes,4> blocks;
    for (int t = 0; t < 4; ++t) blocks[t] = Bytes(kBlockBudget[t], 0x00);
    blocks[0][0] = 0xAB; blocks[0][1] = 0xCD;
    for (int i = 0; i < 72; ++i) blocks[1][i] = (Byte)(i & 0x7F);
    for (int i = 0; i < 4; ++i)  blocks[2][i] = (Byte)(4 + i);
    for (int i = 0; i < 120; ++i) blocks[3][i] = 0xFF;
    return blocks;
}

TEST_CASE("MasterFrameStore: slot positioning, tier decode, DOWN, PARTIAL",
          "[rategroup][masterframe]") {
    auto r = build_gs_sample();
    for (auto& e : r.errors) INFO(e);
    REQUIRE(r.ok());
    const std::string twin_path = "._test_twin.json";
    { std::ofstream f(twin_path); f << r.gs_twin.dump(1) << "\n"; }

    ccsds::CcsdsTmCodec tm;
    REQUIRE(tm.load(mib_dir() + "/ccsds_tm.json"));
    cli::MasterFrameStore store(shared_mib(), tm);
    std::string err;
    REQUIRE(store.init(twin_path, &err));
    REQUIRE(store.has_twin());
    REQUIRE(store.twin().crc16 == r.crc);

    auto blocks = gs_sample_blocks();
    // 17 pushes; frame for slot 2 is encoded (SFC advances) but LOST in
    // transit.  17th push starts master frame #2.
    for (int k = 0; k < 17; ++k) {
        Bytes data  = obc::RateGroupEngine::pack_subframe(blocks, k % 16);
        Bytes frame = tm.encode_apid(tm.rate_group_apid(), data, Bytes{}, 1);
        if (k != 2) store.ingest(frame);
    }
    std::remove(twin_path.c_str());

    auto masters = store.master_frames();
    REQUIRE(masters.size() == 2);              // grouped by SFC/16
    const auto& mf = masters[0];
    REQUIRE(mf.mf_id == 0);
    REQUIRE(mf.filled() == 15);                // the dropped frame is a GAP
    REQUIRE_FALSE(mf.by_slot[2].has_value());
    REQUIRE(mf.by_slot[3]->slot == 3);         // positioned by SFC%16, not arrival
    REQUIRE(masters[1].mf_id == 1);
    REQUIRE(masters[1].filled() == 1);
    REQUIRE(masters[1].by_slot[0].has_value());

    // Slot 0: every tier's block is complete except none — but H block for
    // slots 0-1 needs slices 0,1 (both present) etc.  Check entry views.
    auto v0 = store.decode_entries(mf, 0);
    REQUIRE(v0.size() == 4);
    REQUIRE(v0[0].entry->id == "EP-3");
    REQUIRE(v0[0].status == cli::EntryView::UP);
    REQUIRE(v0[0].raw == Bytes{0xAB, 0xCD});
    REQUIRE(v0[1].entry->id == "GPS-42");
    REQUIRE(v0[1].status == cli::EntryView::UP);
    REQUIRE(v0[1].raw.size() == 72);
    REQUIRE(v0[1].raw[71] == (Byte)(71 & 0x7F));
    REQUIRE(v0[2].entry->id == "EP-16");
    REQUIRE(v0[2].status == cli::EntryView::UP);
    // sparse raw: picked bytes land at their src offset (16..19)
    REQUIRE(v0[2].raw.size() == 20);
    REQUIRE(v0[2].raw[16] == 4);
    REQUIRE(v0[2].raw[19] == 7);
    REQUIRE(v0[2].raw[0] == 0x00);             // unpicked bytes stay zero
    REQUIRE(v0[2].pick_bytes == Bytes{4, 5, 6, 7});
    REQUIRE(v0[3].entry->id == "XL-8");
    REQUIRE(v0[3].status == cli::EntryView::DOWN);   // all pick bytes 0xFF

    // Slot 2 is missing: every tier whose block needs that slice is PARTIAL.
    auto v2 = store.decode_entries(mf, 2);
    REQUIRE(v2[0].status == cli::EntryView::PARTIAL);   // VH lives IN slot 2
    REQUIRE(v2[1].status == cli::EntryView::PARTIAL);   // H block = slots 2,3
    // M block (slots 0-3) misses slice 2 — but EP-16's pick lives in slice 0,
    // which is present, so the entry itself is fine.
    REQUIRE(v2[2].status == cli::EntryView::UP);
    // L block (slots 0-7) misses slice 2; XL-8's 120 picked bytes live in
    // slice 0 (present) — all 0xFF, so it stays DOWN, not PARTIAL.
    REQUIRE(v2[3].status == cli::EntryView::DOWN);

    // Subframe summary counts got filled at ingest.
    REQUIRE(mf.by_slot[0]->entry_total == 4);
    REQUIRE(mf.by_slot[0]->up_count >= 2);
}

TEST_CASE("MasterFrameStore: no twin -> raw-hex mode still collects frames",
          "[rategroup][masterframe]") {
    ccsds::CcsdsTmCodec tm;
    REQUIRE(tm.load(mib_dir() + "/ccsds_tm.json"));
    cli::MasterFrameStore store(shared_mib(), tm);
    std::string err;
    REQUIRE(store.init("._no_such_twin.json", &err) == false);
    REQUIRE_FALSE(store.has_twin());

    Bytes data(1280, 0x5A);
    store.ingest(tm.encode_apid(tm.rate_group_apid(), data, Bytes{}, 1));
    REQUIRE(store.frames_received() == 1);
    auto masters = store.master_frames();
    REQUIRE(masters.size() == 1);
    REQUIRE(masters[0].by_slot[0].has_value());
    REQUIRE(masters[0].by_slot[0]->tm_data.size() == 1280);
    REQUIRE(store.decode_entries(masters[0], 0).empty());
}
