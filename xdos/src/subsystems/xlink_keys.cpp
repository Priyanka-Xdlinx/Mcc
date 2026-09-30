// SPDX-License-Identifier: Apache-2.0
#include "subsystems/xlink_keys.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace xdos::xlink {

namespace {

// Trim CR/LF/tabs/spaces off both ends
static std::string trim(std::string s) {
    auto issp = [](unsigned char c) { return std::isspace(c) || c == '\r'; };
    while (!s.empty() && issp((unsigned char)s.back())) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && issp((unsigned char)s[i])) ++i;
    return s.substr(i);
}

// Parse "Key: value" or "Key: N" lines.  Returns the value after the colon
// (or empty if the line doesn't match).
static std::string after_colon(const std::string& s) {
    auto pos = s.find(':');
    if (pos == std::string::npos) return "";
    return trim(s.substr(pos + 1));
}

} // anonymous

std::optional<KeyCatalog> KeyCatalog::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return std::nullopt;

    KeyCatalog cat;
    std::string line;
    KeyEntry cur;
    bool have_entry = false;
    bool expect_key_hex = false;
    int  line_no = 0;

    while (std::getline(f, line)) {
        ++line_no;
        auto t = trim(line);
        if (t.empty()) continue;

        // Metadata (first few lines, no colons for SN/MAC/model — plain "SN N" form)
        if (t.rfind("SN ", 0) == 0 && cat.serial_number.empty()) {
            cat.serial_number = trim(t.substr(3));
            continue;
        }
        if (t.rfind("MAC ", 0) == 0 && cat.mac.empty()) {
            cat.mac = trim(t.substr(4));
            continue;
        }
        if (cat.unit_model.empty() && !t.empty() &&
            t.find("###") == std::string::npos &&
            t.find(':') == std::string::npos &&
            !std::isdigit((unsigned char)t[0]) &&
            t.rfind("SN ", 0) != 0 && t.rfind("MAC ", 0) != 0 &&
            t.rfind("Use ", 0) != 0 && t.rfind("Xmt", 0) != 0 &&
            t.rfind("Rcv", 0) != 0 && t.rfind("More", 0) != 0 &&
            t.rfind("Key", 0) != 0) {
            cat.unit_model = t;
            continue;
        }

        // Section separator "###..." ends the current entry
        if (t.find("###") == 0) {
            if (have_entry) {
                cat.entries.push_back(std::move(cur));
                cur = KeyEntry{};
                have_entry = false;
                expect_key_hex = false;
            }
            continue;
        }

        // Frequency lines start a new entry
        if (t.rfind("XmtSymbFreq:", 0) == 0) {
            if (have_entry && cur.xmt_symb_freq_khz != 0) {
                cat.entries.push_back(std::move(cur));
                cur = KeyEntry{};
            }
            try { cur.xmt_symb_freq_khz = std::stoi(after_colon(t)); } catch (...) {}
            have_entry = true;
            continue;
        }
        if (t.rfind("RcvSymbFreq:", 0) == 0) {
            try { cur.rcv_symb_freq_khz = std::stoi(after_colon(t)); } catch (...) {}
            have_entry = true;
            continue;
        }

        // "Key:" (bare) marker; the actual 32-hex-digit string is on the NEXT non-blank line
        if (t == "Key:" || t.rfind("Key:", 0) == 0) {
            std::string rest = after_colon(t);
            if (!rest.empty() && codec::parse_key_hex(rest)) {
                cur.raw_key_text = rest;
                cur.key = *codec::parse_key_hex(rest);
                have_entry = true;
                continue;
            }
            expect_key_hex = true;
            continue;
        }
        if (expect_key_hex) {
            auto k = codec::parse_key_hex(t);
            if (k) {
                cur.key = *k;
                cur.raw_key_text = t;
                have_entry = true;
                expect_key_hex = false;
                continue;
            }
        }

        // Everything else while inside an entry is a capability flag
        // (e.g. "XmtCh0_En", "Use SPI", "RsMode: 0", "XmtOffsetMod").
        if (have_entry) {
            cur.features.push_back(t);
        }
    }
    // Push the last one
    if (have_entry && cur.xmt_symb_freq_khz != 0) {
        cat.entries.push_back(std::move(cur));
    }
    if (cat.entries.empty()) return std::nullopt;
    return cat;
}

const KeyEntry* KeyCatalog::find(int xmt_khz, int rcv_khz) const {
    for (auto const& e : entries) {
        if (e.xmt_symb_freq_khz == xmt_khz && e.rcv_symb_freq_khz == rcv_khz)
            return &e;
    }
    return nullptr;
}

} // namespace xdos::xlink
