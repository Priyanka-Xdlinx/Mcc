// SPDX-License-Identifier: Apache-2.0
#include "rate_group_lut.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace xdos::obc {

using nlohmann::json;

static bool sub_from_string(const std::string& s, Subsystem& out) {
    if (s == "EPS")   { out = Subsystem::EPS;   return true; }
    if (s == "GPS")   { out = Subsystem::GPS;   return true; }
    if (s == "XLINK" || s == "XLINK-S") { out = Subsystem::XLINK; return true; }
    if (s == "ADCS")  { out = Subsystem::ADCS;  return true; }
    if (s == "Payload") { out = Subsystem::Payload; return true; }
    return false;
}

static Bytes hex_to_bytes(const std::string& hex, bool& ok) {
    Bytes out;
    std::string clean;
    for (char c : hex) if (!std::isspace((unsigned char)c)) clean += c;
    if (clean.size() % 2 != 0) { ok = false; return out; }
    for (std::size_t i = 0; i < clean.size(); i += 2) {
        auto hexval = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int hi = hexval(clean[i]), lo = hexval(clean[i + 1]);
        if (hi < 0 || lo < 0) { ok = false; return out; }
        out.push_back(static_cast<Byte>((hi << 4) | lo));
    }
    ok = true;
    return out;
}

std::uint16_t RateGroupLut::compute_signature() const {
    // Same rolling scheme as the GS-side RateGroupLayout::resolve, computed
    // from (offset, size) pairs derived from the row order + resp_size.
    std::uint16_t sig = 0x1F35;
    int offset = 0;
    for (const auto& r : rows) {
        sig = static_cast<std::uint16_t>((sig << 3) | (sig >> 13));  // rotl 3
        sig ^= static_cast<std::uint16_t>(offset * 131 + r.resp_size);
        offset += r.resp_size;
    }
    return sig;
}

bool RateGroupLut::load_json(const std::string& text, RateGroupLut& out,
                             std::string* err) {
    try {
        json j = json::parse(text);
        out.rows.clear();
        out.revision  = j.value("revision", 0);
        out.generated = j.value("generated", std::string());

        if (!j.contains("commands") || !j["commands"].is_array()) {
            if (err) *err = "LUT: missing 'commands' array";
            return false;
        }
        for (const auto& e : j["commands"]) {
            RateGroupLutRow row;
            row.order     = e.value("order", (int)out.rows.size() + 1);
            // No command id on the OBC side — position is the only link to the
            // GS label.  (An id in the file, if present, is simply ignored.)
            std::string subs = e.value("subsystem", std::string());
            if (!sub_from_string(subs, row.sub)) {
                if (err) *err = "LUT: unknown subsystem '" + subs +
                                "' at order " + std::to_string(row.order);
                return false;
            }
            bool ok = false;
            row.tc_bytes = hex_to_bytes(e.value("tc_hex", std::string()), ok);
            if (!ok) {
                if (err) *err = "LUT: bad tc_hex at order " + std::to_string(row.order);
                return false;
            }
            row.resp_size = e.value("resp_size", 0);
            out.rows.push_back(std::move(row));
        }
        // Keep the table in slot order.
        std::sort(out.rows.begin(), out.rows.end(),
                  [](const RateGroupLutRow& a, const RateGroupLutRow& b) {
                      return a.order < b.order;
                  });

        std::uint16_t want = out.compute_signature();
        std::uint16_t got  = static_cast<std::uint16_t>(
            j.value("signature", (int)want));
        out.signature = want;
        if (got != want) {
            // Not fatal — but flag it so a mismatched/hand-edited table is
            // visible.  The GS decoder cross-checks the same value.
            if (err) *err = "LUT: signature mismatch (file 0x" +
                [&]{ std::ostringstream o; o << std::hex << got; return o.str(); }() +
                " vs computed 0x" +
                [&]{ std::ostringstream o; o << std::hex << want; return o.str(); }() +
                ") — table will still load";
        }
        return true;
    } catch (const std::exception& e) {
        if (err) *err = std::string("LUT parse error: ") + e.what();
        return false;
    }
}

bool RateGroupLut::load_file(const std::string& path, RateGroupLut& out,
                             std::string* err) {
    std::ifstream f(path);
    if (!f) { if (err) *err = "LUT: cannot open " + path; return false; }
    std::ostringstream ss; ss << f.rdbuf();
    return load_json(ss.str(), out, err);
}


const RateGroupGsSlot* RateGroupGsTable::find(const std::string& id) const {
    for (const auto& s : slots) if (s.id == id) return &s;
    return nullptr;
}

bool RateGroupGsTable::load_file(const std::string& path, RateGroupGsTable& out,
                                 std::string* err) {
    std::ifstream f(path);
    if (!f) { if (err) *err = "GS LUT: cannot open " + path; return false; }
    std::ostringstream ss; ss << f.rdbuf();
    try {
        json j = json::parse(ss.str());
        out.slots.clear();
        out.revision  = j.value("revision", 0);
        if (!j.contains("commands") || !j["commands"].is_array()) {
            if (err) *err = "GS LUT: missing 'commands' array";
            return false;
        }
        int offset = 0;
        for (const auto& e : j["commands"]) {
            RateGroupGsSlot s;
            s.order    = e.value("order", (int)out.slots.size() + 1);
            s.id       = e.value("id", std::string());
            std::string subs = e.value("subsystem", std::string());
            if (!sub_from_string(subs, s.sub)) {
                if (err) *err = "GS LUT: unknown subsystem '" + subs + "'";
                return false;
            }
            s.size     = e.value("resp_size", 0);
            s.rsp_name = e.value("rsp_name", std::string());
            s.offset   = offset;
            offset    += s.size;
            out.slots.push_back(std::move(s));
        }
        std::sort(out.slots.begin(), out.slots.end(),
                  [](const RateGroupGsSlot& a, const RateGroupGsSlot& b) {
                      return a.order < b.order;
                  });
        // Recompute offsets after sort (in case file was out of order).
        int off = 0;
        for (auto& s : out.slots) { s.offset = off; off += s.size; }
        out.signature = static_cast<std::uint16_t>(j.value("signature", 0));
        return true;
    } catch (const std::exception& e) {
        if (err) *err = std::string("GS LUT parse error: ") + e.what();
        return false;
    }
}

}  // namespace xdos::obc
