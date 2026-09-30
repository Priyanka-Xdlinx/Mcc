// SPDX-License-Identifier: Apache-2.0
#include "subsystems/xlink_codec.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

namespace xdos::xlink::codec {

namespace {

// Read a little-endian U32 from bytes[off..off+4].
inline std::uint32_t rd_u32(const Bytes& b, std::size_t off) {
    if (off + 4 > b.size()) return 0;
    return static_cast<std::uint32_t>(b[off])
         | (static_cast<std::uint32_t>(b[off + 1]) <<  8)
         | (static_cast<std::uint32_t>(b[off + 2]) << 16)
         | (static_cast<std::uint32_t>(b[off + 3]) << 24);
}

inline void wr_u32(Bytes& b, std::size_t off, std::uint32_t v) {
    b[off    ] = static_cast<std::uint8_t>( v        & 0xFF);
    b[off + 1] = static_cast<std::uint8_t>((v >>  8) & 0xFF);
    b[off + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    b[off + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}

// Sign-extend a `bits`-wide integer read as unsigned.
inline std::int64_t sign_extend(std::uint64_t raw, int bits) {
    if (bits <= 0 || bits >= 64) return static_cast<std::int64_t>(raw);
    const std::uint64_t sign_bit = 1ULL << (bits - 1);
    if (raw & sign_bit)
        return static_cast<std::int64_t>(raw | (~0ULL << bits));
    return static_cast<std::int64_t>(raw);
}

// Extract a `bits`-wide field starting at absolute bit offset `off_bits`,
// where the layout is little-endian U32 words.  Bit 0 of a word is the LSB.
std::uint64_t extract_bits_u32(const Bytes& bytes,
                               std::size_t off_bits,
                               int bits) {
    if (bits <= 0 || bits > 64) return 0;
    std::uint64_t out = 0;
    int written = 0;
    while (written < bits) {
        std::size_t word_idx  = off_bits >> 5;         // /32
        int bit_in_word       = static_cast<int>(off_bits & 31);
        int take              = std::min(32 - bit_in_word, bits - written);
        std::size_t byte_off  = word_idx * 4;
        if (byte_off + 4 > bytes.size()) break;
        std::uint32_t word = rd_u32(bytes, byte_off);
        std::uint32_t mask = (take == 32) ? 0xFFFFFFFFu : ((1u << take) - 1u);
        std::uint32_t piece = (word >> bit_in_word) & mask;
        out |= static_cast<std::uint64_t>(piece) << written;
        written += take;
        off_bits += static_cast<std::size_t>(take);
    }
    return out;
}

// Write a `bits`-wide field at absolute bit offset `off_bits` into `bytes`
// with U32-word LE ordering.  Grows `bytes` if needed.
void insert_bits_u32(Bytes& bytes,
                     std::size_t off_bits,
                     int bits,
                     std::uint64_t value) {
    if (bits <= 0 || bits > 64) return;
    std::size_t need_bytes = ((off_bits + static_cast<std::size_t>(bits) + 31) / 32) * 4;
    if (bytes.size() < need_bytes) bytes.resize(need_bytes, 0);
    int written = 0;
    while (written < bits) {
        std::size_t word_idx  = off_bits >> 5;
        int bit_in_word       = static_cast<int>(off_bits & 31);
        int take              = std::min(32 - bit_in_word, bits - written);
        std::size_t byte_off  = word_idx * 4;
        std::uint32_t word = rd_u32(bytes, byte_off);
        std::uint32_t mask = (take == 32) ? 0xFFFFFFFFu : ((1u << take) - 1u);
        std::uint32_t piece = static_cast<std::uint32_t>((value >> written) & mask);
        word &= ~(mask << bit_in_word);
        word |=  (piece << bit_in_word);
        wr_u32(bytes, byte_off, word);
        written += take;
        off_bits += static_cast<std::size_t>(take);
    }
}

// Convert a JSON value into the raw bits stored in the field.
// Handles UINT, INT (two's complement), ENUM (looks up name → value),
// and DOUBLE (bit-cast).  Missing/wrong-type values default to 0.
std::uint64_t encode_field_value(const mib::Parameter& p,
                                 const nlohmann::json& v) {
    if (p.data_type == "DOUBLE" && p.length == 64) {
        double d = 0.0;
        if (v.is_number())      d = v.get<double>();
        else if (v.is_string()) try { d = std::stod(v.get<std::string>()); } catch (...) {}
        std::uint64_t raw = 0;
        std::memcpy(&raw, &d, sizeof(raw));
        return raw;
    }
    if (p.data_type == "ENUM" && p.enum_type) {
        if (v.is_number_integer()) return static_cast<std::uint64_t>(v.get<long long>());
        if (v.is_string()) {
            if (auto* ev = p.enum_type->by_name(v.get<std::string>()))
                return static_cast<std::uint64_t>(ev->value);
        }
        if (v.is_object() && v.contains("value") && v["value"].is_number())
            return static_cast<std::uint64_t>(v["value"].get<long long>());
        return 0;
    }
    long long n = 0;
    if (v.is_number_integer()) n = v.get<long long>();
    else if (v.is_number())    n = static_cast<long long>(v.get<double>());
    else if (v.is_boolean())   n = v.get<bool>() ? 1 : 0;
    // Truncate to p.length bits, treating INT as two's complement.
    if (p.length <= 0 || p.length >= 64) return static_cast<std::uint64_t>(n);
    std::uint64_t mask = (1ULL << p.length) - 1;
    return static_cast<std::uint64_t>(n) & mask;
}

// Convert raw bits back into a JSON value using the parameter's type info.
nlohmann::json decode_field_value(const mib::Parameter& p, std::uint64_t raw) {
    if (p.data_type == "DOUBLE" && p.length == 64) {
        double d = 0.0;
        std::memcpy(&d, &raw, sizeof(d));
        return d;
    }
    if (p.data_type == "INT") {
        return sign_extend(raw, p.length);
    }
    if (p.data_type == "ENUM") return static_cast<long long>(raw);
    return static_cast<long long>(raw);
}

} // anonymous namespace

std::optional<ProductKey> parse_key_hex(std::string_view text) {
    // Accept sequences like "ad563c56-ea8bf594-953e59c4-071d177f".
    // Convention: the text is a 128-bit key expressed as 32 hex digits.  We
    // interpret each U32 group as a little-endian word (LSByte first on the
    // wire), matching the "0xAA.0xBB.0xCC.0xDD → 0xAABBCCDD" convention in
    // the spec section 3.2.5.
    std::string hex;
    hex.reserve(32);
    for (char c : text) {
        if (std::isxdigit(static_cast<unsigned char>(c))) hex.push_back(c);
    }
    if (hex.size() != 32) return std::nullopt;
    ProductKey k{};
    for (int word = 0; word < 4; ++word) {
        // Each 8-hex-char group = one U32 written most-significant-nibble
        // first in text.  We store bytes little-endian on the wire.
        std::string chunk = hex.substr(word * 8, 8);
        std::uint32_t v = 0;
        try { v = static_cast<std::uint32_t>(std::stoul(chunk, nullptr, 16)); }
        catch (...) { return std::nullopt; }
        k[word * 4    ] = static_cast<std::uint8_t>((v      ) & 0xFF);
        k[word * 4 + 1] = static_cast<std::uint8_t>((v >>  8) & 0xFF);
        k[word * 4 + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
        k[word * 4 + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
    }
    return k;
}

Bytes encode_frame(const ProductKey& key,
                   std::uint32_t base,
                   std::uint32_t flag,
                   const mib::Message& msg,
                   const nlohmann::json& values) {
    // 20-byte header + data (each parameter after seq 2 lives in the data area).
    Bytes out(20, 0);
    std::memcpy(out.data(), key.data(), 16);
    wr_u32(out, 16, base | flag);

    // The MIB puts product_key and service_event as the first two parameters
    // with offsets in the header region (0 and 128 bits).  We already wrote
    // those; skip them and pack the rest.
    for (auto const& p : msg.parameters) {
        if (p.name == "product_key" || p.name == "service_event") continue;
        auto it = values.find(p.name);
        std::uint64_t raw = (it != values.end())
            ? encode_field_value(p, *it) : 0;
        if (!p.offset) continue;
        insert_bits_u32(out, static_cast<std::size_t>(*p.offset), p.length, raw);
    }
    return out;
}

Decoded decode_frame(const Bytes& frame, const mib::Message* tm) {
    Decoded d;
    if (frame.size() < 20) { d.error = "frame < 20 bytes"; return d; }
    std::memcpy(d.key_echoed.data(), frame.data(), 16);
    std::uint32_t se = rd_u32(frame, 16);
    d.event_base  = event_base(se);
    d.event_flags = event_flags(se);
    d.ack = (se & FLAG_ACK) != 0;
    d.nak = (se & FLAG_NAK) != 0;
    d.framed_ok = true;
    if (!tm) return d;   // header-only response (RESET, IDLE, etc.)

    for (auto const& p : tm->parameters) {
        if (p.name == "product_key" || p.name == "service_event") continue;
        if (!p.offset) continue;
        std::uint64_t raw = extract_bits_u32(frame,
                                             static_cast<std::size_t>(*p.offset),
                                             p.length);
        d.values[p.name] = decode_field_value(p, raw);
    }
    return d;
}

} // namespace xdos::xlink::codec
