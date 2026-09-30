// SPDX-License-Identifier: Apache-2.0
#include "framing/uhf_framing.hpp"

namespace xdos::framing::uhf {

Bytes encode(Byte command, const Bytes& data, std::uint16_t hwid,
             std::uint16_t seqnum, Byte system) {
    Bytes payload;
    payload.reserve(kHeaderSize + data.size());
    payload.push_back(static_cast<Byte>(hwid & 0xFF));        // LE
    payload.push_back(static_cast<Byte>(hwid >> 8));
    payload.push_back(static_cast<Byte>(seqnum & 0xFF));      // LE
    payload.push_back(static_cast<Byte>(seqnum >> 8));
    payload.push_back(system);
    payload.push_back(command);
    payload.insert(payload.end(), data.begin(), data.end());
    if (payload.size() > (std::size_t)kMaxPayload)
        payload.resize(kMaxPayload);                          // hard ESP limit

    Bytes frame;
    frame.reserve(3 + payload.size());
    frame.push_back(kStart0);
    frame.push_back(kStart1);
    frame.push_back(static_cast<Byte>(payload.size()));
    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

Decoded decode(const Bytes& frame) {
    Decoded d;
    // find the sync pair (tolerate leading noise)
    std::size_t i = 0;
    while (i + 1 < frame.size() &&
           !(frame[i] == kStart0 && frame[i+1] == kStart1)) ++i;
    if (i + 2 >= frame.size()) return d;          // no sync / no length byte
    const std::size_t len = frame[i + 2];
    const std::size_t start = i + 3;
    if (len < (std::size_t)kHeaderSize || start + len > frame.size())
        return d;                                 // truncated or runt payload

    d.hwid    = static_cast<std::uint16_t>(frame[start] |
                                           (frame[start + 1] << 8));
    d.seqnum  = static_cast<std::uint16_t>(frame[start + 2] |
                                           (frame[start + 3] << 8));
    d.system  = frame[start + 4];
    d.command = frame[start + 5];
    d.data.assign(frame.begin() + start + kHeaderSize,
                  frame.begin() + start + len);
    d.framed_ok = true;
    return d;
}

} // namespace xdos::framing::uhf
