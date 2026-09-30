// SPDX-License-Identifier: Apache-2.0
#include "codec/codec.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace xdos::codec {

using json = nlohmann::json;
using xdos::mib::Message;
using xdos::mib::Parameter;

// ─── tiny endian helpers ────────────────────────────────────────────────────
static void put_le(Bytes& buf, std::size_t off, std::uint64_t v, int len) {
    for (int i = 0; i < len; ++i) buf[off + i] = static_cast<Byte>((v >> (8 * i)) & 0xFF);
}
static std::uint64_t get_le(const Byte* p, int len) {
    std::uint64_t v = 0;
    for (int i = 0; i < len; ++i) v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
    return v;
}
static void put_be(Bytes& buf, std::size_t off, std::uint64_t v, int len) {
    for (int i = 0; i < len; ++i)
        buf[off + i] = static_cast<Byte>((v >> (8 * (len - 1 - i))) & 0xFF);
}
static std::uint64_t get_be(const Byte* p, int len) {
    std::uint64_t v = 0;
    for (int i = 0; i < len; ++i) v = (v << 8) | p[i];
    return v;
}
// Byte-offset fields carry their own byte order; bit-packed ones (ADCS) do
// not and always go through the little-endian helpers above.
static void put_int(Bytes& buf, std::size_t off, std::uint64_t v, int len, bool be) {
    be ? put_be(buf, off, v, len) : put_le(buf, off, v, len);
}
static std::uint64_t get_int(const Byte* p, int len, bool be) {
    return be ? get_be(p, len) : get_le(p, len);
}
static std::int64_t sign_extend(std::uint64_t v, int bits) {
    std::uint64_t sign = 1ull << (bits - 1);
    if (v & sign) return static_cast<std::int64_t>(v | ~((1ull << bits) - 1));
    return static_cast<std::int64_t>(v);
}

// ─── enum resolution ────────────────────────────────────────────────────────
static std::int64_t enum_to_num(const Parameter& p, const json& v) {
    if (v.is_string() && p.enum_type) {
        auto* ev = p.enum_type->by_name(v.get<std::string>());
        if (!ev) throw Error("enum value '" + v.get<std::string>() +
                             "' not in " + p.enum_type->name);
        return ev->value;
    }
    if (v.is_number_integer()) return v.get<std::int64_t>();
    if (v.is_number_unsigned()) return static_cast<std::int64_t>(v.get<std::uint64_t>());
    if (v.is_string()) {
        try { return std::stoll(v.get<std::string>(), nullptr, 0); }
        catch (...) { throw Error("enum: bad numeric string"); }
    }
    return 0;
}

// ─── encode (byte mode) ─────────────────────────────────────────────────────
static void enc_byte_field(Bytes& buf, const Parameter& p, const json& v) {
    const int off = *p.offset;
    const int len = p.length;
    const std::string& t = p.data_type;

    if (t == "CHAR" || t == "STRING") {
        std::string s = v.is_string() ? v.get<std::string>() : v.dump();
        for (int i = 0; i < len; ++i)
            buf[off + i] = i < static_cast<int>(s.size()) ? static_cast<Byte>(s[i]) : 0;
        return;
    }
    if (t == "HEX" || t == "RESERVED") {
        std::uint64_t iv = 0;
        if (v.is_number_integer())      iv = static_cast<std::uint64_t>(v.get<std::int64_t>());
        else if (v.is_number_unsigned())iv = v.get<std::uint64_t>();
        else if (v.is_string()) {
            try { iv = std::stoull(v.get<std::string>(), nullptr, 0); } catch (...) {}
        }
        put_int(buf, off, iv, len, p.big_endian);
        return;
    }
    if (t == "BOOL") {
        buf[off] = (v.is_boolean() ? v.get<bool>() : v.get<int>() != 0) ? 1 : 0;
        for (int i = 1; i < len; ++i) buf[off + i] = 0;
        return;
    }
    if (t == "ENUM") {
        put_int(buf, off, static_cast<std::uint64_t>(enum_to_num(p, v)), len,
                p.big_endian);
        return;
    }
    if (t == "FLOAT" && len == 4) {
        double d = v.get<double>();
        if (p.scale) d /= *p.scale;
        float f = static_cast<float>(d);
        std::uint32_t raw; std::memcpy(&raw, &f, 4);
        put_int(buf, off, raw, 4, p.big_endian);
        return;
    }
    if (t == "DOUBLE" && len == 8) {
        double d = v.get<double>();
        if (p.scale) d /= *p.scale;
        std::uint64_t raw; std::memcpy(&raw, &d, 8);
        put_int(buf, off, raw, 8, p.big_endian);
        return;
    }
    // integer
    double d = v.is_number() ? v.get<double>() : 0.0;
    if (p.scale) d /= *p.scale;
    if (t == "INT") {
        auto iv = static_cast<std::int64_t>(std::llround(d));
        put_int(buf, off, static_cast<std::uint64_t>(iv), len, p.big_endian);
    } else {
        auto uv = static_cast<std::uint64_t>(std::llround(d));
        put_int(buf, off, uv, len, p.big_endian);
    }
}

static Bytes encode_byte(const Message& msg, const json& values) {
    std::size_t size = msg.length_bytes.value_or(0);
    if (size == 0) {
        for (auto const& p : msg.parameters)
            if (!p.is_array && p.offset)
                size = std::max<std::size_t>(size, *p.offset + p.length);
    }
    Bytes buf(size, 0);
    for (auto const& p : msg.parameters) {
        if (p.is_array || !p.offset) continue;
        auto it = values.find(p.name);
        if (it == values.end()) continue;
        enc_byte_field(buf, p, *it);
    }
    return buf;
}

// ─── encode (bit mode) ──────────────────────────────────────────────────────
static std::uint64_t enc_bit_field(const Parameter& p, const json& v) {
    const std::string& t = p.data_type;
    const int bits = p.length;
    auto mask = bits >= 64 ? ~0ull : ((1ull << bits) - 1);

    if (t == "BOOL") return (v.is_boolean() ? v.get<bool>() : v.get<int>() != 0) ? 1 : 0;
    if (t == "ENUM") return static_cast<std::uint64_t>(enum_to_num(p, v)) & mask;
    if (t == "CHAR" || t == "STRING") {
        std::string s = v.is_string() ? v.get<std::string>() : v.dump();
        int nbytes = bits / 8;
        std::uint64_t acc = 0;
        for (int i = 0; i < nbytes && i < static_cast<int>(s.size()); ++i)
            acc |= static_cast<std::uint64_t>(static_cast<unsigned char>(s[i])) << (8 * i);
        return acc & mask;
    }
    if (t == "FLOAT" && bits == 32) {
        double d = v.get<double>(); if (p.scale) d /= *p.scale;
        float f = static_cast<float>(d);
        std::uint32_t raw; std::memcpy(&raw, &f, 4);
        return raw;
    }
    if (t == "DOUBLE" && bits == 64) {
        double d = v.get<double>(); if (p.scale) d /= *p.scale;
        std::uint64_t raw; std::memcpy(&raw, &d, 8);
        return raw;
    }
    double d = v.is_number() ? v.get<double>() : 0.0;
    if (p.scale) d /= *p.scale;
    auto iv = static_cast<std::int64_t>(std::llround(d));
    return static_cast<std::uint64_t>(iv) & mask;
}

static Bytes encode_bits(const Message& msg, const json& values) {
    std::size_t total_bits = static_cast<std::size_t>(msg.length_bytes.value_or(0)) * 8;
    if (total_bits == 0) {
        for (auto const& p : msg.parameters)
            if (!p.is_array && p.offset)
                total_bits = std::max<std::size_t>(total_bits, *p.offset + p.length);
    }
    std::size_t nbytes = (total_bits + 7) / 8;
    Bytes buf(nbytes, 0);
    for (auto const& p : msg.parameters) {
        if (p.is_array || !p.offset) continue;
        auto it = values.find(p.name);
        if (it == values.end()) continue;
        std::uint64_t v = enc_bit_field(p, *it);
        std::size_t bit_off = static_cast<std::size_t>(*p.offset);
        for (int i = 0; i < p.length; ++i) {
            if (!((v >> i) & 1)) continue;
            std::size_t bi = bit_off + i;
            buf[bi >> 3] = static_cast<Byte>(buf[bi >> 3] | (1u << (bi & 7)));
        }
    }
    return buf;
}

Bytes encode(const Message& msg, const json& values) {
    if (msg.offset_unit == "bits") return encode_bits(msg, values);
    return encode_byte(msg, values);
}

// ─── decode (byte mode) ─────────────────────────────────────────────────────
static json dec_byte_field(const Parameter& p, const Byte* raw) {
    const int len = p.length;
    const std::string& t = p.data_type;

    if (t == "CHAR" || t == "STRING") {
        std::string s;
        for (int i = 0; i < len; ++i) {
            if (!raw[i]) break;
            s.push_back(static_cast<char>(raw[i]));
        }
        return s;
    }
    if (t == "HEX" || t == "RESERVED") {
        std::ostringstream o; o << "0x";
        // Print most-significant byte first, whichever order it arrived in.
        for (int k = 0; k < len; ++k) {
            const int i = p.big_endian ? k : len - 1 - k;
            o << std::setw(2) << std::setfill('0') << std::hex
              << std::uppercase << static_cast<int>(raw[i]);
        }
        return o.str();
    }
    if (t == "BOOL") {
        for (int i = 0; i < len; ++i) if (raw[i]) return true;
        return false;
    }
    if (t == "ENUM") {
        auto v = static_cast<std::int64_t>(get_int(raw, len, p.big_endian));
        json out = json::object();
        out["value"] = v;
        if (p.enum_type) {
            auto* ev = p.enum_type->by_value(v);
            out["name"] = ev ? json(ev->name) : json(nullptr);
        }
        return out;
    }
    if (t == "FLOAT" && len == 4) {
        std::uint32_t r = static_cast<std::uint32_t>(get_int(raw, 4, p.big_endian));
        float f; std::memcpy(&f, &r, 4);
        double d = f;
        if (p.scale) d *= *p.scale;
        return d;
    }
    if (t == "DOUBLE" && len == 8) {
        std::uint64_t r = get_int(raw, 8, p.big_endian);
        double d; std::memcpy(&d, &r, 8);
        if (p.scale) d *= *p.scale;
        return d;
    }
    auto u = get_int(raw, len, p.big_endian);
    if (t == "INT") {
        auto signed_v = sign_extend(u, len * 8);
        if (p.scale) return static_cast<double>(signed_v) * *p.scale;
        return static_cast<std::int64_t>(signed_v);
    }
    if (p.scale) return static_cast<double>(u) * *p.scale;
    return static_cast<std::int64_t>(u);
}

static json decode_byte(const Message& msg, const Bytes& payload) {
    json out = json::object();
    std::vector<const Parameter*> fixed, arrays;
    for (auto const& p : msg.parameters) (p.is_array ? arrays : fixed).push_back(&p);

    for (auto const* p : fixed) {
        if (!p->offset) continue;
        std::size_t end = static_cast<std::size_t>(*p->offset) + p->length;
        if (end > payload.size()) { out[p->name] = json(nullptr); continue; }
        out[p->name] = dec_byte_field(*p, payload.data() + *p->offset);
    }

    if (!arrays.empty()) {
        std::size_t start = 0;
        for (auto const* p : fixed)
            if (p->offset)
                start = std::max<std::size_t>(start, *p->offset + p->length);
        std::size_t rec_len = 0;
        for (auto const* p : arrays) rec_len += p->length;
        json arr = json::array();
        std::size_t pos = start;
        while (rec_len > 0 && pos + rec_len <= payload.size()) {
            json rec = json::object();
            std::size_t cur = pos;
            for (auto const* p : arrays) {
                rec[p->name] = dec_byte_field(*p, payload.data() + cur);
                cur += p->length;
            }
            arr.push_back(std::move(rec));
            pos += rec_len;
        }
        out["_array"] = std::move(arr);
    }
    return out;
}

// ─── decode (bit mode) ──────────────────────────────────────────────────────
static json dec_bit_field(const Parameter& p, std::uint64_t raw) {
    const std::string& t = p.data_type;
    const int bits = p.length;

    if (t == "BOOL") return (raw & 1) != 0;
    if (t == "ENUM") {
        json out = json::object();
        out["value"] = static_cast<std::int64_t>(raw);
        if (p.enum_type) {
            auto* ev = p.enum_type->by_value(static_cast<std::int64_t>(raw));
            out["name"] = ev ? json(ev->name) : json(nullptr);
        }
        return out;
    }
    if (t == "CHAR" || t == "STRING") {
        int nbytes = bits / 8;
        std::string s;
        for (int i = 0; i < nbytes; ++i) {
            Byte b = (raw >> (8 * i)) & 0xFF;
            if (!b) break;
            s.push_back(static_cast<char>(b));
        }
        return s;
    }
    if (t == "FLOAT" && bits == 32) {
        float f; std::uint32_t r = raw & 0xFFFFFFFFu;
        std::memcpy(&f, &r, 4);
        double d = f;
        if (p.scale) d *= *p.scale;
        return d;
    }
    if (t == "DOUBLE" && bits == 64) {
        double d; std::memcpy(&d, &raw, 8);
        if (p.scale) d *= *p.scale;
        return d;
    }
    if (t == "INT") {
        auto v = sign_extend(raw, bits);
        if (p.scale) return static_cast<double>(v) * *p.scale;
        return v;
    }
    if (p.scale) return static_cast<double>(raw) * *p.scale;
    return static_cast<std::int64_t>(raw);
}

static json decode_bits(const Message& msg, const Bytes& payload) {
    json out = json::object();
    for (auto const& p : msg.parameters) {
        if (p.is_array || !p.offset) continue;
        std::uint64_t v = 0;
        for (int i = 0; i < p.length; ++i) {
            std::size_t bi = static_cast<std::size_t>(*p.offset) + i;
            std::size_t byte_idx = bi >> 3;
            if (byte_idx >= payload.size()) break;
            if ((payload[byte_idx] >> (bi & 7)) & 1u)
                v |= 1ull << i;
        }
        out[p.name] = dec_bit_field(p, v);
    }
    return out;
}

json decode(const Message& msg, const Bytes& payload) {
    if (msg.offset_unit == "bits") return decode_bits(msg, payload);
    return decode_byte(msg, payload);
}

} // namespace xdos::codec
