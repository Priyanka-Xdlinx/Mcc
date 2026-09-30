// SPDX-License-Identifier: Apache-2.0
#include "cli/value_format.hpp"
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>

using nlohmann::json;

namespace xdos::cli {

// ─── value formatting (uses MIB parameter metadata) ─────────────────────────
//
// format_value renders a JSON value the way a flight-system operator wants
// to see it: with units, with scale applied, with enum symbol if applicable.
//
//   FLOAT → "7.420 V"
//   INT  (codec already scaled, unit °C) → "23.45 °C"
//   ENUM → "3 (Normal)"
//   UINT → "42" or "42 s" if unit present
std::string format_value(const json& v, const mib::Parameter& p) {
    std::ostringstream os;
    try {
        if (p.data_type == "ENUM" && p.enum_type) {
            // Decoded enum may arrive as a plain integer OR as a JSON object
            // shaped {"value": N} or {"value": N, "name": "Foo"} depending on
            // the codec path.  Accept all shapes — the user-visible display
            // must be a number-with-symbolic-name regardless.
            long long val = 0;
            if (v.is_number_integer()) val = v.get<long long>();
            else if (v.is_number())    val = static_cast<long long>(v.get<double>());
            else if (v.is_object() && v.contains("value") && v["value"].is_number())
                                       val = v["value"].get<long long>();
            os << val;
            if (auto* ev = p.enum_type->by_value(val)) {
                os << " (" << ev->name << ")";
            } else {
                os << " (?)";
            }
        } else if (p.data_type == "FLOAT" || p.data_type == "DOUBLE") {
            // The codec has already applied `scale`; this layer only formats.
            double d = v.is_number() ? v.get<double>() : 0.0;
            os << std::fixed << std::setprecision(3) << d;
            if (p.unit) os << " " << *p.unit;
        } else if (p.data_type == "CHAR") {
            // Filename-style CHAR fields (XLink VERSION, ADCS name tags)
            // arrive as a little-endian 4-byte word.  Render the printable
            // ASCII characters, then the raw value in hex for debugging.
            std::uint64_t raw = 0;
            if (v.is_number_integer()) raw = static_cast<std::uint64_t>(v.get<long long>());
            else if (v.is_number())    raw = static_cast<std::uint64_t>(v.get<double>());
            std::string ascii;
            for (int i = 0; i < static_cast<int>(p.length / 8) && i < 8; ++i) {
                char c = static_cast<char>((raw >> (i * 8)) & 0xFF);
                ascii.push_back(std::isprint(static_cast<unsigned char>(c)) ? c : '.');
            }
            os << "\"" << ascii << "\"";
        } else if (p.data_type == "INT" || p.data_type == "UINT") {
            // Special-case: 1-byte boolean-ish status flags
            auto [lbl1, lbl0] = flag_labels_for(p);
            if (!lbl1.empty()) {
                long long val = 0;
                if (v.is_number_integer()) val = v.get<long long>();
                else if (v.is_number())    val = static_cast<long long>(v.get<double>());
                os << val << " (" << (val ? lbl1 : lbl0) << ")";
            } else if (p.scale) {
                // Already in engineering units — scaling happens once, in the
                // codec.  Applying it again here is what made every scaled
                // reading come out as the raw wire count.
                double d = v.is_number() ? v.get<double>() : 0.0;
                os << std::fixed << std::setprecision(2) << d;
                if (p.unit) os << " " << *p.unit;
            } else {
                if (v.is_number_integer())      os << v.get<long long>();
                else if (v.is_number())         os << v.get<double>();
                else                            os << v.dump();
                if (p.unit) os << " " << *p.unit;
            }
        } else {
            os << v.dump();
            if (p.unit) os << " " << *p.unit;
        }
    } catch (...) { os << v.dump(); }
    return os.str();
}

std::pair<std::string, std::string> flag_labels_for(const mib::Parameter& p) {
    if (p.length != 1 || p.data_type != "UINT" || p.unit.has_value())
        return {"", ""};
    std::string n;
    for (char c : p.name) n.push_back(static_cast<char>(std::tolower(c)));
    auto has = [&](const char* s){ return n.find(s) != std::string::npos; };
    if (has("rbf"))                     return {"connected (pin in)",  "disconnected (deployed)"};
    if (has("hdrm") && has("release"))  return {"RELEASED",            "held"};
    if (has("timeout") && has("flag"))  return {"TIMEOUT",             "ok"};
    if (has("release") && has("status")) return {"RELEASED",           "held"};
    if (has(" flag") || n.rfind("flag") == n.size() - 4) return {"set", "clear"};
    return {"", ""};
}

// ─── UHF derived engineering values ─────────────────────────────────────────
// CC1110 datasheet formulas with the OpenLST 27 MHz crystal.  Every value
// documented in the firmware checks out against these:
//   437.000 MHz = 0x102F69 x 27e6/2^16      (board_defaults.h "437 MHz")
//   400.200 MHz = 0x0ED27D                  ("RF Transmission Mode Frequency")
//   450.100 MHz = 0x10AB9D                  ("RF Reception Mode Frequency")
//   60.267 kHz  = 27e6/(8 x (4+3) x 2^3)    ("Channel bandwidth 60.267kHz")
//   7416 bps    = (256+32) x 2^8/2^28 x 27e6 (the radio's 7.4 kbps mode)
static double uhf_num(const nlohmann::json& vals, const char* name) {
    auto it = vals.find(name);
    if (it == vals.end()) return -1;
    if (it->is_number()) return it->get<double>();
    if (it->is_string()) {
        try { return (double)std::stoull(it->get<std::string>(), nullptr, 0); }
        catch (...) {}
    }
    return -1;
}

std::string uhf_derived_text(const std::string& tm_name,
                             const nlohmann::json& vals,
                             const std::string& indent) {
    constexpr double kXosc = 27e6;          // OpenLST crystal
    std::ostringstream os;
    char b[160];

    if (tm_name == "TX_RADIO_CONFIG" || tm_name == "RX_RADIO_CONFIG") {
        const double f2 = uhf_num(vals, "FREQ2"), f1 = uhf_num(vals, "FREQ1"),
                     f0 = uhf_num(vals, "FREQ0");
        const double chan   = uhf_num(vals, "CHANNR");
        const double chsp_e = uhf_num(vals, "CHANSPC_E"),
                     chsp_m = uhf_num(vals, "CHANSPC_M");
        const double dr_e = uhf_num(vals, "DRATE_E"), dr_m = uhf_num(vals, "DRATE_M");
        const double bw_e = uhf_num(vals, "CHANBW_E"), bw_m = uhf_num(vals, "CHANBW_M");
        const double dv_e = uhf_num(vals, "DEVIATION_E"),
                     dv_m = uhf_num(vals, "DEVIATION_M");
        if (f2 < 0 || f1 < 0 || f0 < 0) return "";

        const double freq_reg = f2 * 65536.0 + f1 * 256.0 + f0;
        const double spacing  = kXosc / 262144.0 * (256.0 + chsp_m)
                                * std::pow(2.0, chsp_e);            // 2^18
        const double carrier  = freq_reg * kXosc / 65536.0
                                + (chan > 0 ? chan * spacing : 0.0);
        const double drate = (256.0 + dr_m) * std::pow(2.0, dr_e)
                             / 268435456.0 * kXosc;                 // 2^28
        const double bw  = kXosc / (8.0 * (4.0 + bw_m) * std::pow(2.0, bw_e));
        const double dev = kXosc / 131072.0 * (8.0 + dv_m)
                           * std::pow(2.0, dv_e);                   // 2^17

        os << indent << "derived (CC1110 formulas, 27 MHz XOSC):\n";
        std::snprintf(b, sizeof(b),
            "%sCarrier frequency %20s = %.6f MHz  (FREQ 0x%06X%s)\n",
            indent.c_str(), "", carrier / 1e6, (unsigned)freq_reg,
            chan > 0 ? (" + CHANNR " + std::to_string((int)chan)).c_str() : "");
        os << b;
        std::snprintf(b, sizeof(b), "%sData rate %28s = %.0f bps\n",
                      indent.c_str(), "", drate);
        os << b;
        std::snprintf(b, sizeof(b), "%sChannel bandwidth %20s = %.3f kHz\n",
                      indent.c_str(), "", bw / 1e3);
        os << b;
        std::snprintf(b, sizeof(b), "%sFrequency deviation %18s = %.3f kHz\n",
                      indent.c_str(), "", dev / 1e3);
        os << b;
        std::snprintf(b, sizeof(b), "%sChannel spacing %22s = %.3f kHz\n",
                      indent.c_str(), "", spacing / 1e3);
        os << b;
        return os.str();
    }

    if (tm_name == "TX_RADIO_PKT_CONFIG" || tm_name == "RX_RADIO_PKT_CONFIG") {
        const double s0 = uhf_num(vals, "SYNC_WORD0"),
                     s1 = uhf_num(vals, "SYNC_WORD1");
        if (s0 < 0 || s1 < 0) return "";
        os << indent << "derived:\n";
        std::snprintf(b, sizeof(b), "%sSync word SYNC[15:0] %17s = 0x%04X\n",
                      indent.c_str(), "",
                      ((unsigned)s0 << 8) | (unsigned)s1);
        os << b;
        return os.str();
    }
    return "";
}

} // namespace xdos::cli
