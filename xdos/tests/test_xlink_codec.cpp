// SPDX-License-Identifier: Apache-2.0
// Round-trip tests for the XLink service-server codec.

#include "subsystems/xlink_codec.hpp"
#include "mib/mib.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstring>

using namespace xdos::xlink::codec;

// Helper: build a Parameter with the right fields set.
static xdos::mib::Parameter mk_param(int seq, int offset, int length,
                                     std::string name, std::string dtype,
                                     std::optional<std::string> unit = std::nullopt) {
    xdos::mib::Parameter p;
    p.seq = seq;
    p.offset = offset;
    p.length = length;
    p.name = std::move(name);
    p.data_type = std::move(dtype);
    p.unit = std::move(unit);
    return p;
}

static xdos::mib::Message make_ip_adr_tm() {
    xdos::mib::Message m;
    m.direction = xdos::Direction::TM;
    m.msg_id = 0x9;
    m.wire_id = 0x9;
    m.name = "IP_ADR_RSP";
    m.length_bytes = 24;
    m.offset_unit = "bits";
    m.parameters.push_back(mk_param(1,   0, 128, "product_key",   "HEX"));
    m.parameters.push_back(mk_param(2, 128,  32, "service_event", "UINT"));
    m.parameters.push_back(mk_param(3, 160,  32, "ip_address",    "UINT"));
    return m;
}

TEST_CASE("XLink codec: header-only IDLE round-trip", "[xlink]") {
    ProductKey key{};
    for (int i = 0; i < 16; ++i) key[i] = static_cast<std::uint8_t>(i);

    xdos::mib::Message idle;
    idle.direction = xdos::Direction::TC;
    idle.msg_id = 0;
    idle.parameters.push_back(mk_param(1,   0, 128, "product_key",   "HEX"));
    idle.parameters.push_back(mk_param(2, 128,  32, "service_event", "UINT"));

    auto frame = encode_frame(key, 0x0, FLAG_SET, idle, nlohmann::json::object());
    REQUIRE(frame.size() == 20);
    for (int i = 0; i < 16; ++i) REQUIRE(frame[i] == static_cast<std::uint8_t>(i));
    REQUIRE(frame[16] == 0x00);
    REQUIRE(frame[19] == 0x20);   // 0x20000000 LE, top byte last

    auto ack_frame = frame;
    ack_frame[19] = 0x60;         // ACK|SET
    auto d = decode_frame(ack_frame, nullptr);
    REQUIRE(d.framed_ok);
    REQUIRE(d.ack);
    REQUIRE_FALSE(d.nak);
    REQUIRE(d.event_base == 0x0);
}

TEST_CASE("XLink codec: IP_ADR little-endian encoding", "[xlink]") {
    ProductKey key{};
    auto tm = make_ip_adr_tm();
    nlohmann::json vals;
    vals["ip_address"] = 0x64030201u;   // 100.3.2.1 per spec §3.2.5

    auto frame = encode_frame(key, 0x9, FLAG_SET, tm, vals);
    REQUIRE(frame.size() == 24);
    REQUIRE(frame[20] == 0x01);   // LSByte first on the wire
    REQUIRE(frame[21] == 0x02);
    REQUIRE(frame[22] == 0x03);
    REQUIRE(frame[23] == 0x64);

    frame[19] = 0x60;
    auto d = decode_frame(frame, &tm);
    REQUIRE(d.framed_ok);
    REQUIRE(d.ack);
    REQUIRE(d.values["ip_address"].get<long long>() == 0x64030201);
}

TEST_CASE("XLink codec: bit-field packing at U32 boundary", "[xlink]") {
    xdos::mib::Message m;
    m.direction = xdos::Direction::TC;
    m.msg_id = 0x7;
    m.parameters.push_back(mk_param(1,   0, 128, "product_key",  "HEX"));
    m.parameters.push_back(mk_param(2, 128,  32, "service_event","UINT"));
    m.parameters.push_back(mk_param(3, 160,   3, "BdMode",       "UINT"));
    m.parameters.push_back(mk_param(4, 163,  29, "Reserve",      "UINT"));

    ProductKey key{};
    nlohmann::json vals;  vals["BdMode"] = 5;   // MODE_BD_460800
    auto frame = encode_frame(key, 0x7, FLAG_SET, m, vals);
    REQUIRE(frame.size() == 24);
    REQUIRE(frame[20] == 0x05);
    REQUIRE(frame[21] == 0x00);
    REQUIRE(frame[22] == 0x00);
    REQUIRE(frame[23] == 0x00);

    auto d = decode_frame(frame, &m);
    REQUIRE(d.values["BdMode"].get<long long>() == 5);
    REQUIRE(d.values["Reserve"].get<long long>() == 0);
}

TEST_CASE("XLink codec: INT sign extension", "[xlink]") {
    xdos::mib::Message m;
    m.direction = xdos::Direction::TM;
    m.parameters.push_back(mk_param(1,   0, 128, "product_key",   "HEX"));
    m.parameters.push_back(mk_param(2, 128,  32, "service_event", "UINT"));
    m.parameters.push_back(mk_param(3, 160,   8, "T_AD9361",      "INT", "°C"));

    ProductKey key{};
    nlohmann::json vals;  vals["T_AD9361"] = -25;
    auto frame = encode_frame(key, 0x4, FLAG_GET, m, vals);
    REQUIRE(frame[20] == 0xE7);           // two's complement of -25 in 8 bits

    auto d = decode_frame(frame, &m);
    REQUIRE(d.values["T_AD9361"].get<long long>() == -25);
}

TEST_CASE("XLink codec: product-key parser", "[xlink]") {
    // First key from XLK0151 file: XmtSymbFreq=2048, RcvSymbFreq=512
    auto k = parse_key_hex("ad563c56-ea8bf594-953e59c4-071d177f");
    REQUIRE(k.has_value());
    REQUIRE((*k)[0] == 0x56);       // LE: LSByte of 0xAD563C56 first
    REQUIRE((*k)[1] == 0x3C);
    REQUIRE((*k)[2] == 0x56);
    REQUIRE((*k)[3] == 0xAD);
}

TEST_CASE("XLink codec: NAK detection", "[xlink]") {
    std::vector<std::uint8_t> nak_frame(20, 0);
    static const char msg[] = "ACCESS_VIOLATION";
    std::memcpy(nak_frame.data(), msg, 16);
    nak_frame[19] = 0x80;                 // NAK flag
    auto d = decode_frame(nak_frame, nullptr);
    REQUIRE(d.framed_ok);
    REQUIRE(d.nak);
    REQUIRE_FALSE(d.ack);
}
