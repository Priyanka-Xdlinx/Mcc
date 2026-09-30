// SPDX-License-Identifier: Apache-2.0
// XDOS Ground Station — ADCS CubeSpace UART framing.
//
// Frame layout:
//
//   [0x1F 0x7F] | escaped(payload) | [0x1F 0xFF]
//
// where `payload = msg_id || data`, and any 0x1F byte inside the payload
// is escaped as 0x1F 0x1F.  The receiver knows it has reached the end of
// the frame when it sees 0x1F 0xFF (the trailer can only occur as the
// real frame end because every other 0x1F is doubled).
#pragma once

#include "common/types.hpp"

namespace xdos::framing::adcs {

constexpr Byte kEsc = 0x1F;
constexpr Byte kSom = 0x7F;
constexpr Byte kEom = 0xFF;

struct Decoded {
    Byte  msg_id    = 0;
    Bytes data;
    bool  framed_ok = false;
};

// Bare `msg_id || data` — no delimiters and no escaping.
//
// This is what the flight OBC wants in the ADCS TC data field: it builds the
// whole CubeSpace frame itself (delimiters *and* the 0x1F doubling) before
// putting it on the ADCS UART.  Escaping here as well would reach the ADCS
// double-escaped, so the ground hands over the raw body and lets the OBC
// frame it.  Talking straight to an ADCS, use encode() instead.
Bytes raw_body(Byte msg_id, const Bytes& data);

// Split a bare `msg_id || data` body — the inverse of raw_body(), for the TM
// direction when the OBC has already stripped the CubeSpace framing.
Decoded split_raw_body(const Bytes& body);

// Build a complete CubeSpace UART frame around (msg_id, data):
//   1F 7F | escaped(msg_id || data) | 1F FF
Bytes encode(Byte msg_id, const Bytes& data);

// Decode a CubeSpace UART frame.  Returns framed_ok = false on bad framing.
Decoded decode(const Bytes& frame);

} // namespace xdos::framing::adcs
