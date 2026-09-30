// SPDX-License-Identifier: Apache-2.0
// XDOS Ground Station — MIB-driven payload codec.
//
// encode  : value map (json object) -> payload bytes
// decode  : payload bytes -> value map (json object)
//
// Two offset modes, selected per-message by Message.offset_unit:
//   "bits"  — ADCS (CubeSpace): fields are bit-packed, little-endian
//   "bytes" — EPS, GPS:         fields are byte-aligned, little-endian
//
// All multi-byte values are little-endian.
// Scaled fields carry `scale` as a MULTIPLIER applied on the way out of the
// wire:  engineering = raw * scale,  raw = engineering / scale.
// So a field documented as "deg C x 100" has scale 0.01, and one documented
// as "value = RAWVAL*0.00001" has scale 0.00001 — the stored number is the
// factor in the vendor's own formula, never its reciprocal.
//
// Scale is applied exactly once, here.  Presentation layers format what the
// codec returns and must not scale again.  ENUM fields accept either the numeric value or
// the enum value name as input.
//
// Variable-length trailing arrays (GPS HWMONITOR/PSRDOP/VERSION) are
// decoded greedily from the remaining bytes; the result lands in a
// "_array" array of objects in the output json.
#pragma once

#include "mib/mib.hpp"
#include <nlohmann/json.hpp>

namespace xdos::codec {

struct Error : std::runtime_error { using std::runtime_error::runtime_error; };

// Encode a value map into payload bytes for the given message.
//   - missing fields default to 0 / empty
//   - extra fields are ignored
//   - array parameters are skipped on encode (ground station never sends them)
Bytes encode(const mib::Message& msg, const nlohmann::json& values);

// Decode payload bytes against the message definition.
// Variable-length trailing arrays are expanded into "_array".
nlohmann::json decode(const mib::Message& msg, const Bytes& payload);

} // namespace xdos::codec
