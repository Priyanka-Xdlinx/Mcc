// SPDX-License-Identifier: Apache-2.0
// Payload Server tests — "Command and Data Format Specification".
//
// Covers the 3-byte payload header (Type / Command ID / Length), the
// spec's data-field layouts, CCSDS carriage under APID 5 with TC_ID =
// Command ID, and a full GS -> sim -> telemetry round trip.
//
// Run isolated: ./build/xdos_tests "[payload]"

#include "codec/ccsds_codec.hpp"
#include "framing/payload_framing.hpp"
#include "session/session.hpp"
#include "transport/tcp_transport.hpp"
#include "mib/mib.hpp"

#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <thread>

using namespace xdos;
using namespace xdos::ccsds;
namespace pf = xdos::framing::payload;

static std::string profile_path() {
    for (const auto& p : { "mib/ccsds_tc.json",
                           "../mib/ccsds_tc.json",
                           "../../mib/ccsds_tc.json" }) {
        if (std::filesystem::exists(p)) return p;
    }
    throw std::runtime_error("test: cannot find mib/ccsds_tc.json");
}

static std::filesystem::path mib_dir() {
    for (auto p : { "mib", "../mib", "../../mib" })
        if (std::filesystem::is_directory(p)) return p;
    return "mib";
}

// ─── header bit layout (spec §1) ────────────────────────────────────────────
TEST_CASE("Payload: 3-byte header packs type/command/length", "[payload][framing]") {
    // Docker ON (545) targeting SCANWAY (1) — one data byte.
    Bytes frame = pf::encode(pf::MsgType::Telecommand, pf::kCmdContainerOn,
                             Bytes{ pf::kPayloadScanway });
    REQUIRE(frame.size() == 4);                    // 3 header + 1 data

    // bits 23..21 type, 20..11 command id, 10..0 length
    REQUIRE(((frame[0] >> 5) & 0x07) == 0);        // Telecommand
    REQUIRE((((frame[0] & 0x1F) << 5) | ((frame[1] >> 3) & 0x1F)) == 545);
    REQUIRE((((frame[1] & 0x07) << 8) | frame[2]) == 1);
    REQUIRE(frame[3] == pf::kPayloadScanway);
}

TEST_CASE("Payload: header round-trips for every message type", "[payload][framing]") {
    const std::uint16_t cmds[] = { pf::kCmdContainerOn, pf::kCmdContainerOff,
                                   pf::kCmdPayloadSeq,  pf::kCmdCaptureMsi,
                                   pf::kCmdCaptureScanway, pf::kCmdHealthMetrics };
    for (int t = 0; t <= 7; ++t) {
        for (auto cmd : cmds) {
            Bytes data(t * 13, Byte{0xA5});        // 0..91 bytes
            auto frame = pf::encode(static_cast<pf::MsgType>(t), cmd, data);
            auto d = pf::decode(frame);
            REQUIRE(d.header_ok);
            REQUIRE(d.length_ok);
            REQUIRE(static_cast<int>(d.type) == t);
            REQUIRE(d.command_id == cmd);
            REQUIRE(d.length == data.size());
            REQUIRE(d.data == data);
        }
    }
}

TEST_CASE("Payload: encode rejects out-of-range command id and length",
          "[payload][framing]") {
    REQUIRE(pf::encode(pf::MsgType::Telecommand, 1024, Bytes{}).empty());
    REQUIRE_FALSE(pf::encode(pf::MsgType::Telecommand, 1023, Bytes{}).empty());

    Bytes too_big(pf::kMaxDataLen + 1, Byte{0});
    REQUIRE(pf::encode(pf::MsgType::Telecommand, 600, too_big).empty());

    Bytes at_limit(pf::kMaxDataLen, Byte{0});
    REQUIRE(pf::encode(pf::MsgType::Telecommand, 600, at_limit).size()
            == pf::kHeaderLen + pf::kMaxDataLen);
}

TEST_CASE("Payload: frame_size drives length-prefixed stream reads",
          "[payload][framing]") {
    auto frame = pf::encode(pf::MsgType::Telemetry, pf::kCmdCaptureMsi,
                            Bytes(200, Byte{0x11}));
    REQUIRE(pf::frame_size(frame.data()) == frame.size());

    // Two frames back to back in one TCP segment must split at the boundary.
    Bytes stream = frame;
    auto second = pf::encode(pf::MsgType::Telemetry, pf::kCmdContainerOff,
                             Bytes{ pf::kPayloadSar });
    stream.insert(stream.end(), second.begin(), second.end());

    std::size_t first_len = pf::frame_size(stream.data());
    REQUIRE(first_len == frame.size());
    REQUIRE(pf::frame_size(stream.data() + first_len) == second.size());
}

TEST_CASE("Payload: decode flags a truncated data field", "[payload][framing]") {
    auto frame = pf::encode(pf::MsgType::Telecommand, pf::kCmdPayloadSeq,
                            Bytes(50, Byte{0x7E}));
    frame.resize(frame.size() - 10);               // lose the tail
    auto d = pf::decode(frame);
    REQUIRE(d.header_ok);
    REQUIRE(d.length == 50);
    REQUIRE_FALSE(d.length_ok);                    // declared 50, 40 present
}

// ─── data-field layouts (spec §3.1) ─────────────────────────────────────────
TEST_CASE("Payload: custom sequence data field is [id][nlen][alen][name][args]",
          "[payload]") {
    auto d = pf::make_sequence_data(pf::kPayloadMsi, "capture_burst", "n=5,exp=20ms");
    REQUIRE(d[0] == pf::kPayloadMsi);
    REQUIRE(d[1] == 13);                           // "capture_burst"
    REQUIRE(d[2] == 12);                           // "n=5,exp=20ms"
    REQUIRE(d.size() == 3u + 13u + 12u);
    REQUIRE(std::string(d.begin() + 3, d.begin() + 16) == "capture_burst");
    REQUIRE(std::string(d.begin() + 16, d.end()) == "n=5,exp=20ms");
}

TEST_CASE("Payload: sequence fields truncate at the spec maxima", "[payload]") {
    std::string long_name(40, 'x');                // max 16
    std::string long_args(200, 'y');               // max 128
    auto d = pf::make_sequence_data(pf::kPayloadSar, long_name, long_args);
    REQUIRE(d[1] == pf::kMaxSeqNameLen);
    REQUIRE(d[2] == pf::kMaxSeqArgsLen);
    REQUIRE(d.size() == 3 + pf::kMaxSeqNameLen + pf::kMaxSeqArgsLen);
}

TEST_CASE("Payload: IDs are 0=MSI 1=SCANWAY 2=SAR", "[payload]") {
    REQUIRE(pf::kPayloadMsi     == 0);
    REQUIRE(pf::kPayloadScanway == 1);
    REQUIRE(pf::kPayloadSar     == 2);
    REQUIRE(std::string(pf::payload_name(0)) == "MSI");
    REQUIRE(std::string(pf::payload_name(1)) == "SCANWAY");
    REQUIRE(std::string(pf::payload_name(2)) == "SAR");
}

// ─── MIB ────────────────────────────────────────────────────────────────────
TEST_CASE("Payload: MIB carries the spec's command set", "[payload][mib]") {
    mib::Mib m; m.load_dir(mib_dir());
    REQUIRE(m.subsystem(Subsystem::Payload) != nullptr);

    struct { int id; const char* name; } expect[] = {
        { 545, "Container_Operation_ON"  },
        { 546, "Container_Operation_OFF" },
        { 600, "Custom_Payload_Sequence" },
        { 601, "Capture_MSI"             },
        { 602, "Capture_SCANWAY"         },
    };
    for (auto& e : expect) {
        auto* tc = m.by_wire(Subsystem::Payload, Direction::TC, e.id);
        REQUIRE(tc != nullptr);
        REQUIRE(tc->name == e.name);
    }

    // Docker ON/OFF carry exactly one enum-typed Payload ID byte.
    auto* on = m.find(Subsystem::Payload, Direction::TC, "Container_Operation_ON");
    REQUIRE(on->length_bytes.value_or(0) == 1);
    REQUIRE(on->parameters.size() == 1);
    REQUIRE(on->parameters[0].name == "Payload ID");
    REQUIRE(on->parameters[0].enum_type != nullptr);
    REQUIRE(on->parameters[0].enum_type->by_value(2)->name == "SAR");
}

// ─── CCSDS carriage ─────────────────────────────────────────────────────────
TEST_CASE("Payload: CCSDS carries the frame under APID 5 with TC_ID = command",
          "[payload][ccsds]") {
    CcsdsCodec codec;
    REQUIRE(codec.load(profile_path()));
    REQUIRE(codec.apid_to_subsystem(0x005) == Subsystem::Payload);

    auto inner = pf::encode(pf::MsgType::Telecommand, pf::kCmdPayloadSeq,
                            pf::make_sequence_data(pf::kPayloadScanway,
                                                   "scan", "mode=hi"));
    Bytes wire = codec.encode(Subsystem::Payload, CcsdsDirection::TC,
                              pf::kCmdPayloadSeq, inner);

    auto d = codec.decode(wire);
    REQUIRE(d.has_value());
    REQUIRE(d->crc_ok);
    REQUIRE(d->apid == 0x005);
    REQUIRE(d->subsystem == Subsystem::Payload);
    REQUIRE(d->tc_id == 600);                      // TC_ID mirrors Command ID
    REQUIRE(d->tc_data == inner);

    auto p = pf::decode(d->tc_data);
    REQUIRE(p.length_ok);
    REQUIRE(p.command_id == 600);
    REQUIRE(p.data[0] == pf::kPayloadScanway);
}

// ─── end-to-end against a sim that mirrors xdos_payload_sim ─────────────────
namespace {
struct PayloadLoopServer {
    transport::TcpServer srv;
    CcsdsCodec*          codec = nullptr;
    std::atomic<int>     frames_seen{0};
    std::mutex           mu;
    pf::Decoded          last;

    bool start(int port, CcsdsCodec* c) {
        codec = c;
        return srv.start("127.0.0.1", port,
            [this](int fd, const std::string&) {
                Bytes buf;
                while (true) {
                    auto chunk = transport::fd_recv_blocking(fd, 8192);
                    if (chunk.empty()) break;
                    buf.insert(buf.end(), chunk.begin(), chunk.end());
                    while (buf.size() >= 6 && buf[0] == 0x1A && buf[1] == 0xCF) {
                        std::size_t need = 2 + (((buf[4] & 0x03u) << 8) | buf[5]) + 1u;
                        if (buf.size() < need) break;
                        Bytes frame(buf.begin(), buf.begin() + need);
                        buf.erase(buf.begin(), buf.begin() + need);

                        auto d = codec->decode(frame);
                        if (!d || !d->crc_ok) continue;
                        auto p = pf::decode(d->tc_data);
                        {
                            std::lock_guard<std::mutex> g(mu);
                            last = p;
                            ++frames_seen;
                        }
                        // Reply: single-character ACK echoing the command id.
                        auto rep = pf::encode(pf::MsgType::Telemetry, p.command_id,
                                              Bytes{ pf::kAckChar });
                        transport::fd_send(fd, codec->encode(Subsystem::Payload,
                                              CcsdsDirection::TM, p.command_id, rep));
                    }
                }
            });
    }
    void stop() { srv.stop(); }
};
}

TEST_CASE("Payload: end-to-end send_tc -> sim -> ACK", "[payload][e2e]") {
    CcsdsCodec codec;
    REQUIRE(codec.load(profile_path()));

    PayloadLoopServer sim;
    REQUIRE(sim.start(20041, &codec));

    mib::Mib mib; mib.load_dir(mib_dir());
    session::Session sess(mib);
    sess.set_ccsds_codec(&codec);
    sess.set_ccsds_enabled(true);
    sess.configure(Subsystem::Payload, { "127.0.0.1", 20041 });
    REQUIRE(sess.connect(Subsystem::Payload));

    // Docker ON for SAR, through the ordinary MIB-driven TC path.
    auto r = sess.send_tc(Subsystem::Payload, "Container_Operation_ON",
                          nlohmann::json{{"Payload ID", pf::kPayloadSar}});
    REQUIRE(r.error.empty());
    REQUIRE(r.ccsds_used);

    // The native frame the session built is the payload frame itself.
    auto sent = pf::decode(r.tx_native);
    REQUIRE(sent.header_ok);
    REQUIRE(sent.type == pf::MsgType::Telecommand);
    REQUIRE(sent.command_id == 545);
    REQUIRE(sent.length == 1);
    REQUIRE(sent.data[0] == pf::kPayloadSar);

    // ...and the sim saw exactly that.
    {
        std::lock_guard<std::mutex> g(sim.mu);
        REQUIRE(sim.frames_seen == 1);
        REQUIRE(sim.last.command_id == 545);
        REQUIRE(sim.last.data[0] == pf::kPayloadSar);
    }

    // The payload server acknowledges rather than returning telemetry: a
    // single character in the data field of an otherwise identical frame.
    auto got = pf::decode(r.rx_native);
    REQUIRE(got.header_ok);
    REQUIRE(got.command_id == 545);
    REQUIRE(pf::is_ack(got));
    REQUIRE(got.length == 1);
    REQUIRE(got.data[0] == pf::kAckChar);
    REQUIRE(r.response_msg_id == 545);

    sess.disconnect_all();
    sim.stop();
}

TEST_CASE("Payload: variable-length sequence rides send_tc_data", "[payload][e2e]") {
    CcsdsCodec codec;
    REQUIRE(codec.load(profile_path()));

    PayloadLoopServer sim;
    REQUIRE(sim.start(20042, &codec));

    mib::Mib mib; mib.load_dir(mib_dir());
    session::Session sess(mib);
    sess.set_ccsds_codec(&codec);
    sess.set_ccsds_enabled(true);
    sess.configure(Subsystem::Payload, { "127.0.0.1", 20042 });
    REQUIRE(sess.connect(Subsystem::Payload));

    auto data = pf::make_sequence_data(pf::kPayloadMsi, "capture", "n=3");
    auto r = sess.send_tc_data(Subsystem::Payload, "Custom_Payload_Sequence", data);
    REQUIRE(r.error.empty());

    auto sent = pf::decode(r.tx_native);
    REQUIRE(sent.command_id == 600);
    REQUIRE(sent.length == data.size());
    REQUIRE(sent.data == data);

    {
        std::lock_guard<std::mutex> g(sim.mu);
        REQUIRE(sim.last.command_id == 600);
        REQUIRE(sim.last.data == data);
    }

    sess.disconnect_all();
    sim.stop();
}

// ─── acknowledgement (no telemetry) ─────────────────────────────────────────
TEST_CASE("Payload: ACK is a single character in an otherwise identical frame",
          "[payload][ack]") {
    // Same header shape as the command it answers — only the data differs.
    auto ack = pf::encode(pf::MsgType::Telemetry, pf::kCmdCaptureScanway,
                          Bytes{ pf::kAckChar });
    REQUIRE(ack.size() == pf::kHeaderLen + 1);

    auto d = pf::decode(ack);
    REQUIRE(pf::is_ack(d));
    REQUIRE(d.command_id == pf::kCmdCaptureScanway);
    REQUIRE(d.length == 1);
    REQUIRE(d.data[0] == 'A');

    // A NAK is structurally an ACK frame carrying the rejection character.
    auto nak = pf::decode(pf::encode(pf::MsgType::Telemetry, pf::kCmdCaptureScanway,
                                     Bytes{ pf::kNakChar }));
    REQUIRE(pf::is_ack(nak));
    REQUIRE(nak.data[0] == 'N');

    // Anything that is not exactly one data byte is not an acknowledgement.
    REQUIRE_FALSE(pf::is_ack(pf::decode(
        pf::encode(pf::MsgType::Telemetry, 545, Bytes{}))));
    REQUIRE_FALSE(pf::is_ack(pf::decode(
        pf::encode(pf::MsgType::Telemetry, 545, Bytes{ 'A', 'B' }))));
}
