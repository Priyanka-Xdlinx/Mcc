// SPDX-License-Identifier: Apache-2.0
// XDOS Ground Station — EPS RS485 / I2C framing.
//
// Per "Msg Structure" sheet of the NL EPS Communication Protocol:
//
//   1.  Build the message array (uint8_t[]):
//         [0]   dest_address  (1 - 14 = boards, 201/202 = OBC, 0 = broadcast)
//         [1]   src_address
//         [2]   message_id
//         [3..n] data
//         [n+1]  CRC-8 (poly 0x07, init 0x00, over indices 0..n)
//
//   2.  Base64-encode that whole array.
//
//   3.  Transmit  on the line as:  0x00  +  base64 bytes  +  0x01
//
// Device addresses (from the spec):  OBC1=201, OBC2=202,
// PDU 1..8 = 1..8, BMS 1..3 = 9..11, MPPT 1..3 = 12..14, broadcast = 0.
#pragma once

#include "common/types.hpp"

namespace xdos::framing::eps {

constexpr Byte kStartFrame = 0x00;
constexpr Byte kEndFrame   = 0x01;
constexpr Byte kObc1Addr   = 201;
constexpr Byte kObc2Addr   = 202;
constexpr Byte kBroadcastAddr = 0;

// Build a full EPS line frame around (dest, msg_id, data).
// src defaults to OBC1 — the ground station spoofs the OBC's source address.
Bytes encode(Byte dest_addr, Byte msg_id, const Bytes& data,
             Byte src_addr = kObc1Addr);

struct Decoded {
    Byte  dest_addr = 0;
    Byte  src_addr  = 0;
    Byte  msg_id    = 0;
    Bytes data;
    Byte  crc       = 0;
    bool  crc_ok    = false;
    bool  framed_ok = false;        // frame boundaries were recognised
    bool  wrapped   = false;        // saw the 0x00 .. 0x01 line wrapper
    bool  raw_array = false;        // bare message array, no Base64 layer
};

// Decode an EPS reply in whichever of the two shapes it arrives in:
//
//   wrapped    0x00 + Base64(array) + 0x01   — the RS485 line format, which
//              the bus and the OBC simulator both pass through verbatim
//   raw array  dest|src|msg_id|data|crc8     — the flight OBC un-Base64s the
//              line frame before handing the bytes up, so the wrapper and the
//              Base64 layer are already gone
//
// Base64 text is entirely printable, so one non-Base64 byte tells the two
// apart.  `wrapped` / `raw_array` report which shape was seen; `framed_ok`
// means the boundaries were recognised either way.  A bare Base64 string with
// no wrapper still decodes (wrapped = false, raw_array = false).
Decoded decode(const Bytes& frame);

// Internal helpers exposed for tests:
std::string base64_encode(const Bytes& in);
std::optional<Bytes> base64_decode(std::string_view in);

} // namespace xdos::framing::eps
