// SPDX-License-Identifier: Apache-2.0
// XDOS Ground Station — Drishti SAT UHF (OpenLST) ESP UART framing.
//
// Per SAT_UHF_Firmware open-lst (common/uart.h, common/commands.h):
//
//   [0x22 0x69] | [len: 1 byte] | [payload: len bytes, len <= 251]
//
//   payload (little-endian):
//     off  bytes  field
//      0   2      hwid       (radio hardware id; 0xFFFF = HWID_LOCAL,
//                             accepted by every radio)
//      2   2      seqnum     (echoed verbatim in the reply)
//      4   1      system     (1 = MSG_TYPE_RADIO_IN / _OUT)
//      5   1      command    (opcode, e.g. 0x17 GET_TELEM)
//      6.. n      data       (0..245 bytes)
//
// No CRC at this layer — the UART link relies on the framing alone
// (RF-side integrity is handled by the CC1110 packet engine).
// Max frame on the wire: 2 + 1 + 251 = 254 bytes.
#pragma once

#include "common/types.hpp"

namespace xdos::framing::uhf {

constexpr Byte kStart0 = 0x22;
constexpr Byte kStart1 = 0x69;
constexpr int  kMaxPayload = 251;                 // ESP_MAX_PAYLOAD
constexpr int  kHeaderSize = 6;                   // hwid+seqnum+system+command
constexpr int  kMaxData    = kMaxPayload - kHeaderSize;   // 245
constexpr std::uint16_t kHwidLocal = 0xFFFF;      // accepted by any radio
constexpr Byte kSystemRadio = 1;                  // MSG_TYPE_RADIO_IN / _OUT

// Build a full ESP frame around (command opcode, data).
Bytes encode(Byte command, const Bytes& data,
             std::uint16_t hwid = kHwidLocal,
             std::uint16_t seqnum = 0,
             Byte system = kSystemRadio);

struct Decoded {
    std::uint16_t hwid    = 0;
    std::uint16_t seqnum  = 0;
    Byte          system  = 0;
    Byte          command = 0;      // reply opcode (ACK/NACK/TELEM/…)
    Bytes         data;             // payload after the 6-byte header
    bool          framed_ok = false;
};

// Decode one ESP frame (tolerates leading garbage before the sync bytes).
Decoded decode(const Bytes& frame);

} // namespace xdos::framing::uhf
