// SPDX-License-Identifier: Apache-2.0
#include "codec/ccsds_tm_codec.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace xdos::ccsds {

using nlohmann::json;

// ─── MSB-first bit writer / reader ──────────────────────────────────────────
namespace {
class BitWriter {
public:
    explicit BitWriter(Bytes& out) : out_(out) {}
    void put(std::uint64_t value, int nbits) {
        for (int i = nbits - 1; i >= 0; --i) {
            if (bit_ == 0) out_.push_back(0);
            std::uint8_t b = (value >> i) & 1u;
            out_.back() |= static_cast<std::uint8_t>(b << (7 - bit_));
            bit_ = (bit_ + 1) & 7;
        }
    }
private:
    Bytes& out_;
    int    bit_ = 0;
};

class BitReader {
public:
    BitReader(const Byte* data, std::size_t len) : d_(data), len_(len) {}
    std::uint64_t get(int nbits) {
        std::uint64_t v = 0;
        for (int i = 0; i < nbits; ++i) {
            std::uint8_t bit = 0;
            if (byte_ < len_)
                bit = (d_[byte_] >> (7 - bit_)) & 1u;
            v = (v << 1) | bit;
            if (++bit_ == 8) { bit_ = 0; ++byte_; }
        }
        return v;
    }
private:
    const Byte*  d_;
    std::size_t  len_;
    std::size_t  byte_ = 0;
    int          bit_  = 0;
};

std::string sub_name(Subsystem s) { return std::string(to_string(s)); }
}  // namespace

// ─── CRC-16-CCITT (identical to the TC codec) ───────────────────────────────
std::uint16_t CcsdsTmCodec::crc16_ccitt(const Byte* data, std::size_t len) {
    std::uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= static_cast<std::uint16_t>(data[i]) << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<std::uint16_t>(crc << 1);
    }
    return crc;
}

// ─── CCS time code, 8 bytes, 16-bit millisecond variant ─────────────────────
//   yr-offset(1, base 2000) | Mo(1) | DOM(1) | h(1) | m(1) | s(1) | ms(2 BE)
void CcsdsTmCodec::write_time_code(Byte* out) {
    auto now  = std::chrono::system_clock::now();
    auto secs = std::chrono::system_clock::to_time_t(now);
    auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()).count() % 1000;
    std::tm tm_utc{};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &secs);
#else
    gmtime_r(&secs, &tm_utc);
#endif
    int year = tm_utc.tm_year + 1900;
    out[0] = static_cast<Byte>((year - 2000) & 0xFF);
    out[1] = static_cast<Byte>(tm_utc.tm_mon + 1);
    out[2] = static_cast<Byte>(tm_utc.tm_mday);
    out[3] = static_cast<Byte>(tm_utc.tm_hour);
    out[4] = static_cast<Byte>(tm_utc.tm_min);
    out[5] = static_cast<Byte>(tm_utc.tm_sec);
    out[6] = static_cast<Byte>((ms >> 8) & 0xFF);
    out[7] = static_cast<Byte>(ms & 0xFF);
}

DecodedTmFrame::TimeCode CcsdsTmCodec::read_time_code(const Byte* in) {
    DecodedTmFrame::TimeCode t;
    t.year         = static_cast<std::uint16_t>(2000 + in[0]);
    t.month        = in[1];
    t.day          = in[2];
    t.hour         = in[3];
    t.minute       = in[4];
    t.second       = in[5];
    t.milliseconds = static_cast<std::uint16_t>((in[6] << 8) | in[7]);
    return t;
}

// ─── profile load (also pulls identity from the referenced TC profile) ──────
bool CcsdsTmCodec::load(const std::string& tm_json_path) {
    std::ifstream f(tm_json_path);
    if (!f) return false;
    json j; f >> j;

    profile_name_ = j.value("profile_name", "xdos_ccsds_tm");
    enabled_by_default_ = j.value("enabled_by_default", true);

    const auto& c = j.at("constants");
    auto hex16 = [](const std::string& s) {
        return static_cast<std::uint16_t>(std::stoul(s, nullptr, 16));
    };
    sof_        = hex16(c.value("sof", std::string("FCA1")));
    frame_len_  = c.value("frame_length_bytes", 1432);
    rm_default_ = c.value("default_radio_module_id", 1);
    gs_id_      = c.value("ground_station_id", 0);
    idle_apid_  = c.value("idle_apid", 2047);

    // Identity + APID map come from the referenced TC profile (single source).
    std::string ref = j.value("references_profile", std::string("ccsds_tc.json"));
    std::filesystem::path base = std::filesystem::path(tm_json_path).parent_path();
    std::ifstream rf(base / ref);
    if (!rf) return false;
    json tc; rf >> tc;
    scid_ = tc.at("constants").value("spacecraft_id", 0);
    rate_group_apid_ = tc.value("rate_group_apid", 600);
    for (auto it = tc.at("apid_map").begin(); it != tc.at("apid_map").end(); ++it) {
        Subsystem s;
        const std::string& k = it.key();
        if      (k == "ADCS")    s = Subsystem::ADCS;
        else if (k == "EPS")     s = Subsystem::EPS;
        else if (k == "GPS")     s = Subsystem::GPS;
        else if (k == "Payload") s = Subsystem::Payload;
        else if (k == "XLINK" || k == "XLINK-S" || k == "XLink") s = Subsystem::XLINK;
        else if (k == "UHF")     s = Subsystem::UHF;
        else if (k == "SENSOR")  s = Subsystem::SENSOR;
        else continue;
        int apid = it.value().at("apid").get<int>();
        apid_by_sub_[s]   = apid;
        sub_by_apid_[apid] = s;
    }
    return true;
}

std::optional<Subsystem> CcsdsTmCodec::apid_to_subsystem(int apid) const {
    auto it = sub_by_apid_.find(apid);
    if (it == sub_by_apid_.end()) return std::nullopt;
    return it->second;
}

std::uint16_t CcsdsTmCodec::next_master() {
    std::lock_guard<std::mutex> g(counter_mu_);
    return master_cnt_++;
}
std::uint16_t CcsdsTmCodec::next_sub(int apid) {
    std::lock_guard<std::mutex> g(counter_mu_);
    return sub_cnt_by_apid_[apid]++;
}
std::uint16_t CcsdsTmCodec::next_pkt_seq(int apid) {
    std::lock_guard<std::mutex> g(counter_mu_);
    return static_cast<std::uint16_t>(pkt_seq_by_apid_[apid]++ & 0x3FFF);
}
void CcsdsTmCodec::reset_counters() {
    std::lock_guard<std::mutex> g(counter_mu_);
    master_cnt_ = 0; sub_cnt_by_apid_.clear(); pkt_seq_by_apid_.clear();
}
std::uint16_t CcsdsTmCodec::current_sub(int apid) const {
    std::lock_guard<std::mutex> g(counter_mu_);
    auto it = sub_cnt_by_apid_.find(apid);
    return it == sub_cnt_by_apid_.end() ? 0 : it->second;
}

// ─── encode ─────────────────────────────────────────────────────────────────
Bytes CcsdsTmCodec::encode(Subsystem subsystem, const Bytes& tm_data,
                           const Bytes& gs_tc_echo, int rm_id) {
    int apid = idle_apid_;
    if (auto it = apid_by_sub_.find(subsystem); it != apid_by_sub_.end())
        apid = it->second;
    return encode_apid(apid, tm_data, gs_tc_echo, rm_id);
}

Bytes CcsdsTmCodec::encode_apid(int apid, const Bytes& tm_data,
                                const Bytes& gs_tc_echo, int rm_id) {
    const int rm = (rm_id < 0) ? rm_default_ : (rm_id & 0x0F);

    // Fixed field sizes.
    const std::size_t TM_DATA = kTmDataLen, ECHO = kEchoLen;
    // pkt_data_length = (secondary header + user data) octets − 1
    //   secondary header = GS-ID(1) + time(8) + RSV(2) = 11
    //   user data        = 1280 + 123 = 1403
    const int pkt_data_len = static_cast<int>(11 + TM_DATA + ECHO) - 1;  // 1413

    Bytes body;                       // everything after SOF, before CRC
    body.reserve(frame_len_);
    BitWriter bw(body);

    // TM Primary Header (8 B)
    bw.put(0, 2);                          // TFVN
    bw.put(scid_ & 0x3FF, 10);            // SCID
    bw.put(rm, 4);                         // RM-ID (VCID slot)
    bw.put(next_master(), 16);             // Master frame count
    bw.put(next_sub(apid), 16);            // Sub frame count (per-APID)

    // Data Field Status (2 B)
    bw.put(1, 1);                          // SecHdr present
    bw.put(0, 1);                          // Synch
    bw.put(0, 1);                          // Packet order
    bw.put(3, 2);                          // Segment length id (11)
    bw.put(0, 11);                         // First header pointer

    // SPP Primary (6 B)
    bw.put(0, 3);                          // Version
    bw.put(0, 1);                          // Type = TM
    bw.put(1, 1);                          // Secondary header flag
    bw.put(apid & 0x7FF, 11);              // APID
    bw.put(3, 2);                          // Sequence flag (unsegmented)
    bw.put(next_pkt_seq(apid), 14);        // Packet sequence count
    bw.put(pkt_data_len & 0xFFFF, 16);     // Packet data length

    // SPP Secondary (11 B): GS-ID(1) + time(8) + RSV(2)
    bw.put(gs_id_ & 0xFF, 8);
    {
        std::array<Byte, 8> tc{};
        write_time_code(tc.data());
        for (Byte b : tc) bw.put(b, 8);
    }
    bw.put(0, 16);                         // RSV

    // At this point body is byte-aligned (6+2+6+11 = 25 B). User data follows.
    Bytes user;
    user.reserve(TM_DATA + ECHO);
    user.insert(user.end(), tm_data.begin(),
                tm_data.begin() + std::min(tm_data.size(), TM_DATA));
    user.resize(TM_DATA, 0x00);            // zero-pad TM DATA
    user.insert(user.end(), gs_tc_echo.begin(),
                gs_tc_echo.begin() + std::min(gs_tc_echo.size(), ECHO));
    user.resize(TM_DATA + ECHO, 0x00);     // zero-pad echo
    body.insert(body.end(), user.begin(), user.end());

    // Assemble: SOF | body | CRC(body)
    Bytes frame;
    frame.reserve(frame_len_);
    frame.push_back(static_cast<Byte>((sof_ >> 8) & 0xFF));
    frame.push_back(static_cast<Byte>(sof_ & 0xFF));
    frame.insert(frame.end(), body.begin(), body.end());
    std::uint16_t crc = crc16_ccitt(body.data(), body.size());
    frame.push_back(static_cast<Byte>((crc >> 8) & 0xFF));
    frame.push_back(static_cast<Byte>(crc & 0xFF));
    return frame;
}

// ─── decode ─────────────────────────────────────────────────────────────────
bool CcsdsTmCodec::looks_like_tm(const Bytes& b) const {
    return b.size() >= 4 &&
           b[0] == ((sof_ >> 8) & 0xFF) && b[1] == (sof_ & 0xFF);
}

std::optional<DecodedTmFrame> CcsdsTmCodec::decode(const Bytes& wire) const {
    if (wire.size() < static_cast<std::size_t>(frame_len_)) return std::nullopt;
    if (!looks_like_tm(wire)) return std::nullopt;

    DecodedTmFrame d;
    const Byte* body = wire.data() + 2;               // after SOF
    const std::size_t body_len = frame_len_ - 4;      // minus SOF and CRC
    BitReader br(body, body_len);

    d.tf_version_number  = static_cast<int>(br.get(2));
    d.spacecraft_id      = static_cast<int>(br.get(10));
    d.radio_module_id    = static_cast<int>(br.get(4));
    d.master_frame_count = static_cast<int>(br.get(16));
    d.sub_frame_count    = static_cast<int>(br.get(16));

    d.secondary_header_flag = br.get(1) != 0;
    d.synch_flag            = static_cast<int>(br.get(1));
    d.packet_order_flag     = static_cast<int>(br.get(1));
    d.segment_length_id     = static_cast<int>(br.get(2));
    d.first_header_pointer  = static_cast<int>(br.get(11));

    d.packet_version      = static_cast<int>(br.get(3));
    d.packet_type         = static_cast<int>(br.get(1));
    d.spp_secondary_flag  = br.get(1) != 0;
    d.apid                = static_cast<int>(br.get(11));
    d.sequence_flag       = static_cast<int>(br.get(2));
    d.packet_sequence_cnt = static_cast<int>(br.get(14));
    d.packet_data_length  = static_cast<int>(br.get(16));

    d.ground_station_id = static_cast<std::uint8_t>(br.get(8));
    {
        std::array<Byte, 8> tc{};
        for (auto& b : tc) b = static_cast<Byte>(br.get(8));
        d.time_code = read_time_code(tc.data());
    }
    d.rsv = static_cast<std::uint16_t>(br.get(16));

    // User data is byte-aligned at offset 25 within body (SOF excluded).
    const std::size_t user_off = 25;
    d.tm_data.assign(body + user_off, body + user_off + kTmDataLen);
    d.gs_tc_cmd.assign(body + user_off + kTmDataLen,
                       body + user_off + kTmDataLen + kEchoLen);

    d.crc_received = static_cast<std::uint16_t>(
        (wire[frame_len_ - 2] << 8) | wire[frame_len_ - 1]);
    d.crc_computed = crc16_ccitt(body, body_len);
    d.crc_ok       = (d.crc_received == d.crc_computed);
    d.subsystem    = apid_to_subsystem(d.apid);
    return d;
}

// ─── summary ────────────────────────────────────────────────────────────────
std::string DecodedTmFrame::summary() const {
    std::ostringstream os;
    os << "TM SCID=" << spacecraft_id << " RM=" << radio_module_id
       << " MFC=" << master_frame_count << " SFC=" << sub_frame_count
       << " APID=" << apid
       << (subsystem ? " (" + sub_name(*subsystem) + ")" : "")
       << " seq=" << packet_sequence_cnt
       << " time=" << time_code.year << "-"
       << static_cast<int>(time_code.month) << "-"
       << static_cast<int>(time_code.day) << " "
       << static_cast<int>(time_code.hour) << ":"
       << static_cast<int>(time_code.minute) << ":"
       << static_cast<int>(time_code.second) << "."
       << time_code.milliseconds
       << " CRC=" << (crc_ok ? "OK" : "BAD");
    return os.str();
}

}  // namespace xdos::ccsds
