// SPDX-License-Identifier: Apache-2.0
// UHF (Drishti OpenLST) — ESP framing, MIB, routing and APID wiring.
#include <catch2/catch_test_macros.hpp>

#include "framing/uhf_framing.hpp"
#include "codec/codec.hpp"
#include "codec/ccsds_codec.hpp"
#include "mib/mib.hpp"
#include "obc/bridge.hpp"

#include <filesystem>

using namespace xdos;
namespace uf = xdos::framing::uhf;
using nlohmann::json;

static std::string mib_dir() {
    for (auto p : { "mib", "../mib", "../../mib" })
        if (std::filesystem::is_directory(p)) return p;
    return "mib";
}
static const mib::Mib& shared_mib() {
    static mib::Mib m;
    static bool loaded = (m.load_dir(mib_dir()), true);
    (void)loaded;
    return m;
}

TEST_CASE("UHF ESP framing: GET_TELEM is the documented 9-byte frame", "[uhf]") {
    // 22 69 | len 06 | hwid FFFF | seq 0000 | system 01 | command 17
    Bytes f = uf::encode(0x17, {});
    REQUIRE(f == Bytes{ 0x22, 0x69, 0x06, 0xFF, 0xFF, 0x00, 0x00, 0x01, 0x17 });
}

TEST_CASE("UHF ESP framing: round trip, garbage tolerance, limits", "[uhf]") {
    Bytes data = { 1, 2, 3, 4, 5, 6, 7 };
    Bytes f = uf::encode(0x1e, data, 0x0001, 0xBEEF);
    auto d = uf::decode(f);
    REQUIRE(d.framed_ok);
    REQUIRE(d.hwid == 0x0001);
    REQUIRE(d.seqnum == 0xBEEF);
    REQUIRE(d.system == uf::kSystemRadio);
    REQUIRE(d.command == 0x1e);
    REQUIRE(d.data == data);

    // leading noise before the sync pair is skipped
    Bytes noisy = { 0x00, 0x99, 0x22 };
    noisy.insert(noisy.end(), f.begin(), f.end());
    auto d2 = uf::decode(noisy);
    REQUIRE(d2.framed_ok);
    REQUIRE(d2.command == 0x1e);

    // truncated frame rejected
    Bytes cut(f.begin(), f.end() - 2);
    REQUIRE_FALSE(uf::decode(cut).framed_ok);

    // oversize data clamps at the ESP payload limit (251 -> 254-byte frame)
    Bytes big(300, 0xAB);
    Bytes fb = uf::encode(0x17, big);
    REQUIRE(fb.size() == 254);
    REQUIRE(fb[2] == 251);
}

TEST_CASE("UHF MIB: command set and payload sizes match the firmware", "[uhf]") {
    const auto& m = shared_mib();
    REQUIRE(m.subsystem(Subsystem::UHF) != nullptr);

    // 24 distinct TC opcodes (REBOOT has an immediate + postpone form)
    auto tcs = m.messages(Subsystem::UHF, Direction::TC);
    REQUIRE(tcs.size() == 25);

    const mib::Message* gt = m.find(Subsystem::UHF, Direction::TC, "GET_TELEM");
    REQUIRE(gt != nullptr);
    REQUIRE(gt->wire_id == 0x17);

    const mib::Message* telem = m.find(Subsystem::UHF, Direction::TM, "TELEM");
    REQUIRE(telem != nullptr);
    REQUIRE(telem->wire_id == 0x18);
    REQUIRE(telem->length_bytes.value_or(0) == 70);      // telemetry_t

    REQUIRE(m.find(Subsystem::UHF, Direction::TM, "TX_RADIO_CONFIG")
                ->length_bytes.value_or(0) == 22);        // radio_config_t
    REQUIRE(m.find(Subsystem::UHF, Direction::TM, "RX_RADIO_PKT_CONFIG")
                ->length_bytes.value_or(0) == 8);         // radio_pkt_config_t
    REQUIRE(m.find(Subsystem::UHF, Direction::TM, "POWER_SETTINGS")
                ->length_bytes.value_or(0) == 13);        // radio_power_settings_t
    REQUIRE(m.find(Subsystem::UHF, Direction::TM, "BEACON_TIMERS")
                ->length_bytes.value_or(0) == 7);         // beacon_config_t

    // codec encode: SET_BEACON_TIMERS packs 7 little-endian bytes
    const mib::Message* sbt =
        m.find(Subsystem::UHF, Direction::TC, "SET_BEACON_TIMERS");
    REQUIRE(sbt != nullptr);
    json v = { {"Tx Period", 2}, {"Tx Timeout", 30},
               {"Tx Interval Timeout", 60}, {"Beacon Mode Timeout", 1200} };
    Bytes payload = codec::encode(*sbt, v);
    REQUIRE(payload == Bytes{ 2, 30, 60, 0xB0, 0x04, 0x00, 0x00 });
}

TEST_CASE("OBC bridge sniffs UHF frames by the ESP sync pair", "[uhf]") {
    Bytes f = uf::encode(0x17, {});
    auto s = obc::Bridge::sniff_subsystem(f);
    REQUIRE(s.has_value());
    REQUIRE(*s == Subsystem::UHF);
    // a >=20-byte UHF frame must not fall into the loose XLINK heuristic
    Bytes big = uf::encode(0x24, Bytes(22, 0x10));
    REQUIRE(obc::Bridge::sniff_subsystem(big) == Subsystem::UHF);
    // junk is still unrecognised
    REQUIRE_FALSE(obc::Bridge::sniff_subsystem({0xDE,0xAD,0xBE,0xEF}).has_value());
}

TEST_CASE("CCSDS: UHF is APID 2 in both directions", "[uhf]") {
    ccsds::CcsdsCodec codec;
    codec.load(mib_dir() + "/ccsds_tc.json");
    REQUIRE(codec.apid_to_subsystem(0x002).has_value());
    REQUIRE(*codec.apid_to_subsystem(0x002) == Subsystem::UHF);
    auto ep = codec.subsystem_endpoint(Subsystem::UHF);
    REQUIRE(ep.has_value());
    REQUIRE(ep->port == 10060);

    // encode a UHF TC and confirm the frame decodes back to APID 2
    Bytes native = uf::encode(0x17, {});
    Bytes wire = codec.encode(Subsystem::UHF, ccsds::CcsdsDirection::TC,
                              0x17, native);
    auto d = codec.decode(wire);
    REQUIRE(d.has_value());
    REQUIRE(d->apid == 2);
    REQUIRE(d->tc_data == native);
}
