// SPDX-License-Identifier: Apache-2.0
// XDOS Ground Station — CCSDS TC/TM codec implementation (v2 per sheet).
//
// Wire layout (no EOF):
//   SOF(2) | TF-hdr(5) | Pkt-hdr(6) | GS-ID(1) | TimeCode(8) | AncLen(2)
//          | TC-ID(2)  | TC-Len(2)  | TC-Data(N) | CRC(2)
//
// CRC coverage: TF-hdr → TC-Data end.
#include "codec/ccsds_codec.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>

namespace xdos::ccsds {

using nlohmann::json;

// ─── Bit-buffer helpers (MSB-first) ─────────────────────────────────────────
static void pack_bits(Byte* buf, std::size_t bit_offset,
                      std::size_t bit_width, std::uint64_t value) {
    for (std::size_t i = 0; i < bit_width; ++i) {
        std::size_t bit_pos     = bit_offset + i;
        std::size_t byte_idx    = bit_pos / 8;
        std::size_t bit_in_byte = 7 - (bit_pos % 8);
        std::uint64_t bit_val   = (value >> (bit_width - 1 - i)) & 0x1ULL;
        if (bit_val) buf[byte_idx] |=  static_cast<Byte>(1u << bit_in_byte);
        else         buf[byte_idx] &= ~static_cast<Byte>(1u << bit_in_byte);
    }
}

static std::uint64_t unpack_bits(const Byte* buf,
                                 std::size_t bit_offset, std::size_t bit_width) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < bit_width; ++i) {
        std::size_t bit_pos     = bit_offset + i;
        std::size_t byte_idx    = bit_pos / 8;
        std::size_t bit_in_byte = 7 - (bit_pos % 8);
        std::uint64_t bit_val   = (buf[byte_idx] >> bit_in_byte) & 0x1ULL;
        value = (value << 1) | bit_val;
    }
    return value;
}

// ─── CRC-16 CCITT (poly 0x1021, init 0xFFFF, no XOR-out, MSB-first) ─────────
std::uint16_t CcsdsCodec::crc16_ccitt(const Byte* data, std::size_t len) {
    std::uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= static_cast<std::uint16_t>(data[i]) << 8;
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000) ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<std::uint16_t>(crc << 1);
        }
    }
    return crc;
}

// ─── Time code (8 bytes, centisecond resolution) ────────────────────────────
//   Year(2) | Mo(1) | DOM(1) | h(1) | m(1) | s(1) | cs(1)
//   cs = 0..99 (multiply by 10 for ms)
void CcsdsCodec::write_time_code(Byte* out) {
    auto now = std::chrono::system_clock::now();
    auto secs = std::chrono::time_point_cast<std::chrono::seconds>(now);
    auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(now - secs).count();
    int  cs   = static_cast<int>(ms / 10);   // 0..99

    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &t);
#else
    gmtime_r(&t, &utc);
#endif
    int year = utc.tm_year + 1900;

    out[0] = static_cast<Byte>((year >> 8) & 0xFF);
    out[1] = static_cast<Byte>( year       & 0xFF);
    out[2] = static_cast<Byte>(utc.tm_mon + 1);
    out[3] = static_cast<Byte>(utc.tm_mday);
    out[4] = static_cast<Byte>(utc.tm_hour);
    out[5] = static_cast<Byte>(utc.tm_min);
    out[6] = static_cast<Byte>(utc.tm_sec);
    out[7] = static_cast<Byte>(cs);
}

DecodedFrame::TimeCode CcsdsCodec::read_time_code(const Byte* in) {
    DecodedFrame::TimeCode tc;
    tc.year         = static_cast<std::uint16_t>((in[0] << 8) | in[1]);
    tc.month        = in[2];
    tc.day          = in[3];
    tc.hour         = in[4];
    tc.minute       = in[5];
    tc.second       = in[6];
    tc.centiseconds = in[7];
    return tc;
}

// ─── load() ─────────────────────────────────────────────────────────────────
static std::uint32_t parse_hex_word(const std::string& s) {
    std::uint32_t v = 0;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == 'x' || c == 'X') continue;
        if (c == '0' && v == 0) continue;
        if      (c >= '0' && c <= '9') v = (v << 4) | static_cast<std::uint32_t>(c - '0');
        else if (c >= 'A' && c <= 'F') v = (v << 4) | static_cast<std::uint32_t>(c - 'A' + 10);
        else if (c >= 'a' && c <= 'f') v = (v << 4) | static_cast<std::uint32_t>(c - 'a' + 10);
    }
    return v;
}

bool CcsdsCodec::load(const std::string& json_path) {
    std::ifstream f(json_path);
    if (!f) throw Error("ccsds: cannot open " + json_path);
    json j; f >> j;

    profile_name_ = j.value("profile_name", std::string("ccsds_default"));
    scid_         = j.at("constants").at("spacecraft_id").get<int>();
    vcid_         = j.at("constants").at("default_vcid").get<int>();
    gs_id_        = j.at("constants").value("ground_station_id", 0);
    enabled_by_default_ = j.value("enabled_by_default", true);

    sof_ = static_cast<std::uint16_t>(
        parse_hex_word(j.at("constants").at("sof").get<std::string>()));

    framing_mode_ = j.value("framing_mode", std::string("length_prefixed"));

    apid_by_sub_.clear();
    sub_by_apid_.clear();
    for (auto& [name, entry] : j.at("apid_map").items()) {
        auto sub_opt = subsystem_from_string(name);
        if (!sub_opt) throw Error("ccsds: unknown subsystem in apid_map: " + name);
        ApidEntry e{
            entry.at("apid").get<int>(),
            entry.value("sim_host", std::string("127.0.0.1")),
            entry.at("sim_port").get<int>()
        };
        apid_by_sub_[*sub_opt] = e;
        sub_by_apid_[e.apid]   = *sub_opt;
    }

    reset_counters();
    return true;
}

void CcsdsCodec::reset_counters() {
    std::lock_guard<std::mutex> lk(counter_mu_);
    frame_seq_ = 0;
    pkt_seq_by_apid_.clear();
}

std::uint16_t CcsdsCodec::next_frame_seq() {
    std::lock_guard<std::mutex> lk(counter_mu_);
    std::uint16_t v = frame_seq_;
    frame_seq_ = static_cast<std::uint8_t>((frame_seq_ + 1) & 0xFF);
    return v;
}

std::uint16_t CcsdsCodec::next_pkt_seq(int apid) {
    std::lock_guard<std::mutex> lk(counter_mu_);
    auto& c = pkt_seq_by_apid_[apid];
    std::uint16_t v = c;
    c = static_cast<std::uint16_t>((c + 1) & 0x3FFF);
    return v;
}

// ─── encode() ───────────────────────────────────────────────────────────────
Bytes CcsdsCodec::encode(Subsystem subsystem, CcsdsDirection dir,
                         std::uint16_t tc_id, const Bytes& tc_data) {
    auto it = apid_by_sub_.find(subsystem);
    if (it == apid_by_sub_.end())
        throw Error("ccsds::encode: no APID for subsystem");
    const int apid = it->second.apid;

    const std::size_t N        = tc_data.size();
    const std::size_t tf_hdr   = 5;
    const std::size_t pkt_hdr  = 6;
    const std::size_t gs_id_sz = 1;
    const std::size_t time_hdr = 8;
    const std::size_t anc_len  = 2;
    const std::size_t tc_id_sz = 2;
    const std::size_t tc_len_sz= 2;
    const std::size_t crc_sz   = 2;

    // Total frame body (excludes SOF). frame_length covers this.
    const std::size_t frame_body_sz =
        tf_hdr + pkt_hdr + gs_id_sz + time_hdr + anc_len
              + tc_id_sz + tc_len_sz + N + crc_sz;

    Bytes wire;
    wire.reserve(2 + frame_body_sz);

    // ── SOF ──
    wire.push_back(static_cast<Byte>((sof_ >> 8) & 0xFF));
    wire.push_back(static_cast<Byte>( sof_       & 0xFF));

    // Body
    std::size_t body_start = wire.size();
    wire.insert(wire.end(), frame_body_sz, Byte{0});
    Byte* body = wire.data() + body_start;

    // ── TC Transfer Frame Primary Header (40 bits) ──
    pack_bits(body, 0,  2,  0);                                  // tf version
    pack_bits(body, 2,  1,  0);                                  // bypass flag
    pack_bits(body, 3,  1,  0);                                  // ctrl command
    pack_bits(body, 4,  2,  0);                                  // reserved spare
    pack_bits(body, 6,  10, static_cast<std::uint64_t>(scid_));
    pack_bits(body, 16, 6,  static_cast<std::uint64_t>(vcid_));
    pack_bits(body, 22, 10, static_cast<std::uint64_t>(frame_body_sz - 1));
    pack_bits(body, 32, 8,  next_frame_seq());

    // ── Space Packet Primary Header (48 bits, starts byte 5) ──
    const std::size_t pkt_bit = 40;
    pack_bits(body, pkt_bit + 0,  3,  0);                        // pkt version
    pack_bits(body, pkt_bit + 3,  1,  static_cast<std::uint64_t>(dir));
    pack_bits(body, pkt_bit + 4,  1,  1);                        // sec hdr flag
    pack_bits(body, pkt_bit + 5,  11, static_cast<std::uint64_t>(apid));
    pack_bits(body, pkt_bit + 16, 2,  3);                        // seq flag = unsegmented
    pack_bits(body, pkt_bit + 18, 14, next_pkt_seq(apid));
    const std::size_t pkt_data_field_sz =
        gs_id_sz + time_hdr + anc_len + tc_id_sz + tc_len_sz + N;
    pack_bits(body, pkt_bit + 32, 16,
              static_cast<std::uint64_t>(pkt_data_field_sz - 1));

    // ── Packet Secondary Header ──
    std::size_t cursor = tf_hdr + pkt_hdr;            // byte 11
    body[cursor++] = static_cast<Byte>(gs_id_ & 0xFF);
    write_time_code(body + cursor);                   // bytes 12..19
    cursor += time_hdr;
    body[cursor++] = 0;                               // ancillary_data_length (high)
    body[cursor++] = 0;                               // ancillary_data_length (low)

    // ── User Data (TC ID, TC Len, TC Data) ──
    body[cursor++] = static_cast<Byte>((tc_id >> 8) & 0xFF);
    body[cursor++] = static_cast<Byte>( tc_id       & 0xFF);
    body[cursor++] = static_cast<Byte>((N >> 8) & 0xFF);
    body[cursor++] = static_cast<Byte>( N       & 0xFF);
    std::copy(tc_data.begin(), tc_data.end(), body + cursor);
    cursor += N;

    // ── CRC-16 over TF-hdr → TC-Data end ──
    std::uint16_t crc = crc16_ccitt(body, cursor);
    body[cursor++] = static_cast<Byte>((crc >> 8) & 0xFF);
    body[cursor++] = static_cast<Byte>( crc       & 0xFF);

    return wire;
}

// ─── decode() ───────────────────────────────────────────────────────────────
std::optional<DecodedFrame> CcsdsCodec::decode(const Bytes& wire) const {
    // Minimum frame: SOF(2) + TF(5) + Pkt(6) + GS(1) + Time(8) + Anc(2)
    //              + TC-ID(2) + TC-Len(2) + CRC(2) = 30 bytes
    if (wire.size() < 30) return std::nullopt;
    if (wire[0] != static_cast<Byte>((sof_ >> 8) & 0xFF)) return std::nullopt;
    if (wire[1] != static_cast<Byte>( sof_       & 0xFF)) return std::nullopt;

    const Byte* body  = wire.data() + 2;
    std::size_t bodyN = wire.size() - 2;             // body = everything after SOF

    DecodedFrame d;

    // ── TC Transfer Frame Primary Header ──
    d.tf_version_number    = static_cast<int>(unpack_bits(body, 0,  2));
    d.bypass_flag          = static_cast<int>(unpack_bits(body, 2,  1));
    d.control_command_flag = static_cast<int>(unpack_bits(body, 3,  1));
    d.spacecraft_id        = static_cast<int>(unpack_bits(body, 6,  10));
    d.virtual_channel_id   = static_cast<int>(unpack_bits(body, 16, 6));
    d.frame_length         = static_cast<int>(unpack_bits(body, 22, 10));
    d.frame_sequence_num   = static_cast<int>(unpack_bits(body, 32, 8));

    // ── Space Packet Primary Header ──
    const std::size_t pkt_bit = 40;
    d.packet_version      = static_cast<int>(unpack_bits(body, pkt_bit + 0,  3));
    d.direction           = (unpack_bits(body, pkt_bit + 3, 1) == 1)
                              ? CcsdsDirection::TC : CcsdsDirection::TM;
    d.secondary_header    = (unpack_bits(body, pkt_bit + 4, 1) == 1);
    d.apid                = static_cast<int>(unpack_bits(body, pkt_bit + 5,  11));
    d.sequence_flag       = static_cast<int>(unpack_bits(body, pkt_bit + 16, 2));
    d.packet_sequence_cnt = static_cast<int>(unpack_bits(body, pkt_bit + 18, 14));
    d.packet_data_length  = static_cast<int>(unpack_bits(body, pkt_bit + 32, 16));

    // ── Packet Secondary Header ──
    std::size_t cursor = 11;
    d.ground_station_id = body[cursor++];
    d.time_code = read_time_code(body + cursor);
    cursor += 8;
    d.ancillary_data_length = (body[cursor] << 8) | body[cursor + 1];
    cursor += 2;
    if (d.ancillary_data_length > 0 &&
        cursor + static_cast<std::size_t>(d.ancillary_data_length) <= bodyN - 6) {
        d.ancillary_data.assign(body + cursor,
                                body + cursor + d.ancillary_data_length);
        cursor += d.ancillary_data_length;
    }

    // ── User Data (TC ID, TC Len, TC Data) ──
    if (cursor + 4 + 2 > bodyN) return d;            // not enough for TC-ID+TC-Len+CRC
    d.tc_id  = static_cast<std::uint16_t>((body[cursor] << 8) | body[cursor + 1]);
    cursor += 2;
    d.tc_len = static_cast<std::uint16_t>((body[cursor] << 8) | body[cursor + 1]);
    cursor += 2;

    std::size_t tc_data_end = cursor + d.tc_len;
    if (tc_data_end + 2 > bodyN) {
        // Truncated — pull what we can but mark CRC bad
        tc_data_end = bodyN - 2;
        if (tc_data_end < cursor) tc_data_end = cursor;
    }
    d.tc_data.assign(body + cursor, body + tc_data_end);

    // ── CRC ──
    d.crc_received = static_cast<std::uint16_t>(
        (body[tc_data_end] << 8) | body[tc_data_end + 1]);
    d.crc_computed = crc16_ccitt(body, tc_data_end);
    d.crc_ok       = (d.crc_received == d.crc_computed);

    auto it = sub_by_apid_.find(d.apid);
    if (it != sub_by_apid_.end()) d.subsystem = it->second;

    return d;
}

bool CcsdsCodec::looks_like_ccsds(const Bytes& bytes) const {
    return bytes.size() >= 2
        && bytes[0] == static_cast<Byte>((sof_ >> 8) & 0xFF)
        && bytes[1] == static_cast<Byte>( sof_       & 0xFF);
}

std::optional<Subsystem> CcsdsCodec::apid_to_subsystem(int apid) const {
    auto it = sub_by_apid_.find(apid);
    if (it == sub_by_apid_.end()) return std::nullopt;
    return it->second;
}

std::optional<CcsdsCodec::Endpoint> CcsdsCodec::subsystem_endpoint(Subsystem s) const {
    auto it = apid_by_sub_.find(s);
    if (it == apid_by_sub_.end()) return std::nullopt;
    return Endpoint{ it->second.host, it->second.port };
}

std::string DecodedFrame::summary() const {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
        "[%s] SCID=0x%03X VCID=%d frame_seq=%d APID=0x%03X pkt_seq=%d "
        "TC_ID=0x%04X TC_Len=%d CRC=%s",
        direction == CcsdsDirection::TC ? "TC" : "TM",
        spacecraft_id, virtual_channel_id, frame_sequence_num,
        apid, packet_sequence_cnt,
        tc_id, tc_len,
        crc_ok ? "ok" : "BAD");
    return std::string(buf);
}

}  // namespace xdos::ccsds
