// SPDX-License-Identifier: Apache-2.0
// XDOS Ground Station — Sensor Controller (Tracker) frame format.
//
// Per SENSOR_CONTROLLER_Tracker_FRAME_FORMAT_ENUMS:
//
//   [0xA5 0xAA] | [dest_id: 1] | [cmd_id: 1] | [len: 1] | [payload: len] | [0xBA]
//
//   dest_id   the addressed controller — 50 (0x32) for this board; comes from
//             the "address" field of the MIB node, not hard-coded here
//   cmd_id    the command id (128/129 for SET, 1/4/7 for GET); the same id
//             comes back on the reply
//   len       payload bytes only — it excludes SOF, dest, cmd, len and EOF
//
// There is no checksum at this layer: SOF, the length byte and EOF are the
// only integrity signals the format offers, so decode() verifies all three.
//
// Requests and replies share this one shape.  A NACK is not structurally
// distinct — it is a reply whose payload is (error_event, error_byte) — so
// telling ACK from NACK is the caller's job, from the payload.
#pragma once

#include "common/types.hpp"

namespace xdos::framing::sensor {

constexpr Byte kStart0  = 0xA5;
constexpr Byte kStart1  = 0xAA;
constexpr Byte kEnd     = 0xBA;
constexpr Byte kDestId  = 50;      // 0x32 — fallback when the MIB has no node
constexpr int  kMaxPayload = 255;  // the length byte is one octet
constexpr int  kOverhead   = 6;    // SOF(2) + dest + cmd + len + EOF

// Build a complete frame around (dest_id, cmd_id, payload).
// A payload longer than kMaxPayload is truncated — it cannot be described by
// the single length byte.
Bytes encode(Byte dest_id, Byte cmd_id, const Bytes& payload);

struct Decoded {
    Byte  dest_id = 0;
    Byte  cmd_id  = 0;
    Bytes data;                 // payload bytes, exactly `len` of them
    bool  framed_ok = false;    // SOF found, length consistent, EOF in place
};

// Decode one frame (tolerates leading garbage before the sync pair).
Decoded decode(const Bytes& frame);

// Total frame size given the length byte, for trimming zero-padded TM fields.
inline std::size_t frame_size(Byte len) {
    return static_cast<std::size_t>(len) + kOverhead;
}

} // namespace xdos::framing::sensor
