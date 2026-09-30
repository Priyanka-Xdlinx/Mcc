// SPDX-License-Identifier: Apache-2.0
#include "framing/sensor_framing.hpp"

namespace xdos::framing::sensor {

Bytes encode(Byte dest_id, Byte cmd_id, const Bytes& payload) {
    const std::size_t n = payload.size() > (std::size_t)kMaxPayload
                        ? (std::size_t)kMaxPayload : payload.size();
    Bytes f;
    f.reserve(kOverhead + n);
    f.push_back(kStart0);
    f.push_back(kStart1);
    f.push_back(dest_id);
    f.push_back(cmd_id);
    f.push_back(static_cast<Byte>(n));
    f.insert(f.end(), payload.begin(), payload.begin() + n);
    f.push_back(kEnd);
    return f;
}

Decoded decode(const Bytes& frame) {
    Decoded d;
    // Find the sync pair — a reply may be preceded by line noise.
    std::size_t i = 0;
    while (i + 1 < frame.size() &&
           !(frame[i] == kStart0 && frame[i + 1] == kStart1)) ++i;
    if (i + 4 >= frame.size()) return d;        // no sync, or header truncated

    const Byte        dest = frame[i + 2];
    const Byte        cmd  = frame[i + 3];
    const std::size_t len  = frame[i + 4];
    const std::size_t total = i + frame_size(static_cast<Byte>(len));
    if (total > frame.size())        return d;  // declared longer than we hold
    if (frame[total - 1] != kEnd)    return d;  // EOF not where len says it is

    d.dest_id   = dest;
    d.cmd_id    = cmd;
    d.data.assign(frame.begin() + i + 5, frame.begin() + total - 1);
    d.framed_ok = true;
    return d;
}

} // namespace xdos::framing::sensor
