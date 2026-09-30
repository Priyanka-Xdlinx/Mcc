// SPDX-License-Identifier: Apache-2.0
// xdos_gs CLI — shared types, utilities, Logger, and CommandHistory.
#pragma once

#include "common/color.hpp"
#include "common/types.hpp"
#include "mib/mib.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace xdos::cli {

// ─── runtime config ─────────────────────────────────────────────────────────
struct GsConfig {
    bool via_obc      = true;
    std::string obc_host = "127.0.0.1";
    int obc_port      = 10000;
    int eps_port      = 10010;
    int adcs_port     = 10020;
    int gps_port      = 10030;
    int payload_port  = 10040;
    // XLink direct-connect defaults (only used with --no-obc).  For real
    // hardware this should be set to 192.168.1.1:51003 per the XLink spec;
    // for sim it's the same 10050 the OBC uses.
    std::string xlink_host = "127.0.0.1";
    int         xlink_port = 10050;
    // 32-hex product key (four U32 words in "aabbccdd-eeff..." form).  Empty
    // means "use zero key" — fine for the sim, real hardware will NAK.
    std::string xlink_key;
};

// ─── logging tee ────────────────────────────────────────────────────────────
class Logger {
public:
    Logger() {
        auto t = std::time(nullptr);
        auto lt = *std::localtime(&t);
        std::ostringstream o;
        o << "TCTM_Payload_" << std::setfill('0')
          << std::setw(2) << (lt.tm_mon+1) << "_"
          << std::setw(2) << lt.tm_mday   << "_"
          << std::setw(2) << lt.tm_hour   << "_"
          << std::setw(2) << lt.tm_min << ".txt";
        name_ = o.str();
        fs_.open(name_, std::ios::out | std::ios::app);
    }
    template<class T>
    Logger& operator<<(const T& v) {
        std::ostringstream o; o << v;
        std::string s = o.str();
        std::cout << s;
        // Strip ANSI escapes from log file copy
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\033') {
                while (i < s.size() && s[i] != 'm') ++i;
                continue;
            }
            fs_.put(s[i]);
        }
        fs_.flush();
        return *this;
    }
    const std::string& filename() const { return name_; }

private:
    std::string name_;
    std::ofstream fs_;
};

// ─── TC classification ──────────────────────────────────────────────────────
enum class TcKind { SET, GET, ACTION };

inline TcKind classify_tc(const std::string& name) {
    if (name == "LOG")                              return TcKind::GET;
    if (name == "UNLOG" || name == "UNLOGALL")      return TcKind::ACTION;
    std::string lower; for (char c : name) lower.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(c))));
    std::vector<std::string> toks; std::string t;
    for (char c : lower) {
        if (c == '_') { if (!t.empty()) { toks.push_back(t); t.clear(); } }
        else            t.push_back(c);
    }
    if (!t.empty()) toks.push_back(t);
    for (auto const& tok : toks) {
        if (tok == "get" || tok == "read" || tok == "query" ||
            tok == "request" || tok == "readings" || tok == "status")
            return TcKind::GET;
        if (tok == "set" || tok == "config" || tok == "configure" ||
            tok == "write" || tok == "enable" || tok == "disable")
            return TcKind::SET;
    }
    return TcKind::ACTION;
}

inline std::string tag_str(TcKind k) {
    switch (k) {
        case TcKind::SET:    return color::set_tag("[SET]");
        case TcKind::GET:    return color::get_tag("[GET]");
        case TcKind::ACTION: return color::act_tag("[ACT]");
    }
    return "[???]";
}

// ─── small input utilities ──────────────────────────────────────────────────
inline std::string read_line(const std::string& prompt) {
    std::cout << color::hint(prompt);
    std::cout.flush();
    std::string s;
    if (!std::getline(std::cin, s)) return "";
    return s;
}

inline std::string trim(std::string s) {
    size_t i = 0, j = s.size();
    while (i < j && std::isspace((unsigned char)s[i])) ++i;
    while (j > i && std::isspace((unsigned char)s[j-1])) --j;
    return s.substr(i, j - i);
}

inline std::optional<int> parse_int(const std::string& s) {
    try { size_t e=0; int v = std::stoi(s, &e); if (e==s.size()) return v; }
    catch (...) {}
    return std::nullopt;
}

inline std::string fmt_bytes(const Bytes& b) {
    std::ostringstream o; o << std::hex << std::setfill('0');
    for (auto x : b) o << std::setw(2) << (int)x << ' ';
    return o.str();
}

inline bool icontains(const std::string& haystack, const std::string& needle) {
    if (needle.size() > haystack.size()) return false;
    return std::search(
        haystack.begin(), haystack.end(),
        needle.begin(), needle.end(),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a))
                == std::tolower(static_cast<unsigned char>(b));
        }
    ) != haystack.end();
}

// ─── TC listing entry ───────────────────────────────────────────────────────
struct TcEntry {
    int                 id;
    int                 wire_id;
    std::string         name;
    std::string         param_id;                // e.g. "EP-019" if present in MIB
    std::optional<std::string> node;
    TcKind              kind;
    bool                synthesized = false;   // GPS LOG-wrap or ADCS empty-req
};

// ─── command history ────────────────────────────────────────────────────────
struct HistoryEntry {
    Subsystem             sub;
    int                   tc_id;
    std::string           tc_name;
    std::optional<std::string> node;
    bool                  synthesized;
    std::chrono::steady_clock::time_point when;
};

class CommandHistory {
public:
    void push(Subsystem sub, int tc_id, const std::string& name,
              std::optional<std::string> node, bool synth) {
        HistoryEntry e { sub, tc_id, name, std::move(node), synth,
                         std::chrono::steady_clock::now() };
        if (entries_.size() >= kMax) entries_.erase(entries_.begin());
        entries_.push_back(std::move(e));
    }
    const std::vector<HistoryEntry>& entries() const { return entries_; }
    bool empty() const { return entries_.empty(); }
private:
    static constexpr size_t kMax = 10;
    std::vector<HistoryEntry> entries_;
};

// remembering the last TC so [Enter] can repeat it
struct LastSent { bool valid = false; Subsystem sub; int id; bool synth=false; };

} // namespace xdos::cli
