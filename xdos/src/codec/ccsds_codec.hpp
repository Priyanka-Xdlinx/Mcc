// SPDX-License-Identifier: Apache-2.0
// XDOS Ground Station — CCSDS TC/TM codec (v2 per sheet).
//
// Frame layout on the wire (no EOF, GS-ID + ms in secondary header,
// user_data split into TC ID + TC Len + TC Data):
//
//   SOF(2) | TF-hdr(5) | Pkt-hdr(6) | GS-ID(1) | TimeCode(8) | AncLen(2)
//          | TC-ID(2)  | TC-Len(2)  | TC-Data(N) | CRC(2)
//
//   CRC coverage: TF-hdr start  →  TC-Data end
//
// Profile-driven: layout loaded from mib/ccsds_tc.json.
#pragma once

#include "common/types.hpp"

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

namespace xdos::ccsds {

enum class CcsdsDirection : int { TC = 1, TM = 0 };

struct Error : std::runtime_error { using std::runtime_error::runtime_error; };

// ─── DecodedFrame ──────────────────────────────────────────────────────────
struct DecodedFrame {
    // TC Transfer Frame Primary Header
    int  tf_version_number    = 0;
    int  bypass_flag          = 0;
    int  control_command_flag = 0;
    int  spacecraft_id        = 0;
    int  virtual_channel_id   = 0;
    int  frame_length         = 0;
    int  frame_sequence_num   = 0;

    // Space Packet Primary Header
    int            packet_version       = 0;
    CcsdsDirection direction            = CcsdsDirection::TC;
    bool           secondary_header     = false;
    int            apid                 = 0;
    int            sequence_flag        = 0;
    int            packet_sequence_cnt  = 0;
    int            packet_data_length   = 0;

    // Packet Secondary Header
    std::uint8_t  ground_station_id    = 0;
    struct TimeCode {
        std::uint16_t year         = 0;
        std::uint8_t  month        = 0;
        std::uint8_t  day          = 0;
        std::uint8_t  hour         = 0;
        std::uint8_t  minute       = 0;
        std::uint8_t  second       = 0;
        std::uint8_t  centiseconds = 0;   // 0..99, multiply by 10 for ms
    } time_code;
    int    ancillary_data_length = 0;
    Bytes  ancillary_data;

    // User Data (Option B — split into 3 explicit fields)
    std::uint16_t tc_id   = 0;
    std::uint16_t tc_len  = 0;
    Bytes         tc_data;

    // Verification
    std::uint16_t crc_received = 0;
    std::uint16_t crc_computed = 0;
    bool          crc_ok       = false;

    // Subsystem resolved from APID
    std::optional<Subsystem> subsystem;

    std::string summary() const;
};

// ─── Codec ─────────────────────────────────────────────────────────────────
class CcsdsCodec {
public:
    bool load(const std::string& json_path);

    // Build a CCSDS frame given:
    //   subsystem   → APID lookup
    //   dir         → TC or TM (sets packet_type bit)
    //   tc_id       → subsystem MIB wire-id
    //   tc_data     → payload parameters only (NOT native framing)
    Bytes encode(Subsystem subsystem,
                 CcsdsDirection dir,
                 std::uint16_t tc_id,
                 const Bytes& tc_data);

    std::optional<DecodedFrame> decode(const Bytes& wire_bytes) const;

    bool looks_like_ccsds(const Bytes& bytes) const;

    std::optional<Subsystem> apid_to_subsystem(int apid) const;

    struct Endpoint { std::string host; int port; };
    std::optional<Endpoint> subsystem_endpoint(Subsystem s) const;

    int  spacecraft_id()      const { return scid_; }
    int  default_vcid()       const { return vcid_; }
    int  ground_station_id()  const { return gs_id_; }
    bool enabled_by_default() const { return enabled_by_default_; }
    const std::string& profile_name() const { return profile_name_; }

    void reset_counters();

private:
    std::string  profile_name_;
    int          scid_  = 0;
    int          vcid_  = 0;
    int          gs_id_ = 0;
    std::uint16_t sof_  = 0x1ACF;
    bool         enabled_by_default_ = true;
    std::string  framing_mode_ = "length_prefixed";

    struct ApidEntry { int apid; std::string host; int port; };
    std::map<Subsystem, ApidEntry> apid_by_sub_;
    std::map<int, Subsystem>       sub_by_apid_;

    mutable std::mutex                counter_mu_;
    std::uint8_t                      frame_seq_ = 0;
    std::map<int, std::uint16_t>      pkt_seq_by_apid_;

    std::uint16_t next_frame_seq();
    std::uint16_t next_pkt_seq(int apid);

    static std::uint16_t crc16_ccitt(const Byte* data, std::size_t len);
    static void          write_time_code(Byte* out);
    static DecodedFrame::TimeCode read_time_code(const Byte* in);
};

}  // namespace xdos::ccsds