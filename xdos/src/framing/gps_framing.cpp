// SPDX-License-Identifier: Apache-2.0
#include "framing/gps_framing.hpp"
#include "common/crc.hpp"

#include <cstring>

namespace xdos::framing::gps {

static void put_u16_le(Byte* p, std::uint16_t v) {
    p[0] = static_cast<Byte>(v & 0xFF);
    p[1] = static_cast<Byte>((v >> 8) & 0xFF);
}
static void put_u32_le(Byte* p, std::uint32_t v) {
    p[0] = static_cast<Byte>(v & 0xFF);
    p[1] = static_cast<Byte>((v >> 8) & 0xFF);
    p[2] = static_cast<Byte>((v >> 16) & 0xFF);
    p[3] = static_cast<Byte>((v >> 24) & 0xFF);
}
static std::uint16_t get_u16_le(const Byte* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}
static std::uint32_t get_u32_le(const Byte* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

Bytes encode(std::uint16_t msg_id, const Bytes& data, EncodeOpts opts) {
    Bytes out(kHeaderLen + data.size() + 4, 0);
    out[0] = kSync0; out[1] = kSync1; out[2] = kSync2;
    out[3] = static_cast<Byte>(kHeaderLen);
    put_u16_le(out.data() + 4, msg_id);
    out[6] = 0;        // message type = binary
    out[7] = kPortAddress;   // port address — see gps_framing.hpp
    put_u16_le(out.data() + 8,  static_cast<std::uint16_t>(data.size()));
    // header offsets 10..13 left zero (seq / idle / time status — placeholders)
    put_u16_le(out.data() + 14, opts.week);
    put_u32_le(out.data() + 16, opts.ms);
    // 20..23 receiver status, 24..27 reserved+sw_version — placeholders

    if (!data.empty())
        std::memcpy(out.data() + kHeaderLen, data.data(), data.size());

    auto crc = crc::crc32_oem7(out.data(), kHeaderLen + data.size());
    put_u32_le(out.data() + kHeaderLen + data.size(), crc);
    return out;
}

Decoded decode(const Bytes& frame) {
    Decoded d{};
    if (frame.size() < kHeaderLen + 4)                              return d;
    if (frame[0] != kSync0 || frame[1] != kSync1 || frame[2] != kSync2) return d;

    d.framed_ok = true;
    d.msg_id    = get_u16_le(frame.data() + 4);
    auto dlen   = get_u16_le(frame.data() + 8);
    d.week      = get_u16_le(frame.data() + 14);
    d.ms        = get_u32_le(frame.data() + 16);
    d.rx_status = get_u32_le(frame.data() + 20);

    if (frame.size() < static_cast<std::size_t>(kHeaderLen) + dlen + 4)
        return d;

    d.data.assign(frame.begin() + kHeaderLen,
                  frame.begin() + kHeaderLen + dlen);
    auto crc_rx = get_u32_le(frame.data() + kHeaderLen + dlen);
    auto crc_cm = crc::crc32_oem7(frame.data(), kHeaderLen + dlen);
    d.crc_ok = (crc_rx == crc_cm);
    return d;
}

} // namespace xdos::framing::gps
