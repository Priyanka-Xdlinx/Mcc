// SPDX-License-Identifier: Apache-2.0
// XLink Service Server codec.
//
// Wire format (from XLink_Service_Server_V1.8.1):
//
//   Offset  Size   Field
//   ------  ----   -----
//    0      16     product_key[4]   little-endian 128-bit key
//   16       4     service_event    LE U32:  base_id | flag(GET/SET/ACK/NAK)
//   20      N      data             LE bit-packed struct, U32-word-aligned
//
// The data block is described in the MIB as bit-offset parameters (like ADCS).
// The one XLink-specific quirk: fields NEVER span a U32 boundary — so we pack
// and unpack in whole 32-bit words, not free-form bit streams.

#pragma once
#include "mib/mib.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <nlohmann/json.hpp>

namespace xdos::xlink::codec {

using Bytes = std::vector<std::uint8_t>;
using ProductKey = std::array<std::uint8_t, 16>;

// Service event flags — top nibble of service_event word.
constexpr std::uint32_t FLAG_GET = 0x10000000;
constexpr std::uint32_t FLAG_SET = 0x20000000;
constexpr std::uint32_t FLAG_ACK = 0x40000000;
constexpr std::uint32_t FLAG_NAK = 0x80000000;
constexpr std::uint32_t FLAG_MASK = 0xF0000000;

// Base event ID (low 28 bits) — everything except the flag nibble.
inline std::uint32_t event_base(std::uint32_t se) { return se & ~FLAG_MASK; }
inline std::uint32_t event_flags(std::uint32_t se) { return se & FLAG_MASK; }

// Parse a product key from the ASCII "aabbccdd-eeff..." form used in the
// key file.  Ignores dashes and whitespace.  Returns nullopt on bad input.
std::optional<ProductKey> parse_key_hex(std::string_view text);

// Encode a TC (SET or GET) frame:
//   [16-byte key] [4-byte service_event with flag] [MIB-driven data]
// `msg` describes the TC in the MIB; `values` are the field values.
// For a bare GET (no data), pass an empty json object.
Bytes encode_frame(const ProductKey& key,
                   std::uint32_t event_base,
                   std::uint32_t flag,           // FLAG_GET or FLAG_SET
                   const mib::Message& msg,
                   const nlohmann::json& values);

// Decode a received frame.
struct Decoded {
    bool          framed_ok = false;   // ≥ 20 bytes, header sane
    bool          ack = false;         // ACK flag set (SET/ACT succeeded)
    bool          nak = false;         // NAK flag set
    std::uint32_t event_base = 0;      // low 28 bits of service_event
    std::uint32_t event_flags = 0;     // top nibble
    ProductKey    key_echoed{};        // key from the response (should match ours)
    nlohmann::json values = nlohmann::json::object();  // decoded data fields
    std::string   error;               // populated on parse failure
};

// `tm` is the response Message from the MIB.  Caller is responsible for
// looking it up (typically by event_base first-parsed).  Pass nullptr for
// header-only responses (RESET, IDLE, etc.) — decode fills only the header
// fields and leaves values empty.
Decoded decode_frame(const Bytes& frame, const mib::Message* tm);

} // namespace xdos::xlink::codec
