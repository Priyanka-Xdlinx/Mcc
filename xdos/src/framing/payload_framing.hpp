// SPDX-License-Identifier: Apache-2.0
// XDOS Ground Station — Payload Server framing.
//
// Per the "Command and Data Format Specification" §1, a payload frame is a
// 24-bit big-endian header followed by the data field:
//
//   bits 23..21   Message Type   (3 bits)
//   bits 20..11   Command ID     (10 bits, 0..1023)
//   bits 10..0    Length         (11 bits, 0..2047 — data bytes only)
//   then          Data           (Length bytes)
//
// On the wire that is three bytes, most-significant first:
//
//   byte 0:  [ type(3) | cmd_id high 5 ]
//   byte 1:  [ cmd_id low 5 | length high 3 ]
//   byte 2:  [ length low 8 ]
//
// This frame is the *inner* payload, carried as CCSDS TC_Data under APID 5,
// exactly as the EPS base64 frame and the ADCS/GPS native frames are.  The
// CCSDS TC_ID field carries the Command ID.
//
// Note there is no CRC at this layer — integrity is the CCSDS wrapper's
// CRC-16 (§ccsds_codec.cpp).
#pragma once

#include "common/types.hpp"

#include <cstdint>

namespace xdos::framing::payload {

// ── Message Type (spec §2) ───────────────────────────────────────────────────
enum class MsgType : std::uint8_t {
    Telecommand   = 0,
    Telemetry     = 1,
    Heartbeat     = 2,
    Ephemeris     = 3,
    HealthMetrics = 4,
    Event         = 5,
    Reserved6     = 6,
    Reserved7     = 7,
};

constexpr std::size_t   kHeaderLen    = 3;
constexpr std::uint16_t kMaxCommandId = 0x3FF;   // 10 bits
constexpr std::uint16_t kMaxDataLen   = 0x7FF;   // 11 bits

// ── Command IDs (spec §3, §3.2, §4) ──────────────────────────────────────────
constexpr std::uint16_t kCmdContainerOn    = 545;   // Docker ON  — data: payload id
constexpr std::uint16_t kCmdContainerOff   = 546;   // Docker OFF — data: payload id
constexpr std::uint16_t kCmdPayloadSeq     = 600;   // Custom payload sequence
constexpr std::uint16_t kCmdCaptureMsi     = 601;   // Capture — MSI
constexpr std::uint16_t kCmdCaptureScanway = 602;   // Capture — SCANWAY
constexpr std::uint16_t kCmdHeartbeat      = 700;
constexpr std::uint16_t kCmdEvent          = 701;
constexpr std::uint16_t kCmdEphemeris      = 702;
constexpr std::uint16_t kCmdHealthMetrics  = 703;

// ── Payload IDs (data field of 545/546/600) ──────────────────────────────────
constexpr Byte kPayloadMsi     = 0;
constexpr Byte kPayloadScanway = 1;
constexpr Byte kPayloadSar     = 2;

// ── Acknowledgement ──────────────────────────────────────────────────────────
// The payload server does not return telemetry.  A command that completes
// successfully is acknowledged with a single character in the data field of an
// otherwise identical frame (same header, same Command ID).
constexpr Byte        kAckChar   = 'A';   // command performed
constexpr Byte        kNakChar   = 'N';   // command rejected / not performed
constexpr std::size_t kAckDataLen = 1;

// Custom Payload Sequence (600) field limits — spec §3.1.
constexpr std::size_t kMaxSeqNameLen = 16;
constexpr std::size_t kMaxSeqArgsLen = 128;

// Build a payload frame.  Returns an empty vector if command_id or the data
// length exceeds what the header's bit-fields can carry.
Bytes encode(MsgType type, std::uint16_t command_id, const Bytes& data);

struct Decoded {
    MsgType       type       = MsgType::Telecommand;
    std::uint16_t command_id = 0;
    std::uint16_t length     = 0;    // as declared in the header
    Bytes         data;              // as actually present in the frame
    bool          header_ok  = false;  // frame carried a full 3-byte header
    bool          length_ok  = false;  // data.size() == declared length
};

Decoded decode(const Bytes& frame);

// Total wire size of the frame whose header starts at `hdr` (>= 3 readable
// bytes).  Use this to drive length-prefixed reads on a TCP stream: read 3
// bytes, call this, then read the remainder.
std::size_t frame_size(const Byte* hdr);

// Build the data field for Custom Payload Sequence (600) — spec §3.1:
//   [payload id 1B][seq name len 1B][args len 1B][seq name][args]
// name/args are truncated to their spec maxima.
Bytes make_sequence_data(Byte payload_id, std::string_view seq_name,
                         std::string_view args);

// True when `d` is a well-formed acknowledgement: a single-character data
// field echoing the command that was sent.
bool is_ack(const Decoded& d);

const char* type_name(MsgType t);
const char* command_name(std::uint16_t command_id);
const char* payload_name(Byte payload_id);

} // namespace xdos::framing::payload
