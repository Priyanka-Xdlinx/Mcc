// SPDX-License-Identifier: Apache-2.0
// XDOS Ground Station — CCSDS TM downlink codec (satellite→ground).
//
// Fixed-length 1432-byte frame per the TM sheet:
//
//   SOF(2 = FCA1)
//   TM Primary Header(6):    TFVN(2) SCID(10) RM-ID(4) MasterCnt(16) SubCnt(16)
//   Data Field Status(2):    SecHdr(1) Synch(1) PktOrder(1) SegLenID(2) FHP(11)
//   SPP Primary(6):          Ver(3) Type(1)=0 SecHdr(1) APID(11) SeqFlag(2)
//                            SeqCount(14) PktDataLen(16)
//   SPP Secondary(11):       GS-ID(8) TimeCode(64) RSV(16)
//   User Data(1403):         TM DATA(1280) | GS TC cmd echo(123)
//   FECF(2):                 CRC-16-CCITT over Primary Header … GS TC cmd end
//
// Identity (spacecraft_id) and the APID map are shared with the TC codec: this
// codec reads them from the referenced ccsds_tc.json so up/down never drift.
// TimeCode here is the 16-bit-millisecond CCS variant (the TC side uses 8-bit
// centiseconds) — the one deliberate secondary-header difference.
#pragma once

#include "common/types.hpp"

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

namespace xdos::ccsds {

// ─── DecodedTmFrame ─────────────────────────────────────────────────────────
struct DecodedTmFrame {
    // TM Primary Header
    int  tf_version_number  = 0;
    int  spacecraft_id      = 0;
    int  radio_module_id    = 0;
    int  master_frame_count = 0;
    int  sub_frame_count    = 0;

    // Data Field Status
    bool secondary_header_flag = false;
    int  synch_flag            = 0;
    int  packet_order_flag     = 0;
    int  segment_length_id     = 0;
    int  first_header_pointer  = 0;

    // SPP Primary
    int  packet_version      = 0;
    int  packet_type         = 0;      // 0 = TM
    bool spp_secondary_flag  = false;
    int  apid                = 0;
    int  sequence_flag       = 0;
    int  packet_sequence_cnt = 0;
    int  packet_data_length  = 0;

    // SPP Secondary
    std::uint8_t ground_station_id = 0;
    struct TimeCode {
        std::uint16_t year         = 0;   // full year (2000 + offset)
        std::uint8_t  month        = 0;
        std::uint8_t  day          = 0;
        std::uint8_t  hour         = 0;
        std::uint8_t  minute       = 0;
        std::uint8_t  second       = 0;
        std::uint16_t milliseconds = 0;   // 0..999
    } time_code;
    std::uint16_t rsv = 0;

    // User Data (payloads returned with trailing pad stripped by length where
    // possible; tm_data keeps full 1280 unless the caller trims)
    Bytes tm_data;     // 1280 B as received
    Bytes gs_tc_cmd;   // 123 B as received

    // Verification
    std::uint16_t crc_received = 0;
    std::uint16_t crc_computed = 0;
    bool          crc_ok       = false;

    std::optional<Subsystem> subsystem;   // resolved from APID

    std::string summary() const;
};

// ─── TM Codec ───────────────────────────────────────────────────────────────
class CcsdsTmCodec {
public:
    // Loads the TM profile AND the TC profile it references (for the shared
    // spacecraft_id + apid_map).  tm_json_path points at mib/ccsds_tm.json.
    bool load(const std::string& tm_json_path);

    // Build a 1432-byte downlink frame.
    //   subsystem  → APID lookup (idle_apid if not mapped)
    //   tm_data    → native subsystem TM bytes (zero-padded to 1280)
    //   gs_tc_echo → last TC forwarded to that subsystem (padded/truncated 123)
    //   rm_id      → radio-module id; <0 uses the profile default
    Bytes encode(Subsystem subsystem,
                 const Bytes& tm_data,
                 const Bytes& gs_tc_echo,
                 int rm_id = -1);

    // Build a frame with an EXPLICIT APID (not tied to a Subsystem enum).
    // Used for the combined rate-group housekeeping push (APID 600), which
    // carries several subsystems' data under one dedicated APID.
    Bytes encode_apid(int apid,
                      const Bytes& tm_data,
                      const Bytes& gs_tc_echo,
                      int rm_id = -1);

    std::optional<DecodedTmFrame> decode(const Bytes& wire_bytes) const;

    bool looks_like_tm(const Bytes& bytes) const;   // SOF == FCA1 && len ok

    std::optional<Subsystem> apid_to_subsystem(int apid) const;

    int  spacecraft_id()     const { return scid_; }
    int  rate_group_apid()   const { return rate_group_apid_; }
    int  ground_station_id() const { return gs_id_; }
    int  default_radio_module_id() const { return rm_default_; }
    int  frame_length()      const { return frame_len_; }
    bool enabled_by_default() const { return enabled_by_default_; }
    const std::string& profile_name() const { return profile_name_; }

    void reset_counters();

    // Peek the sub-frame count the NEXT encode_apid(apid, ...) will stamp,
    // without incrementing.  The rate-group engine keys its subframe slice
    // (slot = count % 16) off this so frame content and header stay aligned.
    std::uint16_t current_sub(int apid) const;

    static constexpr std::size_t kFrameLen  = 1432;
    static constexpr std::size_t kTmDataLen = 1280;
    static constexpr std::size_t kEchoLen   = 123;

private:
    std::string   profile_name_;
    int           scid_      = 0;
    int           rate_group_apid_ = 600;
    int           gs_id_     = 0;
    int           rm_default_ = 1;
    int           idle_apid_ = 2047;
    int           frame_len_ = static_cast<int>(kFrameLen);
    std::uint16_t sof_       = 0xFCA1;
    bool          enabled_by_default_ = true;

    std::map<Subsystem, int> apid_by_sub_;
    std::map<int, Subsystem> sub_by_apid_;

    mutable std::mutex           counter_mu_;
    std::uint16_t                master_cnt_ = 0;
    std::map<int, std::uint16_t> sub_cnt_by_apid_;
    std::map<int, std::uint16_t> pkt_seq_by_apid_;

    std::uint16_t next_master();
    std::uint16_t next_sub(int apid);
    std::uint16_t next_pkt_seq(int apid);

    static std::uint16_t crc16_ccitt(const Byte* data, std::size_t len);
    static void          write_time_code(Byte* out);   // 8 B, 16-bit ms
    static DecodedTmFrame::TimeCode read_time_code(const Byte* in);
};

}  // namespace xdos::ccsds
