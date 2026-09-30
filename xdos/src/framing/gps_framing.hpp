// SPDX-License-Identifier: Apache-2.0
// XDOS Ground Station — NovAtel OEM7 binary framing.
//
//   [AA 44 12] | [28-byte header] | [data: 0..N bytes] | [CRC-32 LE: 4 bytes]
//
// The 28-byte header carries (little-endian):
//   off  bytes  field
//    0   3      sync bytes (AA 44 12)
//    3   1      header length (= 28)
//    4   2      message ID
//    6   1      message type (binary = 0)
//    7   1      port address (informational, kPortAddress)
//    8   2      data length
//   10   2      sequence number
//   12   1      idle time
//   13   1      time status
//   14   2      week
//   16   4      milliseconds-into-week
//   20   4      receiver status
//   24   2      reserved
//   26   2      receiver SW version
//
// The CRC-32 is the standard reflected polynomial 0xEDB88320 over the
// whole frame from the first sync byte through the last data byte.
#pragma once

#include "common/types.hpp"

namespace xdos::framing::gps {

constexpr Byte kSync0 = 0xAA;
constexpr Byte kSync1 = 0x44;
constexpr Byte kSync2 = 0x12;
constexpr int  kHeaderLen = 28;

// Header byte 7, from the OEM7 "Detailed Port Identifier" table — a different
// enumeration from the Port enum in the LOG data field (mib/gps.json), which
// selects where the receiver sends output.  This one just labels the port the
// command came in on and is informational to the receiver.
constexpr Byte kPortAddress = 0xC0;

// Value for the `port` field at offset 0 of the LOG command's data field —
// which port the receiver should send the requested log out of.  A separate
// field from kPortAddress above (and a separate enumeration in mib/gps.json),
// but the receiver expects the same identifier here.
constexpr std::uint32_t kLogPort = 0xC0;

struct EncodeOpts {
    std::uint16_t week = 0;
    std::uint32_t ms   = 0;
};

Bytes encode(std::uint16_t msg_id, const Bytes& data, EncodeOpts opts = {});

struct Decoded {
    std::uint16_t msg_id    = 0;
    Bytes         data;
    std::uint16_t week      = 0;
    std::uint32_t ms        = 0;
    std::uint32_t rx_status = 0;
    bool          crc_ok    = false;
    bool          framed_ok = false;
};

Decoded decode(const Bytes& frame);

} // namespace xdos::framing::gps
