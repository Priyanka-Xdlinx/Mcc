// SPDX-License-Identifier: Apache-2.0
#include "common/hex.hpp"
#include <cctype>

namespace xdos::hex {

static constexpr char kLower[] = "0123456789abcdef";
static constexpr char kUpper[] = "0123456789ABCDEF";

std::string encode(const Bytes& b) {
    std::string out;
    out.reserve(b.size() * 2);
    for (Byte v : b) {
        out.push_back(kLower[v >> 4]);
        out.push_back(kLower[v & 0xF]);
    }
    return out;
}

std::string encode_spaced(const Bytes& b) {
    std::string out;
    out.reserve(b.size() * 3);
    for (std::size_t i = 0; i < b.size(); ++i) {
        if (i) out.push_back(' ');
        out.push_back(kUpper[b[i] >> 4]);
        out.push_back(kUpper[b[i] & 0xF]);
    }
    return out;
}

static int nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::optional<Bytes> decode(std::string_view s) {
    Bytes out;
    out.reserve(s.size() / 2);
    int hi = -1;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '#') {                       // comment to end of line
            while (i + 1 < s.size() && s[i + 1] != '\n') ++i;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)) || c == ',' || c == ':') continue;
        int n = nibble(c);
        if (n < 0) return std::nullopt;
        if (hi < 0) hi = n;
        else { out.push_back(static_cast<Byte>((hi << 4) | n)); hi = -1; }
    }
    if (hi >= 0) return std::nullopt;          // odd nibble count
    return out;
}

} // namespace xdos::hex
