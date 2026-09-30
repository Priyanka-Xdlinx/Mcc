// SPDX-License-Identifier: Apache-2.0
#include "framing/adcs_framing.hpp"

namespace xdos::framing::adcs {

Bytes raw_body(Byte msg_id, const Bytes& data) {
    Bytes out;
    out.reserve(1 + data.size());
    out.push_back(msg_id);
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

Decoded split_raw_body(const Bytes& body) {
    Decoded d{};
    if (body.empty()) return d;
    d.msg_id    = body[0];
    d.data.assign(body.begin() + 1, body.end());
    d.framed_ok = true;      // nothing to verify: there is no framing left
    return d;
}

Bytes encode(Byte msg_id, const Bytes& data) {
    Bytes out;
    out.reserve(2 * (1 + data.size()) + 4);   // every byte could need an escape
    out.push_back(kEsc);
    out.push_back(kSom);
    out.push_back(msg_id);
    if (msg_id == kEsc) out.push_back(kEsc);
    for (Byte b : data) {
        out.push_back(b);
        if (b == kEsc) out.push_back(kEsc);
    }
    out.push_back(kEsc);
    out.push_back(kEom);
    return out;
}

Decoded decode(const Bytes& frame) {
    Decoded d{};
    if (frame.size() < 4 || frame[0] != kEsc || frame[1] != kSom) return d;

    Bytes body;
    body.reserve(frame.size());
    std::size_t i = 2;
    while (i < frame.size()) {
        Byte b = frame[i];
        if (b == kEsc) {
            Byte n = (i + 1 < frame.size()) ? frame[i + 1] : 0;
            if (n == kEom) { d.framed_ok = true; break; }
            if (n == kEsc) { body.push_back(kEsc); i += 2; continue; }
            return d;                     // bad escape
        }
        body.push_back(b);
        ++i;
    }
    if (!d.framed_ok || body.empty()) { d.framed_ok = false; return d; }
    d.msg_id = body[0];
    d.data.assign(body.begin() + 1, body.end());
    return d;
}

} // namespace xdos::framing::adcs
