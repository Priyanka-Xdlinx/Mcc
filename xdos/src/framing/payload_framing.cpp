// SPDX-License-Identifier: Apache-2.0
#include "framing/payload_framing.hpp"

#include <algorithm>

namespace xdos::framing::payload {

// ─── encode / decode ─────────────────────────────────────────────────────────
Bytes encode(MsgType type, std::uint16_t command_id, const Bytes& data) {
    if (command_id > kMaxCommandId)    return {};
    if (data.size() > kMaxDataLen)     return {};

    const auto len = static_cast<std::uint32_t>(data.size());
    std::uint32_t header = 0;
    header |= (static_cast<std::uint32_t>(type) & 0x7u) << 21;
    header |= (static_cast<std::uint32_t>(command_id) & 0x3FFu) << 11;
    header |= (len & 0x7FFu);

    Bytes out;
    out.reserve(kHeaderLen + data.size());
    out.push_back(static_cast<Byte>((header >> 16) & 0xFF));
    out.push_back(static_cast<Byte>((header >>  8) & 0xFF));
    out.push_back(static_cast<Byte>( header        & 0xFF));
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

Decoded decode(const Bytes& frame) {
    Decoded d{};
    if (frame.size() < kHeaderLen) return d;
    d.header_ok = true;

    d.type       = static_cast<MsgType>((frame[0] >> 5) & 0x07);
    d.command_id = static_cast<std::uint16_t>(
                       ((frame[0] & 0x1F) << 5) | ((frame[1] >> 3) & 0x1F));
    d.length     = static_cast<std::uint16_t>(
                       ((frame[1] & 0x07) << 8) | frame[2]);

    d.data.assign(frame.begin() + kHeaderLen, frame.end());
    d.length_ok = (d.data.size() == d.length);
    return d;
}

std::size_t frame_size(const Byte* hdr) {
    const std::size_t len = ((hdr[1] & 0x07u) << 8) | hdr[2];
    return kHeaderLen + len;
}

// ─── Custom Payload Sequence (600) data field — spec §3.1 ────────────────────
Bytes make_sequence_data(Byte payload_id, std::string_view seq_name,
                         std::string_view args) {
    seq_name = seq_name.substr(0, std::min(seq_name.size(), kMaxSeqNameLen));
    args     = args.substr(0, std::min(args.size(), kMaxSeqArgsLen));

    Bytes d;
    d.reserve(3 + seq_name.size() + args.size());
    d.push_back(payload_id);
    d.push_back(static_cast<Byte>(seq_name.size()));
    d.push_back(static_cast<Byte>(args.size()));
    for (char c : seq_name) d.push_back(static_cast<Byte>(c));
    for (char c : args)     d.push_back(static_cast<Byte>(c));
    return d;
}

// ─── acknowledgement ─────────────────────────────────────────────────────────
bool is_ack(const Decoded& d) {
    return d.header_ok && d.length_ok && d.data.size() == kAckDataLen;
}

// ─── naming helpers ──────────────────────────────────────────────────────────
const char* type_name(MsgType t) {
    switch (t) {
        case MsgType::Telecommand:   return "Telecommand";
        case MsgType::Telemetry:     return "Telemetry";
        case MsgType::Heartbeat:     return "Heartbeat";
        case MsgType::Ephemeris:     return "Ephemeris";
        case MsgType::HealthMetrics: return "Health Metrics";
        case MsgType::Event:         return "Event";
        case MsgType::Reserved6:     return "Reserved (6)";
        case MsgType::Reserved7:     return "Reserved (7)";
    }
    return "unknown";
}

const char* command_name(std::uint16_t command_id) {
    switch (command_id) {
        case kCmdContainerOn:    return "Container Operation (ON)";
        case kCmdContainerOff:   return "Container Operation (OFF)";
        case kCmdPayloadSeq:     return "Custom Payload Sequence";
        case kCmdCaptureMsi:     return "Capture (MSI)";
        case kCmdCaptureScanway: return "Capture (SCANWAY)";
        case kCmdHeartbeat:      return "Heartbeat";
        case kCmdEvent:          return "Event";
        case kCmdEphemeris:      return "Ephemeris";
        case kCmdHealthMetrics:  return "Health Metrics";
        default:                 return "unknown command";
    }
}

const char* payload_name(Byte payload_id) {
    switch (payload_id) {
        case kPayloadMsi:     return "MSI";
        case kPayloadScanway: return "SCANWAY";
        case kPayloadSar:     return "SAR";
        default:              return "unknown payload";
    }
}

} // namespace xdos::framing::payload
