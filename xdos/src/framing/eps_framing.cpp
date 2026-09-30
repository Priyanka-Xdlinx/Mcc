// SPDX-License-Identifier: Apache-2.0
#include "framing/eps_framing.hpp"
#include "common/crc.hpp"

#include <array>

namespace xdos::framing::eps {

// ─── Base64 ────────────────────────────────────────────────────────────────
static constexpr char kB64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const Bytes& in) {
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    std::size_t i = 0;
    while (i + 3 <= in.size()) {
        std::uint32_t v = (in[i] << 16) | (in[i + 1] << 8) | in[i + 2];
        out.push_back(kB64Table[(v >> 18) & 0x3F]);
        out.push_back(kB64Table[(v >> 12) & 0x3F]);
        out.push_back(kB64Table[(v >>  6) & 0x3F]);
        out.push_back(kB64Table[(v      ) & 0x3F]);
        i += 3;
    }
    std::size_t rem = in.size() - i;
    if (rem == 1) {
        std::uint32_t v = in[i] << 16;
        out.push_back(kB64Table[(v >> 18) & 0x3F]);
        out.push_back(kB64Table[(v >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        std::uint32_t v = (in[i] << 16) | (in[i + 1] << 8);
        out.push_back(kB64Table[(v >> 18) & 0x3F]);
        out.push_back(kB64Table[(v >> 12) & 0x3F]);
        out.push_back(kB64Table[(v >>  6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

static int b64idx(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

std::optional<Bytes> base64_decode(std::string_view in) {
    Bytes out;
    out.reserve((in.size() / 4) * 3);
    int  buf = 0, bits = 0;
    for (char c : in) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        int v = b64idx(c);
        if (v < 0) return std::nullopt;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<Byte>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

// ─── encode / decode ───────────────────────────────────────────────────────
Bytes encode(Byte dest_addr, Byte msg_id, const Bytes& data, Byte src_addr) {
    Bytes arr;
    arr.reserve(4 + data.size());
    arr.push_back(dest_addr);
    arr.push_back(src_addr);
    arr.push_back(msg_id);
    arr.insert(arr.end(), data.begin(), data.end());
    arr.push_back(crc::crc8(arr));

    auto b64 = base64_encode(arr);

    Bytes out;
    out.reserve(b64.size() + 2);
    out.push_back(kStartFrame);
    for (char c : b64) out.push_back(static_cast<Byte>(c));
    out.push_back(kEndFrame);
    return out;
}

// Is every byte something Base64 text can legally contain?  Used to tell the
// wire's Base64 form apart from the flight OBC's already-decoded array, whose
// address and CRC bytes fall outside the alphabet.
static bool looks_like_base64(const Byte* p, std::size_t len) {
    if (len == 0) return false;
    for (std::size_t i = 0; i < len; ++i) {
        char c = static_cast<char>(p[i]);
        if (b64idx(c) >= 0) continue;
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        return false;
    }
    return true;
}

Decoded decode(const Bytes& frame) {
    Decoded d{};
    if (frame.empty()) return d;

    const Byte* p   = frame.data();
    std::size_t len = frame.size();
    if (p[0] == kStartFrame && p[len - 1] == kEndFrame) {
        d.wrapped   = true;
        d.framed_ok = true;
        ++p; len -= 2;
    }

    Bytes arr;
    if (looks_like_base64(p, len)) {
        auto arr_opt = base64_decode(
            std::string_view{reinterpret_cast<const char*>(p), len});
        if (!arr_opt || arr_opt->size() < 4) return d;
        arr = std::move(*arr_opt);
    } else {
        // Already the bare message array — the flight OBC strips the line
        // framing itself.  There is no wrapper to have seen, so the array
        // being well-formed is what "framed_ok" can mean here.
        if (len < 4) return d;
        arr.assign(p, p + len);
        d.raw_array = true;
        d.framed_ok = true;
    }

    d.dest_addr = arr[0];
    d.src_addr  = arr[1];
    d.msg_id    = arr[2];
    d.crc       = arr.back();
    d.data.assign(arr.begin() + 3, arr.end() - 1);
    auto crc_calc = crc::crc8(arr.data(), arr.size() - 1);
    d.crc_ok    = (crc_calc == d.crc);
    return d;
}

} // namespace xdos::framing::eps
