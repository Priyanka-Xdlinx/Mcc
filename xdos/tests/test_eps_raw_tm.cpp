// SPDX-License-Identifier: Apache-2.0
// Regression: EPS telemetry that reaches the ground station as a bare message
// array rather than the Base64 line frame.
//
// The OBC simulator forwards the EPS bus frame verbatim, so TM DATA carries
// 0x00 + Base64(array) + 0x01.  The flight OBC un-Base64s the line frame
// itself and fills TM DATA with the array — dest|src|msg_id|data|crc8.  Both
// shapes must decode over the same code path.
//
// The frame replayed here is a real capture: Get_FW_Version to PDU1 answered
// by flight hardware (SCID 625, APID 3, FECF 0x90A8).

#include "framing/eps_framing.hpp"
#include "mib/mib.hpp"
#include "session/session.hpp"
#include "codec/ccsds_codec.hpp"
#include "codec/ccsds_tm_codec.hpp"
#include "transport/tcp_transport.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <vector>
#include <filesystem>
#include <thread>

using namespace xdos;
namespace eps = xdos::framing::eps;

static std::filesystem::path mib_root() {
    if (std::filesystem::is_directory("mib"))    return "mib";
    if (std::filesystem::is_directory("../mib")) return "../mib";
    return "../../mib";
}

// The captured downlink: 64 header/user bytes, 1366 zero pad, then the FECF.
static Bytes captured_hw_frame() {
    static const Byte head[] = {
        0xFC, 0xA1, 0x27, 0x11, 0x00, 0x00, 0x00, 0x00, 0x98, 0x00, 0x08, 0x03,
        0xC0, 0x00, 0x05, 0x85, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0xC9, 0x01, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01,
        0x01, 0x5A,
    };
    Bytes f(head, head + sizeof(head));
    f.resize(ccsds::CcsdsTmCodec::kFrameLen - 2, 0x00);
    f.push_back(0x90);
    f.push_back(0xA8);
    return f;
}

TEST_CASE("EPS decode accepts the bare message array", "[eps][framing]") {
    // dest = 201 (OBC1), src = 1 (PDU1), msg_id = 1, 7 data bytes, CRC-8 0x5A.
    Bytes raw{0xC9, 0x01, 0x01, 0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0x5A};
    auto d = eps::decode(raw);
    REQUIRE(d.framed_ok);
    REQUIRE(d.raw_array);
    REQUIRE_FALSE(d.wrapped);
    REQUIRE(d.crc_ok);
    REQUIRE(d.dest_addr == eps::kObc1Addr);
    REQUIRE(d.src_addr  == 1);
    REQUIRE(d.msg_id    == 1);
    REQUIRE(d.data      == Bytes{0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01});
}

TEST_CASE("EPS decode still reads the wrapped line frame", "[eps][framing]") {
    auto wire = eps::encode(1 /*PDU1*/, 1 /*Get_FW_Version*/, Bytes{});
    auto d    = eps::decode(wire);
    REQUIRE(d.framed_ok);
    REQUIRE(d.wrapped);
    REQUIRE_FALSE(d.raw_array);
    REQUIRE(d.crc_ok);
    REQUIRE(d.msg_id == 1);
}

TEST_CASE("Captured flight TM frame decodes end to end", "[eps][tm][e2e]") {
    mib::Mib mib; mib.load_dir(mib_root());
    ccsds::CcsdsCodec   tc;  REQUIRE(tc.load((mib_root() / "ccsds_tc.json").string()));
    ccsds::CcsdsTmCodec tm;  REQUIRE(tm.load((mib_root() / "ccsds_tm.json").string()));

    const auto frame = captured_hw_frame();
    REQUIRE(frame.size() == ccsds::CcsdsTmCodec::kFrameLen);

    // The CCSDS layer was never the problem — assert it so a future change to
    // the CRC or header layout fails here rather than in the field.
    auto dec = tm.decode(frame);
    REQUIRE(dec.has_value());
    REQUIRE(dec->crc_ok);
    REQUIRE(dec->spacecraft_id == 625);
    REQUIRE(dec->apid == 3);
    REQUIRE(dec->subsystem == Subsystem::EPS);

    // Replay it as the OBC would, in two TCP segments split mid-frame, so the
    // reassembly path is exercised alongside the framing fix.
    transport::TcpServer srv;
    REQUIRE(srv.start("127.0.0.1", 10931, [&frame](int fd, const std::string&) {
        auto req = transport::fd_recv_blocking(fd, 8192);
        if (req.empty()) return;
        Bytes a(frame.begin(), frame.begin() + 900);
        Bytes b(frame.begin() + 900, frame.end());
        transport::fd_send(fd, a);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        transport::fd_send(fd, b);
        transport::fd_recv_blocking(fd, 8192);   // hold the connection open
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    session::Session sess(mib);
    sess.set_ccsds_codec(&tc);
    sess.set_tm_codec(&tm);
    sess.set_ccsds_enabled(true);
    sess.configure(Subsystem::EPS, {"127.0.0.1", 10931});
    REQUIRE(sess.connect(Subsystem::EPS));

    auto r = sess.send_tc(Subsystem::EPS, "Get_FW_Version",
                          nlohmann::json::object(), std::string("PDU1"));

    INFO("error: " << r.error);
    REQUIRE(r.error.empty());
    REQUIRE(r.ok);
    REQUIRE(r.rx_frame.size() == ccsds::CcsdsTmCodec::kFrameLen);
    REQUIRE(r.tm_frame.has_value());
    REQUIRE(r.tm_frame->crc_ok);

    // TM DATA trimmed to the true array boundary — CRC byte kept, pad dropped.
    REQUIRE(r.rx_native == Bytes{0xC9, 0x01, 0x01, 0x01, 0x00,
                                 0x00, 0x01, 0x00, 0x01, 0x01, 0x5A});
    REQUIRE(r.response_msg_id == 1);
    REQUIRE(r.rx_payload == Bytes{0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01});

    const auto& v = r.decoded_values;
    REQUIRE(v["Firmware Major Version"]              == 1);
    REQUIRE(v["Firmware Minor Version"]              == 0);
    REQUIRE(v["Firmware Patch Version"]              == 0);
    REQUIRE(v["Communication Protocol Major Version"] == 1);
    REQUIRE(v["Communication Protocol Minor Version"] == 0);

    srv.stop();
}

// ── Trim boundary comes from the CRC, not the spec sheet ───────────────────
// Both arrays below are real flight-hardware replies captured on the bench.
// Get_OBC_Parameters matches the MIB's declared length; Get_PDU_CH_State does
// not — the sheet says 2 data bytes, the PDU answers with 4.  Both must land
// on the same 8-byte boundary, because in both cases that is where the CRC-8
// checks out.
TEST_CASE("EPS raw array trims where the CRC says, not the MIB", "[eps][framing]") {
    struct Case { const char* name; Bytes arr; };
    const std::vector<Case> cases{
        // dest 201 | src 1 | msg_id 10 | 3C 00 03 01 | crc 0x77   (MIB says 4)
        {"Get_OBC_Parameters",
         {0xC9, 0x01, 0x0A, 0x3C, 0x00, 0x03, 0x01, 0x77}},
        // dest 201 | src 1 | msg_id  3 | FD FF 00 00 | crc 0xA3   (MIB says 2)
        {"Get_PDU_CH_State",
         {0xC9, 0x01, 0x03, 0xFD, 0xFF, 0x00, 0x00, 0xA3}},
    };
    for (const auto& c : cases) {
        INFO(c.name);
        auto d = eps::decode(c.arr);
        REQUIRE(d.framed_ok);
        REQUIRE(d.raw_array);
        REQUIRE(d.crc_ok);
        REQUIRE(d.data.size() == 4);
    }
}

TEST_CASE("Length disagreement still decodes over the wire", "[eps][tm][e2e]") {
    mib::Mib mib; mib.load_dir(mib_root());
    ccsds::CcsdsCodec   tc;  REQUIRE(tc.load((mib_root() / "ccsds_tc.json").string()));
    ccsds::CcsdsTmCodec tm;  REQUIRE(tm.load((mib_root() / "ccsds_tm.json").string()));

    // Get_PDU_CH_State: the PDU returns 4 data bytes where the MIB declares 2.
    Bytes reply{0xC9, 0x01, 0x03, 0xFD, 0xFF, 0x00, 0x00, 0xA3};
    auto frame = tm.encode(Subsystem::EPS, reply, Bytes{});
    REQUIRE(frame.size() == ccsds::CcsdsTmCodec::kFrameLen);

    transport::TcpServer srv;
    REQUIRE(srv.start("127.0.0.1", 10932, [&frame](int fd, const std::string&) {
        if (transport::fd_recv_blocking(fd, 8192).empty()) return;
        transport::fd_send(fd, frame);
        transport::fd_recv_blocking(fd, 8192);
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    session::Session sess(mib);
    sess.set_ccsds_codec(&tc);
    sess.set_tm_codec(&tm);
    sess.set_ccsds_enabled(true);
    sess.configure(Subsystem::EPS, {"127.0.0.1", 10932});
    REQUIRE(sess.connect(Subsystem::EPS));

    auto r = sess.send_tc(Subsystem::EPS, "Get_PDU_CH_State",
                          nlohmann::json::object(), std::string("PDU1"));
    INFO("error: " << r.error);
    REQUIRE(r.error.empty());
    REQUIRE(r.ok);
    REQUIRE(r.rx_native == reply);              // full 8 bytes, CRC intact
    REQUIRE(r.rx_payload.size() == 4);          // all four data bytes kept
    REQUIRE(r.decoded_values["CH1 to CH8 Enable/Disable"]  == 0xFD);
    REQUIRE(r.decoded_values["CH9 to CH16 Enable/Disable"] == 0xFF);

    srv.stop();
}
