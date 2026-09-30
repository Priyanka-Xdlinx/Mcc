// SPDX-License-Identifier: Apache-2.0
// XDOS Ground Station — hex encode / decode utilities.
#pragma once

#include "common/types.hpp"
#include <string>
#include <string_view>

namespace xdos::hex {

// Lower-cased compact hex (no separators).  e.g. {0x1f, 0x7f} -> "1f7f"
std::string encode(const Bytes& b);

// Space-separated upper-cased hex.           e.g. {0x1f, 0x7f} -> "1F 7F"
std::string encode_spaced(const Bytes& b);

// Decode a hex string. Tolerates whitespace, comments after '#', and
// is case-insensitive. Returns std::nullopt on malformed input.
std::optional<Bytes> decode(std::string_view s);

} // namespace xdos::hex
