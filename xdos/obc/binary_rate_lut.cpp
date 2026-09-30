// SPDX-License-Identifier: Apache-2.0
#include "binary_rate_lut.hpp"

#include <fstream>

namespace xdos::obc {

using namespace xdos::lut;

namespace {

std::uint16_t rd16(const Bytes& b, std::size_t o) {
    return static_cast<std::uint16_t>(b[o] | (b[o+1] << 8));
}
std::uint32_t rd32(const Bytes& b, std::size_t o) {
    return static_cast<std::uint32_t>(b[o]) |
           (static_cast<std::uint32_t>(b[o+1]) << 8) |
           (static_cast<std::uint32_t>(b[o+2]) << 16) |
           (static_cast<std::uint32_t>(b[o+3]) << 24);
}

bool fail(std::string* err, const std::string& msg) {
    if (err) *err = msg;
    return false;
}

// Hardware APID -> subsystem (ADCS 1, UHF 2, EPS 3, GPS 4, PL 5, XLINK-S 6).
bool sub_from_apid(std::uint8_t apid, Subsystem& out) {
    switch (apid) {
        case 1: out = Subsystem::ADCS;    return true;
        case 2: out = Subsystem::UHF;     return true;
        case 3: out = Subsystem::EPS;     return true;
        case 4: out = Subsystem::GPS;     return true;
        case 5: out = Subsystem::Payload; return true;
        case 6: out = Subsystem::XLINK;   return true;
        default: return false;
    }
}

}  // namespace

bool BinaryRateLut::parse(const Bytes& image, BinaryRateLut& out,
                          std::string* err) {
    out = BinaryRateLut{};
    if (image.size() < (std::size_t)kHeaderSize)
        return fail(err, "bin too short for header");
    if (!(image[0]=='X' && image[1]=='L' && image[2]=='U' && image[3]=='T'))
        return fail(err, "bin bad magic");
    if (image[4] != kFormatVersion)
        return fail(err, "bin unsupported version " + std::to_string(image[4]));
    if (rd16(image, 6) != kSubframeSize)
        return fail(err, "bin subframe size mismatch");
    for (int t = 0; t < 4; ++t)
        if (rd16(image, 8 + 2*t) != (std::uint16_t)kSegBytes[t])
            return fail(err, std::string("bin segment width mismatch (")
                             + kTierName[t] + ")");
    if (image[16] != kReservedBytes)
        return fail(err, "bin reserved-bytes mismatch");

    if (rd32(image, 44) != image.size())
        return fail(err, "bin length field mismatch");
    const std::uint16_t want_crc = rd16(image, 48);
    {
        Bytes z = image; z[48] = z[49] = 0;
        if (crc16_ccitt(z.data(), z.size()) != want_crc)
            return fail(err, "bin CRC mismatch");
    }
    out.crc = want_crc;
    out.t_sf_ms = rd16(image, 18);
    if (out.t_sf_ms == 0) out.t_sf_ms = kTsfMs;      // legacy files wrote 0

    std::array<std::uint16_t,4> counts;
    std::array<std::uint32_t,4> segoff;
    for (int t = 0; t < 4; ++t) counts[t] = rd16(image, 20 + 2*t);
    for (int t = 0; t < 4; ++t) segoff[t] = rd32(image, 28 + 4*t);
    if (segoff[0] != (std::uint32_t)kHeaderSize)
        return fail(err, "bin first segment offset mismatch");
    for (int t = 1; t < 4; ++t)
        if (segoff[t] < segoff[t-1])
            return fail(err, "bin segment offsets not increasing");
    if (segoff[3] > image.size())
        return fail(err, "bin segment offset beyond file");

    for (int t = 0; t < 4; ++t) {
        std::size_t p = segoff[t];
        const std::size_t seg_end = (t < 3) ? segoff[t+1] : image.size();
        auto need = [&](std::size_t n) { return p + n <= seg_end; };
        for (int i = 0; i < counts[t]; ++i) {
            const std::string where = std::string(kTierName[t]) + " entry "
                                      + std::to_string(i + 1);
            if (!need(2) || rd16(image, p) != kEntrySync)
                return fail(err, "bin bad sync at " + where);
            p += 2;
            BinEntry e;
            if (!need(2)) return fail(err, "bin truncated at " + where);
            e.apid  = image[p];
            e.flags = image[p+1];
            p += 2;
            if (!sub_from_apid(e.apid, e.sub))
                return fail(err, "bin unknown APID "
                                 + std::to_string(e.apid) + " at " + where);
            if (!need(2)) return fail(err, "bin truncated at " + where);
            const std::uint16_t tc_len = rd16(image, p); p += 2;
            if (tc_len == 0 || !need(tc_len))
                return fail(err, "bin bad TC length at " + where);
            e.tc_bytes.assign(image.begin() + p, image.begin() + p + tc_len);
            p += tc_len;
            if (!need(3)) return fail(err, "bin truncated at " + where);
            e.tm_raw_len = rd16(image, p); p += 2;
            const std::uint8_t pick_count = image[p]; p += 1;
            if (pick_count == 0)
                return fail(err, "bin entry with no picks at " + where);
            for (int k = 0; k < pick_count; ++k) {
                if (!need(6)) return fail(err, "bin truncated pick at " + where);
                BinPick pk;
                pk.src   = rd16(image, p);
                pk.size  = rd16(image, p + 2);
                pk.block = rd16(image, p + 4);
                p += 6;
                if (pk.size == 0)
                    return fail(err, "bin zero-size pick at " + where);
                if (pk.src + pk.size > e.tm_raw_len)
                    return fail(err, "bin pick outside raw payload at " + where);
                if (pk.block + pk.size > (std::uint16_t)kBlockBudget[t])
                    return fail(err, "bin pick outside block budget at " + where);
                e.picks.push_back(pk);
            }
            out.tiers[t].push_back(std::move(e));
        }
        if (p != seg_end)
            return fail(err, std::string("bin trailing bytes in segment ")
                             + kTierName[t]);
    }
    return true;
}

bool BinaryRateLut::load_file(const std::string& path, BinaryRateLut& out,
                              std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return fail(err, "cannot open " + path);
    Bytes image((std::istreambuf_iterator<char>(f)),
                std::istreambuf_iterator<char>());
    return parse(image, out, err);
}

} // namespace xdos::obc
