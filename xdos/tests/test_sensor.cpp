// SPDX-License-Identifier: Apache-2.0
// Sensor Controller (Tracker) — frame format, MIB and codec.
//
// The byte sequences below are the ground truth: they are what the ground
// station must put on the wire for each command in the TC SET / TC GET
// sheets.  If a change alters any of them, it altered the protocol.
#include "framing/sensor_framing.hpp"
#include "codec/codec.hpp"
#include "mib/mib.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>

using namespace xdos;
namespace sc = xdos::framing::sensor;

static std::filesystem::path mib_dir() {
    if (std::filesystem::is_directory("mib"))    return "mib";
    if (std::filesystem::is_directory("../mib")) return "../mib";
    return "../../mib";
}

static const mib::Mib& loaded() {
    static mib::Mib m = [] { mib::Mib x; x.load_dir(mib_dir()); return x; }();
    return m;
}

// Build the full frame the way Session::transact does: encode the payload
// from the MIB, then wrap it with the node address as DEST-ID.
static Bytes on_the_wire(const char* tc_name, const nlohmann::json& values) {
    const auto& m = loaded();
    const auto* msg = m.find(Subsystem::SENSOR, Direction::TC, tc_name);
    REQUIRE(msg != nullptr);
    int dest = m.node_address(Subsystem::SENSOR,
                              msg->node ? std::optional<std::string_view>(*msg->node)
                                        : std::nullopt);
    return sc::encode(static_cast<Byte>(dest),
                      static_cast<Byte>(msg->wire_id),
                      codec::encode(*msg, values));
}

TEST_CASE("sensor framing round-trips", "[sensor]") {
    Bytes payload { 0x00, 0x01 };
    auto f = sc::encode(50, 128, payload);
    REQUIRE(f == Bytes{ 0xA5, 0xAA, 0x32, 0x80, 0x02, 0x00, 0x01, 0xBA });

    auto d = sc::decode(f);
    REQUIRE(d.framed_ok);
    REQUIRE(d.dest_id == 50);
    REQUIRE(d.cmd_id  == 128);
    REQUIRE(d.data    == payload);
}

TEST_CASE("sensor framing tolerates leading noise", "[sensor]") {
    Bytes f { 0xFF, 0x00, 0xA5, 0xAA, 0x32, 0x07, 0x00, 0xBA };
    auto d = sc::decode(f);
    REQUIRE(d.framed_ok);
    REQUIRE(d.cmd_id == 7);
    REQUIRE(d.data.empty());
}

TEST_CASE("sensor framing rejects malformed frames", "[sensor]") {
    // EOF missing where the length byte says it should be
    REQUIRE_FALSE(sc::decode(Bytes{ 0xA5, 0xAA, 0x32, 0x80, 0x02, 0x00, 0x01, 0x00 })
                      .framed_ok);
    // declared length runs past the buffer
    REQUIRE_FALSE(sc::decode(Bytes{ 0xA5, 0xAA, 0x32, 0x80, 0x08, 0x00, 0xBA })
                      .framed_ok);
    // no sync pair at all
    REQUIRE_FALSE(sc::decode(Bytes{ 0x22, 0x69, 0x32, 0x80, 0x00, 0xBA }).framed_ok);
    REQUIRE_FALSE(sc::decode(Bytes{}).framed_ok);
}

TEST_CASE("sensor TC frames match the command sheets", "[sensor]") {
    // Every frame carries DEST-ID 50 (0x32) from the MIB node.
    SECTION("128 — power the Payload Server ON") {
        REQUIRE(on_the_wire("TC_SET_POWER_CTRL_PS_ES_BRD",
                            {{"board_type","PS"}, {"operation","PowerOn"}})
                == Bytes{ 0xA5, 0xAA, 0x32, 0x80, 0x02, 0x00, 0x00, 0xBA });
    }
    SECTION("128 — power the Edge Server OFF") {
        REQUIRE(on_the_wire("TC_SET_POWER_CTRL_PS_ES_BRD",
                            {{"board_type","ES"}, {"operation","PowerOff"}})
                == Bytes{ 0xA5, 0xAA, 0x32, 0x80, 0x02, 0x01, 0x01, 0xBA });
    }
    SECTION("128 — restart, given as raw numbers rather than enum names") {
        REQUIRE(on_the_wire("TC_SET_POWER_CTRL_PS_ES_BRD",
                            {{"board_type",0}, {"operation",2}})
                == Bytes{ 0xA5, 0xAA, 0x32, 0x80, 0x02, 0x00, 0x02, 0xBA });
    }
    SECTION("129 — select SSD1 for the Payload Server") {
        REQUIRE(on_the_wire("TC_SET_PS_ES_SSD_SELECTION",
                            {{"board_type","PS"}, {"ssd_number","SSD1"}})
                == Bytes{ 0xA5, 0xAA, 0x32, 0x81, 0x02, 0x00, 0x01, 0xBA });
    }
    SECTION("1 — read the Payload Server temperature") {
        // hw_instance uses its own enumeration: PS is 1 here, not 0.
        REQUIRE(on_the_wire("TC_GET_TEMPERATURE_SENSOR_DATA",
                            {{"hw_instance","PS"}})
                == Bytes{ 0xA5, 0xAA, 0x32, 0x01, 0x01, 0x01, 0xBA });
    }
    SECTION("4 — read the Payload Server SSD selection") {
        REQUIRE(on_the_wire("TC_GET_SSD_SELC", {{"board_type","PS"}})
                == Bytes{ 0xA5, 0xAA, 0x32, 0x04, 0x01, 0x00, 0xBA });
    }
    SECTION("7 — board power status carries no payload") {
        REQUIRE(on_the_wire("TC_GET_BOARD_PWR_CTRL_STS", nlohmann::json::object())
                == Bytes{ 0xA5, 0xAA, 0x32, 0x07, 0x00, 0xBA });
    }
}

TEST_CASE("sensor temperature decodes big-endian", "[sensor][codec]") {
    const auto* tm = loaded().find(Subsystem::SENSOR, Direction::TM,
                                   "TC_GET_TEMPERATURE_SENSOR_DATA_RSP");
    REQUIRE(tm != nullptr);

    // The field is raw hundredths of a degree — the MIB carries no "scale",
    // because the codec and the CLI apply scale in opposite directions.
    // TC GET sheet, worked example 1: MSB 0x09, LSB 0xE6 -> 2534 = +25.34 degC
    auto pos = codec::decode(*tm, Bytes{ 0x01, 0x09, 0xE6 });
    REQUIRE(pos["temp_value"].get<std::int64_t>() == 2534);
    REQUIRE(pos["hw_instance"]["name"] == "PS");

    // Worked example 2: MSB 0xFB, LSB 0xFF -> -1025 = -10.25 degC as int16_t.
    // This is the assertion that pins sign-extension across the BE path.
    auto neg = codec::decode(*tm, Bytes{ 0x00, 0xFB, 0xFF });
    REQUIRE(neg["temp_value"].get<std::int64_t>() == -1025);
    REQUIRE(neg["hw_instance"]["name"] == "OBC");

    // Read little-endian the same bytes would be -5111 and -7, not -1025.
    // This is what catches a regression in the endian plumbing.
    REQUIRE(neg["temp_value"].get<std::int64_t>() != -7);
}

TEST_CASE("sensor responses decode against the MIB", "[sensor][codec]") {
    const auto& m = loaded();

    SECTION("board power status — four instances") {
        const auto* tm = m.find(Subsystem::SENSOR, Direction::TM,
                                "TC_GET_BOARD_PWR_CTRL_STS_RSP");
        REQUIRE(tm != nullptr);
        auto v = codec::decode(*tm, Bytes{ 0x00, 0x01, 0x00, 0x01 });
        REQUIRE(v["PSA"]["name"] == "ON");
        REQUIRE(v["PSB"]["name"] == "OFF");
        REQUIRE(v["ES1"]["name"] == "ON");
        REQUIRE(v["ES2"]["name"] == "OFF");
    }

    SECTION("power control response — error field") {
        const auto* tm = m.find(Subsystem::SENSOR, Direction::TM,
                                "TC_SET_POWER_CTRL_PS_ES_BRD_RSP");
        REQUIRE(tm != nullptr);
        auto ok = codec::decode(*tm, Bytes{ 0x00, 0x00, 0x00 });
        REQUIRE(ok["error_field"]["name"] == "AVN_CMD_SUCCESS");
        auto bad = codec::decode(*tm, Bytes{ 0x00, 0x00, 0x09 });
        REQUIRE(bad["error_field"]["name"] == "ERR_PARAM_RANGE");
    }
}

TEST_CASE("sensor MIB carries DEST-ID 50 and the documented ids", "[sensor][mib]") {
    const auto& m = loaded();
    REQUIRE(m.node_address(Subsystem::SENSOR, std::string_view("SC")) == 50);

    struct { const char* name; int id; } expect[] = {
        { "TC_SET_POWER_CTRL_PS_ES_BRD",    128 },
        { "TC_SET_PS_ES_SSD_SELECTION",     129 },
        { "TC_GET_TEMPERATURE_SENSOR_DATA",   1 },
        { "TC_GET_SSD_SELC",                  4 },
        { "TC_GET_BOARD_PWR_CTRL_STS",        7 },
    };
    for (auto const& e : expect) {
        const auto* tc = m.find(Subsystem::SENSOR, Direction::TC, e.name);
        REQUIRE(tc != nullptr);
        REQUIRE(tc->wire_id == e.id);
        // Each TC has a response at the same wire id — the reply echoes it.
        REQUIRE(m.by_wire(Subsystem::SENSOR, Direction::TM, e.id) != nullptr);
    }
}

TEST_CASE("sensor NACK decodes against the EVENT, not the ACK", "[sensor][codec]") {
    const auto& m = loaded();

    // A NACK reuses the command id of what it rejects, so the ACK layout at
    // that id would happily misread it — and for command 128 that means
    // printing error_field = AVN_CMD_SUCCESS for a rejection.  The EVENT
    // entry is what makes the two distinguishable.
    const auto* ack = m.find(Subsystem::SENSOR, Direction::TM,
                             "TC_SET_POWER_CTRL_PS_ES_BRD_RSP");
    const auto* nak = m.find(Subsystem::SENSOR, Direction::Event,
                             "TC_SET_POWER_CTRL_PS_ES_BRD_NACK");
    REQUIRE(ack != nullptr);
    REQUIRE(nak != nullptr);
    REQUIRE(ack->wire_id == nak->wire_id);
    REQUIRE(*ack->length_bytes == 3);
    REQUIRE(*nak->length_bytes == 2);   // the length is the only discriminator

    auto v = codec::decode(*nak, Bytes{ 0x08, 0x80 });
    REQUIRE(v["error_event"]["name"] == "ERR_PARAM_RANGE");
    REQUIRE(v["error_byte"].get<std::int64_t>() == 128);

    // The NACK uses the frame-format sheet's Events table, where ERR_PARAM_RANGE
    // is 8.  The CMD STATUS table used by an ACK's error_field puts it at 9 and
    // calls 8 ERR_BUSY — reading a NACK with that table mislabels every code
    // from 5 up.  This assertion is what catches the two being confused.
    const auto* cmd_status = m.find(Subsystem::SENSOR, Direction::TM,
                                    "TC_SET_POWER_CTRL_PS_ES_BRD_RSP");
    const mib::Parameter* err = nullptr;
    for (auto const& pp : cmd_status->parameters)
        if (pp.name == "error_field") err = &pp;
    REQUIRE(err != nullptr);
    REQUIRE(err->enum_type != nullptr);
    REQUIRE(err->enum_type->by_value(8)->name == std::string("ERR_BUSY"));
    REQUIRE(err->enum_type->by_value(9)->name == std::string("ERR_PARAM_RANGE"));
}

TEST_CASE("sensor command 4 NACK is ambiguous by length", "[sensor][mib]") {
    const auto& m = loaded();
    // Known spec gap, recorded so it is not mistaken for a decode bug: the
    // ACK for TC_GET_SSD_SELC is two bytes and so is a NACK, leaving nothing
    // to tell them apart.  The session prefers the ACK.
    const auto* ack = m.find(Subsystem::SENSOR, Direction::TM, "TC_GET_SSD_SELC_RSP");
    const auto* nak = m.find(Subsystem::SENSOR, Direction::Event, "TC_GET_SSD_SELC_NACK");
    REQUIRE(ack != nullptr);
    REQUIRE(nak != nullptr);
    REQUIRE(*ack->length_bytes == *nak->length_bytes);
    REQUIRE(nak->description.find("AMBIGUOUS") != std::string::npos);
}
