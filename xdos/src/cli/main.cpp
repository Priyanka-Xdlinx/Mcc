// SPDX-License-Identifier: Apache-2.0
// xdos_gs — ground-station operator CLI (level 1).
//
// Flow:
//   startup banner → connect → main menu (1=ADCS / 2=EPS / 3=GPS / r / q)
//   pick subsystem → list TCs (filter-by-kind on ADCS) → enter ID → params → send
//
// Tap key cheat-sheet at every prompt:
//   ID          — send that TC with default-zero params
//   ?ID         — show TC details without sending
//   b           — back one level
//   q           — quit
//
// Frames go to the OBC bridge by default (TCP 10000).  Use --no-obc to
// connect direct to subsystem sims (10010/10020/10030) as before.

#include "codec/ccsds_codec.hpp"
#include "codec/ccsds_tm_codec.hpp"
#include "common/color.hpp"
#include "common/egse.hpp"
#include "common/flight_config.hpp"
#include "common/socket_compat.hpp"
#include "mib/mib.hpp"
#include "session/session.hpp"
#include "codec/codec.hpp"
#include "subsystems/xlink_codec.hpp"
#include "subsystems/xlink_keys.hpp"
#include "cli/value_format.hpp"
#include "framing/payload_framing.hpp"
#include "framing/gps_framing.hpp"
#include "lut/binary_lut_builder.hpp"
#include "lut/rate_group_csv.hpp"
#include "transport/tcp_transport.hpp"

#include <atomic>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <algorithm>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace xdos;
using nlohmann::json;
namespace fs = std::filesystem;
using xdos::cli::format_value;
using xdos::cli::flag_labels_for;

// ─── runtime config ─────────────────────────────────────────────────────────
struct GsConfig {
    bool via_obc      = true;
    std::string obc_host = "192.168.1.111";  // bench default: real OBC
    int obc_port      = 7000;
    int eps_port      = 10010;
    int adcs_port     = 10020;
    int gps_port      = 10030;
    int payload_port  = 10040;
    int uhf_port      = 10060;
    int sensor_port   = 10070;
    // XLink direct-connect defaults (only used with --no-obc).  For real
    // hardware this should be set to 192.168.1.1:51003 per the XLink spec;
    // for sim it's the same 10050 the OBC uses.
    std::string xlink_host = "127.0.0.1";
    int         xlink_port = 10050;
    // 32-hex product key (four U32 words in "aabbccdd-eeff..." form).  Empty
    // means "use zero key" — fine for the sim, real hardware will NAK.
    std::string xlink_key;
    // ADCS TCs as a bare escaped body, without the 1F 7F / 1F FF delimiters:
    // the flight OBC adds those itself on the way to the ADCS UART.  Unset =
    // auto, which turns it on only for a real OBC (routing via a non-loopback
    // OBC host), so every simulator topology keeps sending complete frames.
    std::optional<bool> adcs_bare_tc;
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

// ─── classification (same as before) ────────────────────────────────────────
enum class TcKind { SET, GET, ACTION };
static TcKind classify_tc(const std::string& name) {
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
static std::string tag_str(TcKind k) {
    switch (k) {
        case TcKind::SET:    return color::set_tag("[SET]");
        case TcKind::GET:    return color::get_tag("[GET]");
        case TcKind::ACTION: return color::act_tag("[ACT]");
    }
    return "[???]";
}

// ─── small input utilities ──────────────────────────────────────────────────
static std::string read_line(const std::string& prompt) {
    std::cout << color::hint(prompt);
    std::cout.flush();
    std::string s;
    if (!std::getline(std::cin, s)) return "";
    return s;
}
static std::string trim(std::string s) {
    size_t i = 0, j = s.size();
    while (i < j && std::isspace((unsigned char)s[i])) ++i;
    while (j > i && std::isspace((unsigned char)s[j-1])) --j;
    return s.substr(i, j - i);
}
static std::optional<int> parse_int(const std::string& s) {
    try { size_t e=0; int v = std::stoi(s, &e); if (e==s.size()) return v; }
    catch (...) {}
    return std::nullopt;
}
static std::string fmt_bytes(const Bytes& b) {
    std::ostringstream o; o << std::hex << std::setfill('0');
    for (auto x : b) o << std::setw(2) << (int)x << ' ';
    return o.str();
}

// ─── case-insensitive substring search ──────────────────────────────────────
static bool icontains(const std::string& haystack, const std::string& needle) {
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

// ─── connecting ─────────────────────────────────────────────────────────────
static bool connect_session(session::Session& sess, const GsConfig& cfg) {
    if (!cfg.xlink_key.empty()) {
        auto k = xdos::xlink::codec::parse_key_hex(cfg.xlink_key);
        if (k) sess.set_xlink_key(*k);
        else std::cerr << color::warn("[GS] --xlink-key: bad hex, ignoring\n");
    }

    if (cfg.via_obc) {
        // One physical link: GS ↔ OBC.  From the ground station's point of
        // view the subsystems are not buses — they are APIDs inside the
        // frames.  A single TCP connection carries every TC and TM.
        sess.set_shared_link(true);
        sess.configure(Subsystem::ADCS, { cfg.obc_host, cfg.obc_port });  // shared slot
        bool ok = sess.connect(Subsystem::ADCS);                          // opens the OBC link
        std::string tag = "  OBC link " + cfg.obc_host + ":"
                        + std::to_string(cfg.obc_port);
        if (ok) {
            std::cout << tag << "  " << color::ok("CONNECTED") << "\n";
        } else {
            std::cout << tag << "  " << color::err("FAILED") << "\n"
                      << color::hint("    check power sequence (EPS kill switch on, "
                                     "OBC booted and listening), cable, GS IP on the "
                                     "OBC subnet, port\n");
        }
        return ok;
    }

    // --no-obc: genuine bus-per-subsystem simulator topology — separate
    // endpoints, so per-bus status lines remain meaningful here.
    sess.configure(Subsystem::ADCS,    { "127.0.0.1", cfg.adcs_port });
    sess.configure(Subsystem::EPS,     { "127.0.0.1", cfg.eps_port  });
    sess.configure(Subsystem::GPS,     { "127.0.0.1", cfg.gps_port  });
    sess.configure(Subsystem::UHF,     { "127.0.0.1", cfg.uhf_port  });
    sess.configure(Subsystem::SENSOR,  { "127.0.0.1", cfg.sensor_port });
    // Payload is deliberately NOT configured here: payload TC/TM is routed
    // through the OBC only, so there is no direct GS→payload-server link.
    // The OBC keeps its own connection to the payload server (port 10040).
    // XLINK-S direct (useful when talking to real XLink hardware on
    // 192.168.1.1:51003).
    sess.configure(Subsystem::XLINK, { cfg.xlink_host, cfg.xlink_port });

    bool any = false;
    for (auto s : { Subsystem::ADCS, Subsystem::EPS, Subsystem::GPS,
                    Subsystem::UHF, Subsystem::SENSOR }) {
        bool ok = sess.connect(s);
        std::string tag = "  " + std::string(to_string(s)) + " direct 127.0.0.1";
        if (ok) {
            std::cout << tag << "  " << color::ok("OK") << "\n";
            any = true;
        } else {
            std::cout << tag << "  " << color::err("FAILED") << "\n";
        }
    }
    bool xok = sess.connect(Subsystem::XLINK);
    std::string xtag = std::string("  XLINK-S direct ")
        + cfg.xlink_host + ":" + std::to_string(cfg.xlink_port);
    std::cout << xtag << "  "
              << (xok ? color::ok("OK")
                      : color::warn("not reachable — start xdos_xlink_sim / check XLink transceiver"))
              << "\n";
    if (xok) any = true;
    return any;
}

// ─── banner and menus ───────────────────────────────────────────────────────
static void print_banner(const GsConfig& cfg) {
    std::cout << color::header(
        "╔══════════════════════════════════════════════════════════╗") << "\n";
    std::cout << color::header(
        "║              XDOS Ground Station — Level 1               ║") << "\n";
    std::cout << color::header(
        "╚══════════════════════════════════════════════════════════╝") << "\n";
    std::cout << "  Link: " << (cfg.via_obc
        ? color::ok("via OBC ") + color::addr(cfg.obc_host + ":"
                                              + std::to_string(cfg.obc_port))
        : color::warn("direct to subsystem sims (no OBC)")) << "\n";
}

// ─── flight power monitor (EGSE umbilical) ──────────────────────────────────
// Background poller that watches the EPS (kill switch, battery, orbit,
// channel states) and the OBC (boot FSM) over the EGSE sim-control link —
// the bench harness path that works even when the OBC is unpowered.
// Drives the colored status bar and the [p] power menu, and flashes a line
// whenever something changes state.
struct PowerSnap {
    bool             eps_ok = false, obc_ok = false;
    egse::EpsStatus  eps{};
    egse::ObcStatus  obc{};
};

class PowerMonitor {
public:
    struct Item { std::string name; Subsystem sub; std::uint16_t bits = 0; std::string chans; };

    PowerMonitor(const mib::Mib& mib, FlightConfig fc) : fc_(std::move(fc)) {
        auto sub_of = [](const std::string& n) {
            if (n == "ADCS")    return Subsystem::ADCS;
            if (n == "GPS")     return Subsystem::GPS;
            if (n == "SBAND")   return Subsystem::XLINK;
            if (n == "PAYLOAD") return Subsystem::Payload;
            if (n == "UHF")     return Subsystem::UHF;
            return Subsystem::EPS;      // unmapped names: no TC-gate binding
        };
        for (const auto& e : fc_.power_menu) {
            Item it; it.name = e.name; it.sub = sub_of(e.name);
            it.bits = FlightConfig::bits(e.channels);
            for (int ch : e.channels) {
                auto* c = mib.channel(Subsystem::EPS, ch);
                it.chans += (it.chans.empty() ? "CH" : ", CH") + std::to_string(ch)
                          + (c ? " " + c->subsystem : "");
            }
            if (it.bits) items_.push_back(std::move(it));
        }
    }
    ~PowerMonitor() { stop(); }

    void start() {
        if (running_.exchange(true)) return;
        th_ = std::thread([this] {
            while (running_.load()) {
                poll();
                for (int i = 0; i < 20 && running_.load(); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }
    void stop() {
        if (!running_.exchange(false)) return;
        if (th_.joinable()) th_.join();
    }

    PowerSnap snap() const { std::lock_guard<std::mutex> g(m_); return s_; }
    const std::vector<Item>& items() const { return items_; }

    void refresh_now() { poll(/*announce=*/false); }

    bool send_kill(bool on) {
        transport::TcpClient c;
        if (!c.connect(fc_.eps_host, fc_.eps_port, 400)) return false;
        if (!c.send(egse::make_kill_set(on))) return false;
        auto rep = c.recv(16, 500);
        refresh_now();
        return egse::parse_cmd(rep) == egse::REP_KILL_SET;
    }

    // 1 = powered, 0 = off, -1 = unknown (no data / not a mapped subsystem)
    int powered(Subsystem s) const {
        auto ps = snap();
        if (!ps.eps_ok) return -1;
        for (const auto& it : items_)
            if (it.sub == s) return (ps.eps.chan_mask & it.bits) ? 1 : 0;
        return -1;
    }

private:
    void poll(bool announce = true) {
        PowerSnap n;
        {
            transport::TcpClient c;
            if (c.connect(fc_.eps_host, fc_.eps_port, 300) &&
                c.send(egse::make_req(egse::CMD_EPS_STATUS))) {
                if (auto st = egse::parse_eps_status(c.recv(64, 400))) {
                    n.eps_ok = true; n.eps = *st;
                }
            }
        }
        {
            // OBC status rides a DEDICATED port (routing port + 1) so these
            // frequent polls never contend with TC/TM traffic on the main port.
            transport::TcpClient c;
            if (c.connect(fc_.obc_host, fc_.obc_port + 1, 300) &&
                c.send(egse::make_req(egse::CMD_OBC_STATUS))) {
                if (auto st = egse::parse_obc_status(c.recv(32, 400))) {
                    n.obc_ok = true; n.obc = *st;
                }
            }
        }
        PowerSnap prev;
        {
            std::lock_guard<std::mutex> g(m_);
            prev = s_; s_ = n; have_prev_ = have_ever_ || have_prev_;
        }
        if (announce && have_ever_) announce_changes(prev, n);
        have_ever_ = true;
    }

    void announce_changes(const PowerSnap& a, const PowerSnap& b) {
        auto say = [](const std::string& s) {
            std::cout << "\r\033[K  " << s << "\n" << std::flush;
        };
        if (a.eps_ok && b.eps_ok && a.eps.kill != b.eps.kill)
            say(b.eps.kill ? color::ok("⏻ kill switch → ON — bus energized")
                           : color::err("⏻ kill switch → OFF — bus dead"));
        if (a.obc_ok && b.obc_ok && a.obc.state != b.obc.state)
            say("OBC → " + std::string(egse::obc_state_name(b.obc.state)));
        if (a.eps_ok && b.eps_ok && a.eps.mode != b.eps.mode)
            say("EPS mode → " + std::string(egse::eps_mode_name(b.eps.mode)));
        if (a.eps_ok && b.eps_ok) {
            for (const auto& it : items_) {
                bool was = a.eps.chan_mask & it.bits, now = b.eps.chan_mask & it.bits;
                if (was != now)
                    say((now ? color::ok("● ") : color::err("● ")) + it.name
                        + " power → " + (now ? color::ok("ON") : color::err("OFF")));
            }
        }
    }

    FlightConfig            fc_;
    std::vector<Item>       items_;
    mutable std::mutex      m_;
    PowerSnap               s_;
    bool                    have_prev_ = false, have_ever_ = false;
    std::atomic<bool>       running_ { false };
    std::thread             th_;
};

static PowerMonitor* g_pm = nullptr;

static std::string fmt_mmss(int s) {
    char b[16]; std::snprintf(b, sizeof(b), "%d:%02d", s / 60, s % 60);
    return b;
}

// Colored one-line power/orbit summary shown above the main menu.
static std::string render_power_bar(PowerMonitor& pm) {
    auto ps = pm.snap();
    auto dot = [](int v, const char* label, bool booting = false) {
        std::string d = booting ? color::warn("●")
                      : v == 1  ? color::ok("●")
                      : v == 0  ? color::err("●") : color::hint("·");
        return d + " " + std::string(label);
    };
    int obc = -1; bool obc_boot = false; std::string obc_extra;
    if (ps.obc_ok) {
        using S = egse::ObcState;
        obc = (ps.obc.state == S::On) ? 1 : 0;
        obc_boot = (ps.obc.state == S::Booting || ps.obc.state == S::Sequencing);
        if (ps.obc.state == S::Booting)
            obc_extra = color::warn(" boot " + std::to_string(ps.obc.countdown_s) + "s");
        else if (ps.obc.state == S::Sequencing) {
            static const std::size_t seq_total =
                FlightConfig::load().default_on_sequence.size();
            obc_extra = color::warn(" seq " + std::to_string(ps.obc.seq_done)
                                    + "/" + std::to_string(seq_total));
        }
    }
    auto bit = [&](Subsystem s) { return pm.powered(s); };

    std::string line = "  " + dot(bit(Subsystem::ADCS), "ADCS")
        + "  " + dot(ps.eps_ok ? 1 : 0, "EPS")
        + "  " + dot(obc, "OBC", obc_boot) + obc_extra
        + "  " + dot(bit(Subsystem::XLINK), "SBAND")
        + "  " + dot(bit(Subsystem::GPS), "GPS")
        + "  " + dot(bit(Subsystem::UHF), "UHF")
        + "  " + dot(bit(Subsystem::Payload), "PAYLOAD");

    if (ps.eps_ok) {
        char b[160];
        std::snprintf(b, sizeof(b), "   |  %.2f V · %s · %s %s → %s · kill %s",
                      ps.eps.vbatt_mv / 1000.0,
                      egse::eps_mode_name(ps.eps.mode),
                      ps.eps.phase == 0 ? "☀ sunlit" : "☾ eclipse",
                      fmt_mmss(ps.eps.phase_remaining_s).c_str(),
                      ps.eps.phase == 0 ? "☾" : "☀",
                      "");
        line += color::hint(b);
        line += ps.eps.kill ? color::ok("ON") : color::err("OFF");
    } else {
        line += color::hint("   |  EPS EGSE unreachable");
    }
    return line;
}

static void print_main_menu(bool ccsds_on, bool ccsds_loaded,
                            session::Session* sess = nullptr,
                            bool has_history = false,
                            bool via_obc = true) {
    // Connection status indicators
    std::string status_line;
    if (sess) {
        auto dot = [&](Subsystem s, const char* label) {
            bool on = sess->connected(s);
            std::string d = on ? color::ok("●") : color::err("○");
            return d + " " + std::string(label);
        };
        status_line = "  " + dot(Subsystem::ADCS, "ADCS")
                    + "  " + dot(Subsystem::EPS, "EPS")
                    + "  " + dot(Subsystem::GPS, "GPS")
                    + "  " + dot(Subsystem::Payload, "Payload")
                    + "  " + dot(Subsystem::XLINK, "XLINK")
                    + "  " + dot(Subsystem::UHF, "UHF")
                    + "  " + dot(Subsystem::SENSOR, "SENSOR");
    }
    if (g_pm) std::cout << "\n" << render_power_bar(*g_pm) << "\n";
    std::cout << "\n" << color::header("What would you like to do?")
              << (status_line.empty() ? "" : "     " + status_line) << "\n"
              << "  [1] Send TC to ADCS\n"
              << "  [2] Send TC to EPS\n"
              << "  [3] Send TC to GPS\n";
    if (ccsds_on && via_obc) {
        std::cout << "  [4] Send TC to Payload  " << color::hint("(Payload Server commands, via OBC)") << "\n";
    }
    std::cout << "  [5] Send TC to XLINK-S " << color::hint("(RF transceiver, via OBC)") << "\n";
    std::cout << "  [6] Send TC to UHF " << color::hint("(437 MHz OpenLST transceiver)") << "\n";
    std::cout << "  [7] Send TC to SENSOR " << color::hint("(sensor controller: PS/ES board power, SSD select, temperatures)") << "\n";
    std::cout << "  [p] Power control " << color::hint("(kill switch, subsystem ON/OFF, orbit)") << "\n";
    std::cout << "  [g] Binary rate-group LUT " << color::hint("(CSV import / pick editor -> build + uplink .bin)") << "\n";
    if (ccsds_loaded) {
        std::cout << "  [c] CCSDS framing: "
                  << (ccsds_on ? color::ok("ON") : color::warn("OFF")) << "\n";
    }
    if (has_history) {
        std::cout << "  [!] Repeat recent command\n";
    }
    std::cout << "  [h] Help\n"
              << "  [r] Reconnect buses\n"
              << "  [q] Quit\n";
    std::cout << color::hint("> ");
    std::cout.flush();
}

// ─── TC listing ─────────────────────────────────────────────────────────────
// Optionally filter by kind (nullopt = no filter).  For GPS, also synthesize
// GET TCs for every TM (LOG-wrap).  For ADCS, also synthesize GET TCs for
// every TM (empty-payload request).
struct TcEntry {
    int                 id;
    int                 wire_id;
    std::string         name;
    std::string         param_id;                // e.g. "EP-019" if present in MIB
    std::optional<std::string> node;
    TcKind              kind;
    bool                synthesized = false;   // GPS LOG-wrap or ADCS empty-req
};

static std::vector<TcEntry> all_tcs(const mib::Mib& mib, Subsystem sub) {
    std::vector<TcEntry> out;
    for (auto const* m : mib.messages(sub, Direction::TC)) {
        out.push_back({ m->msg_id, m->wire_id, m->name, m->param_id, m->node,
                        classify_tc(m->name), false });
    }
    if (sub == Subsystem::GPS || sub == Subsystem::ADCS) {
        for (auto const* tm : mib.messages(sub, Direction::TM)) {
            out.push_back({ tm->msg_id, tm->wire_id, tm->name, tm->param_id, tm->node,
                            TcKind::GET, true });
        }
    }
    return out;
}

static void list_tcs_clean(const std::vector<TcEntry>& entries,
                           Subsystem sub,
                           std::optional<TcKind> filter,
                           size_t page, size_t page_size,
                           size_t& total_match_out, size_t& end_out) {
    total_match_out = 0;
    for (auto const& e : entries) if (!filter || e.kind == *filter) ++total_match_out;
    size_t start = page * page_size;
    size_t end   = std::min(start + page_size, total_match_out);
    end_out = end;

    size_t match_idx = 0;
    for (auto const& e : entries) {
        if (filter && e.kind != *filter) continue;
        if (match_idx < start) { ++match_idx; continue; }
        if (match_idx >= end) break;
        ++match_idx;

        std::ostringstream l;
        l << "  " << tag_str(e.kind);
        if (!e.param_id.empty()) {
            // EPS-style: show spec-level id ("EP-003") prominently; numeric
            // msg_id (which collides across PDU/BMS/MPPT) is hidden — type
            // "EP-3" or "3" at the prompt, the CLI will resolve.
            l << "  " << std::setw(8) << std::left << e.param_id;
        } else {
            l << "  ID " << std::setw(4) << std::right << e.id;
        }
        l << "  " << (e.node ? "[" + *e.node + "] " : "      ")
          << e.name;
        if (e.synthesized) {
            l << "  " << color::hint(sub == Subsystem::GPS
                ? "(via LOG)"
                : "(empty-payload TM request)");
        }
        l << "\n";
        std::cout << l.str();
    }
}

// ─── TC details (`?ID` at the ID prompt) ────────────────────────────────────
static void show_tc_details(const mib::Mib& mib, Subsystem sub, int id) {
    // Search native TCs first
    auto* m = mib.subsystem(sub);
    if (!m) { std::cout << color::err("  subsystem not in MIB\n"); return; }
    const mib::Message* hit = nullptr;
    Direction dir = Direction::TC;
    for (auto const& mm : m->messages) {
        if (mm.msg_id == id) {
            hit = &mm; dir = mm.direction;
            if (mm.direction == Direction::TC) break;   // prefer TC
        }
    }
    if (!hit) {
        std::cout << color::err("  ID " + std::to_string(id) + " not found\n");
        return;
    }
    std::cout << "\n" << color::header(std::string("--- ")
        + to_string(sub) + " "
        + (dir == Direction::TC ? "TC" : "TM")
        + " ID " + std::to_string(id) + " : " + hit->name + " ---") << "\n";
    if (dir == Direction::TC)
        std::cout << "  kind        : " << tag_str(classify_tc(hit->name)) << "\n";
    std::cout << "  wire id     : 0x" << std::hex << hit->wire_id
              << std::dec << "\n";
    std::cout << "  length      : "
              << (hit->is_variable_length ? "variable"
                  : (hit->length_bytes
                       ? std::to_string(*hit->length_bytes) + " B"
                       : "?")) << "\n";
    if (hit->node)        std::cout << "  board       : " << *hit->node << "\n";
    if (!hit->doc_reference.empty()) std::cout << "  doc         : " << hit->doc_reference << "\n";
    if (!hit->description.empty())
        std::cout << "  description : " << hit->description << "\n";
    std::cout << "  parameters  : " << hit->parameters.size() << "\n";
    int i = 1;
    for (auto const& p : hit->parameters) {
        std::ostringstream l;
        l << "    " << std::setw(2) << i++
          << "  +" << std::setw(4)
          << (p.offset ? std::to_string(*p.offset) : "?")
          << "  " << std::setw(3) << p.length << "B"
          << "  " << std::setw(7) << std::left << p.data_type
          << "  " << p.name;
        if (p.unit) l << "  [" << *p.unit << "]";
        l << "\n";
        std::cout << l.str();
    }
}



// Heuristic: if a 1-byte UINT parameter's name reads like a status/flag
// (RBF, HDRM, timeout, release, etc.), return symbolic labels for {1, 0}.
// Empty pair = use the default numeric display.


// UINT parameters named like "CH1 to CH8 ..." and "CH9 to CH16 ...".  Returns
// the parameter-name keyword that identifies them (e.g. "Enable/Disable"
// or "Over Current Flag") or empty if not present.
static std::string detect_eps_channel_mask_kind(const mib::Message& tm) {
    std::string lo_name, hi_name;
    for (auto const& p : tm.parameters) {
        if (p.name.rfind("CH1 to CH8",  0) == 0) lo_name = p.name;
        if (p.name.rfind("CH9 to CH16", 0) == 0) hi_name = p.name;
    }
    if (lo_name.empty() || hi_name.empty()) return {};
    // Find the trailing keyword after the channel range
    auto tail = lo_name.find("CH8") + 3;
    while (tail < lo_name.size() && (lo_name[tail] == ' ' || lo_name[tail] == ':')) ++tail;
    return lo_name.substr(tail);
}

// Pick human-readable labels for the channel-state bits based on the kind
// of message (Enable/Disable, Over Current Flag, etc.).  Returns
// {label_when_set, label_when_clear}.
static std::pair<std::string, std::string> channel_state_labels(const std::string& kind) {
    std::string k;
    for (char c : kind) k.push_back(static_cast<char>(std::tolower(c)));
    if (k.find("enable") != std::string::npos || k.find("disable") != std::string::npos)
        return {"ON ", "OFF"};
    if (k.find("over current") != std::string::npos || k.find("overcurrent") != std::string::npos)
        return {"OVERCURRENT", "ok         "};
    if (k.find("fault") != std::string::npos)
        return {"FAULT", "ok   "};
    return {"SET ", "clr "};                  // generic
}

// Detect an MPPT bitmask pair: parameter names "MPPT1 to MPPT8 ..." and
// "MPPT9 to MPPT10 ...".  Returns the kind suffix or empty.
static std::string detect_eps_mppt_mask_kind(const mib::Message& tm) {
    std::string lo_name, hi_name;
    for (auto const& p : tm.parameters) {
        if (p.name.rfind("MPPT1 to MPPT8",  0) == 0) lo_name = p.name;
        if (p.name.rfind("MPPT9 to MPPT10", 0) == 0) hi_name = p.name;
    }
    if (lo_name.empty() || hi_name.empty()) return {};
    auto tail = lo_name.find("MPPT8") + 5;
    while (tail < lo_name.size() && (lo_name[tail] == ' ' || lo_name[tail] == ':')) ++tail;
    // Strip trailing "(...)" if present
    auto paren = lo_name.find('(', tail);
    std::string kind = (paren == std::string::npos)
        ? lo_name.substr(tail)
        : lo_name.substr(tail, paren - tail);
    // trim trailing whitespace
    while (!kind.empty() && (kind.back() == ' ' || kind.back() == '\t')) kind.pop_back();
    return kind;
}

// Render the 10-MPPT state with clear ON/OFF labels.  NL16U uses MPPT1-MPPT7;
// MPPT8-MPPT10 exist in the protocol but aren't wired (we note that).
static void render_eps_mppt_state(Logger& log, const json& vals,
                                  const std::string& title) {
    int lo = 0, hi = 0;
    for (auto it = vals.begin(); it != vals.end(); ++it) {
        if (it.key().rfind("MPPT1 to MPPT8",  0) == 0 && it->is_number_integer())
            lo = it->get<int>();
        if (it.key().rfind("MPPT9 to MPPT10", 0) == 0 && it->is_number_integer())
            hi = it->get<int>();
    }
    unsigned mask = (unsigned(hi) << 8) | unsigned(lo);
    auto [lbl_set, lbl_clr] = channel_state_labels(title);
    std::ostringstream hdr;
    hdr << "    " << title << "  (mask=0x" << std::hex << std::setw(4)
        << std::setfill('0') << mask << std::setfill(' ') << std::dec << ")";
    log << color::header(hdr.str()) << "\n";
    for (int i = 0; i < 10; ++i) {
        int id = i + 1;
        bool on = (mask >> i) & 1;
        std::ostringstream l;
        l << "      MPPT " << std::setw(2) << id;
        if (id <= 7) l << " (solar panel " << id << ")    ";
        else         l << " (unused on NL16U)    ";
        l << " : " << (on ? lbl_set : lbl_clr);
        log << "  " << (on ? color::ok(l.str()) : color::hint(l.str())) << "\n";
    }
}


//   Channel  1 (OBC-A)        : ON
//   Channel  2 (OBC-B)        : OFF
//   ...
static void render_eps_channel_state(Logger& log, const json& vals,
                                     const mib::Mib& mib,
                                     const std::string& title) {
    int lo = 0, hi = 0;
    for (auto it = vals.begin(); it != vals.end(); ++it) {
        if (it.key().rfind("CH1 to CH8",  0) == 0 && it->is_number_integer())
            lo = it->get<int>();
        if (it.key().rfind("CH9 to CH16", 0) == 0 && it->is_number_integer())
            hi = it->get<int>();
    }
    unsigned mask = (unsigned(hi) << 8) | unsigned(lo);
    auto [lbl_set, lbl_clr] = channel_state_labels(title);
    std::ostringstream hdr;
    hdr << "    " << title << "  (mask=0x" << std::hex << std::setw(4)
        << std::setfill('0') << mask << std::setfill(' ') << std::dec << ")";
    log << color::header(hdr.str()) << "\n";
    for (int i = 0; i < 16; ++i) {
        int id = i + 1;
        bool on = (mask >> i) & 1;
        auto* ch = mib.channel(Subsystem::EPS, id);
        std::string subsys = ch ? ch->subsystem : std::string("?");
        std::ostringstream l;
        l << "      Channel " << std::setw(2) << id
          << " (" << std::setw(14) << std::left << subsys << ")"
          << " : " << (on ? lbl_set : lbl_clr);
        log << "  " << (on ? color::ok(l.str()) : color::hint(l.str())) << "\n";
    }
}


// Detect a channel-mask parameter (e.g. "CH1 to CH8 Enable/Disable").
// Returns the (lo, hi) channel range if matched, else (0, 0).
static std::pair<int, int> channel_mask_range(const std::string& name) {
    if (name.rfind("CH", 0) != 0) return {0, 0};
    int lo = 0, hi = 0;
    if (std::sscanf(name.c_str(), "CH%d to CH%d", &lo, &hi) == 2 && lo > 0 && hi > lo) {
        return {lo, hi};
    }
    return {0, 0};
}

// Prompt the operator for a channel-mask byte by listing channels with their
// subsystem names.  Accepts:
//   <empty>, 0, none, n   → 0   (no channels)
//   all, a, 255, 0xFF     → 0xFF (all 8 channels in this byte)
//   0xNN                  → hex byte
//   1,3,5                 → comma-separated channel list
static int prompt_channel_mask_byte(const mib::Mib& mib, int lo, int hi) {
    std::cout << color::hint("    Channels in this byte:\n");
    for (int id = lo; id <= hi; ++id) {
        auto* ch = mib.channel(Subsystem::EPS, id);
        std::string subsys = ch ? ch->subsystem : std::string("?");
        std::cout << "        Channel " << std::setw(2) << id
                  << " (" << std::setw(14) << std::left << subsys << ")"
                  << " bit=" << (id - lo) << "\n";
    }
    std::cout << color::hint(
        "    Enter channels to ENABLE — formats:\n"
        "      <Enter>, 0, none      → no channels\n"
        "      all, 255              → all 8 channels in this byte\n"
        "      0xFF                  → hex byte\n"
        "      1,3,5                 → comma-separated channel list\n");
    std::cout << "    > ";
    std::cout.flush();
    std::string s;
    if (!std::getline(std::cin, s)) return 0;
    s = trim(s);
    std::string lcs;
    for (char c : s) lcs.push_back(static_cast<char>(std::tolower(c)));

    if (lcs.empty() || lcs == "0" || lcs == "none" || lcs == "n") {
        std::cout << color::hint("    → no channels enabled in this byte\n");
        return 0;
    }
    if (lcs == "all" || lcs == "a" || lcs == "255" || lcs == "0xff") {
        std::cout << color::ok("    → all channels CH" + std::to_string(lo)
                               + "-CH" + std::to_string(hi) + " enabled\n");
        return 0xFF;
    }
    if (lcs.rfind("0x", 0) == 0) {
        try {
            int v = std::stoi(lcs.substr(2), nullptr, 16) & 0xFF;
            std::ostringstream o; o << "    → hex mask 0x" << std::hex << v << "\n";
            std::cout << color::hint(o.str());
            return v;
        } catch (...) { return 0; }
    }

    int mask = 0;
    std::vector<int> enabled;
    std::string buf;
    auto consume = [&](const std::string& t) {
        auto str = trim(t);
        if (str.empty()) return;
        try {
            int id = std::stoi(str);
            if (id >= lo && id <= hi) { mask |= (1 << (id - lo)); enabled.push_back(id); }
            else std::cout << color::warn("    (ignoring channel " + std::to_string(id)
                                          + ": out of range " + std::to_string(lo)
                                          + "-" + std::to_string(hi) + ")\n");
        } catch (...) {}
    };
    for (char c : s) {
        if (c == ',' || c == ' ') { consume(buf); buf.clear(); }
        else buf.push_back(c);
    }
    consume(buf);
    if (enabled.empty()) {
        std::cout << color::hint("    → no channels enabled in this byte\n");
    } else {
        std::ostringstream s2;
        s2 << "    → enabling: ";
        for (size_t i = 0; i < enabled.size(); ++i) {
            if (i) s2 << ", ";
            int id = enabled[i];
            auto* ch = mib.channel(Subsystem::EPS, id);
            s2 << "CH" << id;
            if (ch) s2 << " (" << ch->subsystem << ")";
        }
        s2 << "\n";
        std::cout << color::ok(s2.str());
    }
    return mask;
}

// Render an enum's legal values as aligned columns, so the operator never has
// to guess what "1B ENUM" will accept.  This matters more than it looks: the
// sensor controller numbers the same board differently in different commands
// (board_type has PS=0, hw_instance has PS=1), so an unlabelled prompt is an
// invitation to power off the wrong board.
static void print_enum_values(const mib::EnumType& et, std::size_t max_show) {
    const std::size_t n = et.values.size();
    const bool capped = max_show > 0 && n > max_show;
    const std::size_t shown = capped ? max_show : n;

    std::size_t w = 0;
    for (std::size_t i = 0; i < shown; ++i) {
        std::ostringstream t;
        t << et.values[i].value << " = " << et.values[i].name;
        w = std::max(w, t.str().size());
    }
    const std::size_t per_line = std::max<std::size_t>(1, 72 / (w + 4));

    std::ostringstream line;
    for (std::size_t i = 0; i < shown; ++i) {
        if (i % per_line == 0) line << "      ";
        std::ostringstream t;
        t << et.values[i].value << " = " << et.values[i].name;
        line << std::setw(static_cast<int>(w + 4)) << std::left << t.str();
        if ((i + 1) % per_line == 0 || i + 1 == shown) line << "\n";
    }
    std::cout << color::hint(line.str());
    if (capped) {
        std::cout << color::hint("      ... " + std::to_string(n - shown) +
                                 " more — type [?] to list all\n");
    }
}

// The full listing, with the spec's description for each value.
static void print_enum_details(const mib::EnumType& et) {
    std::cout << color::header("  " + et.name)
              << color::hint(et.doc_reference.empty()
                                 ? std::string("\n")
                                 : "  — " + et.doc_reference + "\n");
    for (auto const& ev : et.values) {
        std::ostringstream l;
        l << "      " << std::setw(4) << std::right << ev.value << "  "
          << std::setw(24) << std::left << ev.name;
        std::cout << l.str() << color::hint(ev.description) << "\n";
    }
}

// Read one parameter value, re-prompting until it is something we understand.
// An unparseable entry used to fall back to zero silently — which for an
// ENUM meant the first enumerator went out on the wire unannounced.
static json prompt_one_value(const mib::Parameter& p) {
    const bool is_float = (p.data_type == "FLOAT" || p.data_type == "DOUBLE");
    const bool is_enum  = (p.data_type == "ENUM" && p.enum_type != nullptr);

    std::ostringstream head;
    head << "  " << std::setw(38) << std::left << p.name
         << " (" << p.length << "B " << p.data_type
         << (p.unit ? (", " + *p.unit) : std::string{}) << ")";
    std::cout << head.str() << "\n";
    if (is_enum) print_enum_values(*p.enum_type, 12);

    const std::string prompt = is_enum
        ? std::string("      value or name [0]: ")
        : std::string("      value [0]: ");

    for (;;) {
        std::string s = trim(read_line(prompt));
        if (s.empty()) return is_float ? json(0.0) : json(0);

        if (is_enum) {
            if (s == "?") { print_enum_details(*p.enum_type); continue; }
            // Name match first, case-insensitively — "ps", "PS" and "Ps" all
            // mean the same board.
            std::string want;
            for (char c : s) want.push_back(
                static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            for (auto const& ev : p.enum_type->values) {
                std::string have;
                for (char c : ev.name) have.push_back(
                    static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
                if (have == want) return json(ev.value);
            }
        }

        try {
            if (is_float) return json(std::stod(s));
            auto v = std::stoll(s, nullptr, 0);
            if (is_enum && !p.enum_type->by_value(v)) {
                std::cout << color::warn("      " + std::to_string(v) +
                                         " is not a defined value of " +
                                         p.enum_type->name +
                                         " — sending it anyway.\n");
            }
            return json(v);
        } catch (...) {
            std::cout << color::err("      Not recognised. ")
                      << color::hint(is_enum
                            ? "Enter a number or one of the names above, "
                              "[?] for details, [Enter] for 0.\n"
                            : "Enter a number, or [Enter] for 0.\n");
        }
    }
}

static json prompt_parameters(const mib::Message& tc, const mib::Mib& mib) {
    json out = json::object();
    if (tc.parameters.empty()) return out;
    std::cout << color::hint(std::string("  ") + std::to_string(tc.parameters.size())
                             + " parameter(s); press <Enter> for default-zero\n");
    for (auto const& p : tc.parameters) {
        // Channel-mask parameter (e.g. "CH1 to CH8 Enable/Disable") — show
        // subsystem names and accept either hex or a comma-separated list
        auto [chlo, chhi] = channel_mask_range(p.name);
        if (chlo > 0 && tc.subsystem == Subsystem::EPS && p.length == 1) {
            std::cout << "  " << color::header(p.name) << "  "
                      << color::hint("[" + p.data_type + "]") << "\n";
            int mask = prompt_channel_mask_byte(mib, chlo, chhi);
            out[p.name] = mask;
            continue;
        }
        out[p.name] = prompt_one_value(p);
    }
    return out;
}

// ─── send + report ──────────────────────────────────────────────────────────
// Link identity (radio module + ground station) printed with every command,
// next to the TX/RX byte dumps.  The TC uplink frame carries only the GS-ID
// on the wire (there is no radio-module field on the uplink), so the TX side
// shows the link's configured radio module; the RX side prefers the RM-ID /
// GS-ID decoded from the actual 1432-byte TM downlink frame header.
// Defaults are overwritten in main() from the CCSDS profiles
// (ccsds_tc.json constants.ground_station_id,
//  ccsds_tm.json constants.default_radio_module_id).
static int g_link_gs_id    = 0;
static int g_link_radio_id = 1;

static void log_tx_link(Logger& log, const session::ExchangeResult& r) {
    int gs = r.ccsds_tx ? r.ccsds_tx->ground_station_id : g_link_gs_id;
    char buf[96];
    std::snprintf(buf, sizeof(buf),
        "  TX link   GS-ID=%d → Radio RM-ID=%d  (TC uplink)",
        gs, g_link_radio_id);
    log << color::hint(buf) << "\n";
}

static void log_rx_link(Logger& log, const session::ExchangeResult& r) {
    int rm = r.tm_frame ? r.tm_frame->radio_module_id : g_link_radio_id;
    int gs = r.tm_frame ? r.tm_frame->ground_station_id : g_link_gs_id;
    char buf[96];
    std::snprintf(buf, sizeof(buf),
        "  RX link   Radio RM-ID=%d → GS-ID=%d  (TM downlink)",
        rm, gs);
    log << color::hint(buf) << "\n";
}

// Fixed 1432-byte TM downlink frame (SOF FCA1): show the frame header so
// the operator sees SCID / RM / counters / APID / payload size / timestamp
// at a glance.  Shared by the generic subsystem flow and the XLINK-S flow.
static void log_tm_frame_header(Logger& log, const session::ExchangeResult& r) {
    if (!r.tm_frame) return;
    const auto& t = *r.tm_frame;
    char hdr[256];
    std::snprintf(hdr, sizeof(hdr),
        "  %s SCID=%d RM=%d  MFC=%d SFC=%d  APID=%d%s  seq=%d  payload=%dB  CRC %s",
        color::header("TM frame").c_str(),
        t.spacecraft_id, t.radio_module_id,
        t.master_frame_count, t.sub_frame_count, t.apid,
        t.subsystem ? (" (" + std::string(to_string(*t.subsystem)) + ")").c_str() : "",
        t.packet_sequence_cnt,
        static_cast<int>(r.rx_payload.size()),
        t.crc_ok ? "OK" : "BAD");
    log << hdr << "\n";
    char tline[128];
    std::snprintf(tline, sizeof(tline),
        "           time %04d-%02d-%02d %02d:%02d:%02d.%03d UTC  GS-ID=%d",
        t.time_code.year, t.time_code.month, t.time_code.day,
        t.time_code.hour, t.time_code.minute, t.time_code.second,
        t.time_code.milliseconds, t.ground_station_id);
    log << tline << "\n";
}

static void send_and_report(Logger& log, session::Session& sess,
                            Subsystem sub, const mib::Message& tc,
                            std::optional<std::string> node, const json& vals,
                            bool wrap_log = false, int log_msg_id = 0,
                            bool empty_payload_request = false,
                            const std::string& display_name = {}) {
    // ── flight power preflight ──────────────────────────────────────────
    // Warn (and let the operator abort) when the command is doomed: the OBC
    // isn't bridging yet, or the target subsystem's PDU channel is off.
    if (g_pm) {
        auto ps = g_pm->snap();
        std::string why;
        if (ps.obc_ok && ps.obc.state != egse::ObcState::On)
            why = "OBC is " + std::string(egse::obc_state_name(ps.obc.state))
                + (ps.eps_ok && !ps.eps.kill
                   ? " — turn the kill switch ON first ([p])"
                   : " — wait for boot/sequencing to finish");
        else if (g_pm->powered(sub) == 0)
            why = std::string(to_string(sub))
                + " is UNPOWERED — enable it in the power menu ([p])";
        if (!why.empty()) {
            std::cout << "\n  " << color::err("⚠ " + why) << "\n";
            auto a = read_line("  send anyway? [y/N] > ");
            if (a.empty() || (a[0] != 'y' && a[0] != 'Y')) {
                std::cout << color::hint("  (not sent)") << "\n";
                return;
            }
        }
    }

    session::ExchangeResult r;
    auto t0 = std::chrono::steady_clock::now();
    if (wrap_log) {
        // GPS auto-wrap as LOG(message_id=log_msg_id, trigger=ONCE)
        const mib::Message* log_tc = sess.mib().find(Subsystem::GPS, Direction::TC, "LOG");
        if (!log_tc) { log << color::err("  LOG TC missing from MIB\n"); return; }
        json wrap_vals = {
            {"port",framing::gps::kLogPort},{"message_id",log_msg_id},
            {"message_type",0},{"reserved",0},{"trigger",4},
            {"period",0.0},{"offset",0.0},{"hold",0}};
        r = sess.send_tc(sub, "LOG", wrap_vals, std::nullopt);
    } else if (empty_payload_request) {
        // ADCS TM-request via empty-payload TC frame
        r = sess.send_tm_request(sub, tc.name, node);
    } else {
        r = sess.send_tc(sub, tc.name, vals, node);
    }
    auto rtt = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - t0);

    log << "\n  " << color::header(
        std::string("▸ ") + (display_name.empty() ? tc.name : display_name))
        << (r.ccsds_used ? color::ok("  [CCSDS]") : std::string()) << "\n";

    // SET / ACT pre-send summary so the user sees exactly what was sent.
    // Kind is recoverable from the TC name prefix (Set_, Get_, plus a few ACTs).
    TcKind kind = classify_tc(tc.name);
    if (kind == TcKind::SET && !vals.is_null() && !vals.empty()) {
        log << "    " << color::hint("Setting: ");
        bool first = true;
        for (auto const& p : tc.parameters) {
            if (!vals.contains(p.name)) continue;
            // Skip the verbose 2-byte channel-mask bytes (we'd repeat info)
            if (p.name.rfind("CH1 to CH8",   0) == 0 ||
                p.name.rfind("CH9 to CH16",  0) == 0 ||
                p.name.rfind("MPPT1 to MPPT8",  0) == 0 ||
                p.name.rfind("MPPT9 to MPPT10", 0) == 0) continue;
            if (!first) log << ", ";
            first = false;
            log << p.name << "=" << format_value(vals[p.name], p);
        }
        // For channel-mask SETs, summarise which channels are being commanded ON
        if (sub == Subsystem::EPS) {
            int lo = vals.value("CH1 to CH8 Enable/Disable", 0);
            int hi = vals.value("CH9 to CH16 Enable/Disable", 0);
            unsigned m = (unsigned(hi) << 8) | unsigned(lo);
            if (m) {
                if (!first) log << "  ";
                log << "channels ON = ";
                bool firstch = true;
                for (int i = 0; i < 16; ++i) {
                    if (!((m >> i) & 1)) continue;
                    int id = i + 1;
                    if (!firstch) log << ", ";
                    firstch = false;
                    auto* ch = sess.mib().channel(Subsystem::EPS, id);
                    log << "CH" << id;
                    if (ch) log << " (" << ch->subsystem << ")";
                }
            }
        }
        log << "\n";
    } else if (kind == TcKind::ACTION) {
        log << "    " << color::hint("Action: " + tc.name) << "\n";
    }
    if (r.ccsds_used && r.ccsds_tx) {
        log << "  " << color::hint("    " + r.ccsds_tx->summary()) << "\n";
        log << "  " << color::hint("    native(in tc_data) ")
            << fmt_bytes(r.tx_native) << "\n";
    }
    log << "  TX " << fmt_bytes(r.tx_frame) << "\n";
    log_tx_link(log, r);

    // Show what arrived before reporting any error.  A framing or decode
    // failure on a frame that did reach us looks identical to silence unless
    // the bytes are on screen, which is exactly the wrong thing to hide.
    if (!r.rx_frame.empty()) {
        log << "  RX " << fmt_bytes(r.rx_frame) << "\n";
        log_rx_link(log, r);

        // Fixed 1432-byte TM downlink frame (SOF FCA1): header at a glance.
        log_tm_frame_header(log, r);
    }
    if (!r.error.empty()) {
        log << "  " << color::err("[BUS] ERROR: " + r.error) << "\n";
        return;
    }
    if (r.rx_frame.empty()) {
        log << "  " << color::warn("(no response within ")
            << rtt.count() << "ms)\n";
        return;
    }
    if (r.ccsds_used && r.ccsds_rx) {
        log << "  " << color::hint("    " + r.ccsds_rx->summary()) << "\n";
        log << "  " << color::hint("    native(in tc_data) ")
            << fmt_bytes(r.rx_native) << "\n";
    }
    if (!r.decoded_values.is_null() && !r.decoded_values.empty()) {
        // Try to look up the TM message in the MIB for per-field formatting
        const mib::Message* tm = r.response_msg;
        if (!tm && r.response_msg_id >= 0) {
            tm = sess.mib().by_node_wire(sub, Direction::TM, node ? std::optional<std::string_view>(*node) : std::nullopt,
                                         r.response_msg_id);
        }

        // EPS channel-state / over-current TM? Render combined 16-channel view first.
        bool rendered_channel_table = false;
        if (tm && sub == Subsystem::EPS) {
            auto kind = detect_eps_channel_mask_kind(*tm);
            if (!kind.empty()) {
                std::string title = "Channel " + kind;
                render_eps_channel_state(log, r.decoded_values, sess.mib(), title);
                rendered_channel_table = true;
            }
            // Also handle MPPT bitmask (10 MPPT channels, no subsystem names)
            auto mppt_kind = detect_eps_mppt_mask_kind(*tm);
            if (!mppt_kind.empty()) {
                render_eps_mppt_state(log, r.decoded_values, "MPPT " + mppt_kind);
                rendered_channel_table = true;
            }
        }

        log << "  " << color::header("decoded:") << "\n";
        // Iterate parameters in MIB order (which is spec order: seq 1, 2, 3…)
        // rather than alphabetical JSON-key order.  Also: detect and group
        // version-triplets and inject channel subsystem names.
        std::vector<std::string> handled;          // keys already shown via grouping
        if (tm) {
            // ── 1.  Group version triplets (Major/Minor/Patch) on one line ──
            auto try_version_group = [&](const std::string& prefix,
                                         const std::vector<std::string>& suffixes,
                                         const std::string& label) -> bool {
                std::vector<long long> nums;
                std::vector<std::string> keys;
                for (auto const& sfx : suffixes) {
                    std::string key = prefix + " " + sfx;
                    if (!r.decoded_values.contains(key)) return false;
                    const auto& v = r.decoded_values.at(key);
                    long long n = v.is_number_integer() ? v.get<long long>()
                                : v.is_number()         ? (long long)v.get<double>()
                                : 0;
                    nums.push_back(n);
                    keys.push_back(key);
                }
                std::ostringstream val, brk;
                for (size_t i = 0; i < nums.size(); ++i) {
                    if (i) val << ".", brk << ", ";
                    val << nums[i];
                    brk << suffixes[i] << " " << nums[i];
                }
                std::ostringstream l;
                l << "    " << std::setw(38) << std::left << label
                  << std::setw(14) << std::left << "[VERSION]"
                  << "  = " << val.str() << "  (" << brk.str() << ")\n";
                log << l.str();
                for (auto const& k : keys) handled.push_back(k);
                return true;
            };
            // EPS Get_FW_Version_RSP: combine Firmware Major/Minor/Patch and
            // Communication Protocol Major/Minor into single lines.
            try_version_group("Firmware", {"Major Version", "Minor Version", "Patch Version"},
                              "Firmware version");
            try_version_group("Communication Protocol",
                              {"Major Version", "Minor Version"},
                              "Communication protocol");
            // ADCS Identification: similarly group Firmware version Major/Minor + Runtime
            try_version_group("Firmware version", {"(Major)", "(Minor)"},
                              "Firmware version");

            // ── 2.  Walk parameters in MIB (spec) order ──────────────────
            for (auto const& p : tm->parameters) {
                const std::string& k = p.name;
                if (std::find(handled.begin(), handled.end(), k) != handled.end())
                    continue;
                // If we already rendered the friendly channel table, hide the
                // raw bitmask bytes (CH1 to CH8 = 9 means nothing to a human).
                if (rendered_channel_table &&
                    (k.rfind("CH1 to CH8",   0) == 0 ||
                     k.rfind("CH9 to CH16",  0) == 0 ||
                     k.rfind("MPPT1 to MPPT8",  0) == 0 ||
                     k.rfind("MPPT9 to MPPT10", 0) == 0)) continue;

                if (!r.decoded_values.contains(k)) continue;
                auto const& v = r.decoded_values.at(k);

                // Inject channel subsystem name for "CH<n> ..." parameters.
                // e.g. "CH4 Current Readings" → "CH4 (ADCS) Current Readings"
                std::string display = k;
                if (sub == Subsystem::EPS && k.size() >= 3 && k[0] == 'C' && k[1] == 'H'
                    && std::isdigit(static_cast<unsigned char>(k[2]))) {
                    std::size_t i = 2;
                    while (i < k.size() && std::isdigit(static_cast<unsigned char>(k[i]))) ++i;
                    int chid = std::stoi(k.substr(2, i - 2));
                    auto* ch = sess.mib().channel(Subsystem::EPS, chid);
                    if (ch) {
                        display = "CH" + std::to_string(chid) + " (" + ch->subsystem + ")"
                                + k.substr(i);
                    }
                }

                std::ostringstream l;
                l << "    " << std::setw(38) << std::left << display;
                std::string tag = "[" + p.data_type;
                if (p.unit) tag += " " + *p.unit;
                tag += "]";
                l << std::setw(14) << std::left << tag << "  = " << format_value(v, p);
                l << "\n";
                log << l.str();
            }
            // Show any keys that weren't in the MIB (rare; flagging is useful)
            for (auto it = r.decoded_values.begin(); it != r.decoded_values.end(); ++it) {
                if (it.key() == "_array") {
                    log << "    _array  = " << it->size() << " record(s)\n";
                    continue;
                }
                if (std::find(handled.begin(), handled.end(), it.key()) != handled.end())
                    continue;
                bool in_mib = false;
                for (auto const& p : tm->parameters) if (p.name == it.key()) { in_mib = true; break; }
                if (in_mib) continue;
                std::ostringstream l;
                l << "    " << std::setw(38) << std::left << it.key()
                  << std::setw(14) << std::left << " " << "  = " << it->dump() << "\n";
                log << l.str();
            }
            // UHF radio-config replies: append the engineering values the
            // CC1110 registers encode (carrier MHz, data rate, BW, ...).
            if (sub == Subsystem::UHF) {
                std::string dv = xdos::cli::uhf_derived_text(
                    tm->name, r.decoded_values, "    ");
                if (!dv.empty()) log << color::hint(dv);
            }
        } else {
            // No TM in MIB — fall back to raw JSON dump
            for (auto it = r.decoded_values.begin(); it != r.decoded_values.end(); ++it) {
                std::ostringstream l;
                l << "    " << std::setw(38) << std::left << it.key()
                  << "  = " << it->dump() << "\n";
                log << l.str();
            }
        }
    }
    // SET / ACT acknowledgement banner — check decoded TM for a Status field
    if ((kind == TcKind::SET || kind == TcKind::ACTION)
        && !r.decoded_values.is_null() && r.decoded_values.contains("Status")) {
        auto sv = r.decoded_values["Status"];
        long long s_val = 0;
        if (sv.is_number_integer())  s_val = sv.get<long long>();
        else if (sv.is_number())     s_val = static_cast<long long>(sv.get<double>());
        else if (sv.is_object() && sv.contains("value") && sv["value"].is_number())
                                     s_val = sv["value"].get<long long>();
        std::string who = node ? *node : "device";
        if (s_val == 1) {
            log << "  " << color::ok("✓ Acknowledged by " + who
                + ((kind == TcKind::SET) ? " — values written" : " — action executed"))
                << "\n";
        } else {
            log << "  " << color::err("✗ Rejected by " + who
                + " (Status=" + std::to_string(s_val) + ")") << "\n";
        }
    }
    auto now = std::time(nullptr); auto lt = *std::localtime(&now);
    char ts_buf[32];
    std::snprintf(ts_buf, sizeof(ts_buf), "%02d:%02d:%02d",
                  lt.tm_hour, lt.tm_min, lt.tm_sec);
    log << "  " << color::hint(std::string("Sent ")
        + ts_buf
        + "   RTT " + std::to_string(rtt.count()) + " ms") << "\n";
}

// remembering the last TC so [Enter] can repeat it
struct LastSent { bool valid = false; Subsystem sub; int id; bool synth=false; };

// ─── help texts ─────────────────────────────────────────────────────────────
static void show_main_help() {
    std::cout << "\n" << color::header("═══ XDOS Ground Station — Help ════════════════════════") << "\n"
              << "\n" << color::header("Main menu:") << "\n"
              << "  [1-3]   Select subsystem (ADCS / EPS / GPS)\n"
              << "  [4]     Payload Server (requires CCSDS ON)\n"
              << "  [5]     XLINK-S RF transceiver\n"
              << "  [c]     Toggle CCSDS framing ON/OFF\n"
              << "  [!]     Repeat a recent command from history\n"
              << "  [r]     Reconnect all buses\n"
              << "  [q]     Quit\n"
              << "\n" << color::header("At TC listing prompt:") << "\n"
              << "  42        Send TC 42 (you'll be prompted for params)\n"
              << "  EP-3      Send by spec-level ID (EPS subsystem)\n"
              << "  ?42       Show TC 42 details without sending\n"
              << "  /keyword  Search TCs by name (e.g. /attitude)\n"
              << "  <Enter>   Next page (if listing > 20), or go back\n"
              << "  [b]       Back to main menu\n"
              << "  [h]       Show this help\n"
              << "\n" << color::header("After sending:") << "\n"
              << "  The subsystem's command list is shown again automatically\n"
              << "\n" << color::header("Colour coding:") << "\n"
              << "  " << color::set_tag("[SET]") << "  Configure / write parameter\n"
              << "  " << color::get_tag("[GET]") << "  Query / read telemetry\n"
              << "  " << color::act_tag("[ACT]") << "  Action / command\n"
              << "\n" << color::header("Command-line flags:") << "\n"
              << "  --no-obc        Connect directly to subsystem sims\n"
              << "  --no-color      Disable ANSI colours\n"
              << "  --script FILE   Run commands from a file\n"
              << "  --obc-host H    OBC host (default 192.168.1.111)\n"
              << "  --obc-port P    OBC port (default 7000)\n"
              << "\n";
}

static void show_tc_listing_help() {
    std::cout << "\n" << color::header("TC listing help:") << "\n"
              << "  42          Send TC with msg_id 42\n"
              << "  EP-3        Send by spec-level ID (e.g. EP-003)\n"
              << "  ?42         Show details for TC 42 without sending\n"
              << "  ?EP-3       Show details by spec-level ID\n"
              << "  /keyword    Search TCs by name substring\n"
              << "  <Enter>     Next page (or go back if on last page)\n"
              << "  [b]         Back to main menu\n"
              << "\n";
}

// ─── subsystem flow ─────────────────────────────────────────────────────────
static void subsystem_flow(Logger& log, session::Session& sess,
                           const mib::Mib& mib, Subsystem sub,
                           LastSent& last) {
    auto entries = all_tcs(mib, sub);
    std::cout << "\n" << color::header(std::string("══ ") + to_string(sub)
              + " Telecommands (" + std::to_string(entries.size())
              + ") ════════════════════════") << "\n";

    std::optional<TcKind> filter;
    // For ADCS / when total is big, offer filter-by-kind first
    if (entries.size() > 20) {
        std::cout << "Filter by kind: [s]et  [g]et  [a]ct  [Enter]=all  [b]ack > ";
        std::cout.flush();
        std::string ans; std::getline(std::cin, ans); ans = trim(ans);
        if (!ans.empty()) {
            char c = std::tolower(ans[0]);
            if (c == 's') filter = TcKind::SET;
            else if (c == 'g') filter = TcKind::GET;
            else if (c == 'a') filter = TcKind::ACTION;
            else if (c == 'b') return;
        }
    }

    size_t page = 0;
    while (true) {
        std::string kind_lbl = !filter ? "ALL"
            : (*filter == TcKind::SET ? "SET"
            : (*filter == TcKind::GET ? "GET" : "ACT"));
        size_t match = 0;
        for (auto const& e : entries) if (!filter || e.kind == *filter) ++match;
        std::cout << "\n" << color::header("── " + std::string(to_string(sub))
                  + " · " + kind_lbl + " commands (" + std::to_string(match)
                  + ") ──────────────────────") << "\n";
        size_t total = 0, end = 0;
        list_tcs_clean(entries, sub, filter, page, 20, total, end);
        if (end < total) {
            std::cout << color::hint("  ... " + std::to_string(total - end)
                                     + " more — press [Enter] for next page\n");
        }
        // Tailor prompt by subsystem (EPS/UHF/SENSOR use param-ids like
        // EP-XXX / UHF-XXX / SC-XXX — the numbers shown in the listing;
        // others use numeric ID)
        bool eps_style = (sub == Subsystem::EPS || sub == Subsystem::UHF ||
                          sub == Subsystem::SENSOR);
        const char* id_example = sub == Subsystem::EPS    ? "EP-3 or 3"
                               : sub == Subsystem::SENSOR ? "SC-128 or 128"
                                                          : "UHF-4 or 4";
        std::string prompt = eps_style
            ? std::string("Enter TC ID (e.g. ") + id_example
                  + "), [?ID] details, [/search], [Enter]="
            : "Enter TC ID, [?ID] details, [/search], [Enter]=";
        prompt += (end < total) ? "next page" : "back";
        prompt += ", [b]ack > ";
        std::string s = trim(read_line(prompt));
        if (s.empty()) {
            if (end < total) { ++page; continue; }
            return;
        }
        if (s == "b" || s == "B") return;
        if (s == "h" || s == "H") { show_tc_listing_help(); continue; }
        // ── TC search by name ──
        if (s.front() == '/') {
            std::string keyword = trim(s.substr(1));
            if (keyword.empty()) {
                std::cout << color::warn("  Type /keyword to search (e.g. /attitude)\n");
                continue;
            }
            std::vector<const TcEntry*> matches;
            for (auto const& e : entries) {
                if (filter && e.kind != *filter) continue;
                if (icontains(e.name, keyword)) matches.push_back(&e);
            }
            if (matches.empty()) {
                std::cout << color::warn("  No TCs matching '" + keyword + "'\n");
            } else {
                std::cout << "\n" << color::header(std::to_string(matches.size())
                          + " match(es) for '" + keyword + "':") << "\n";
                for (auto const* e : matches) {
                    std::ostringstream l;
                    l << "  " << tag_str(e->kind);
                    if (!e->param_id.empty())
                        l << "  " << std::setw(8) << std::left << e->param_id;
                    else
                        l << "  ID " << std::setw(4) << std::right << e->id;
                    l << "  " << (e->node ? "[" + *e->node + "] " : "      ")
                      << e->name;
                    if (e->synthesized)
                        l << "  " << color::hint(sub == Subsystem::GPS
                            ? "(via LOG)" : "(empty-payload TM request)");
                    l << "\n";
                    std::cout << l.str();
                }
            }
            continue;
        }
        if (s.front() == '?') {
            std::string rest = trim(s.substr(1));
            // EP-style ID resolution for details
            const mib::Message* d = nullptr;
            if (eps_style) d = mib.by_param_id(sub, rest, Direction::TC);
            if (d) { show_tc_details(mib, sub, d->msg_id); continue; }
            auto v = parse_int(rest);
            if (v) show_tc_details(mib, sub, *v);
            else std::cout << color::err("  bad syntax. Try ?EP-19 or ?42\n");
            continue;
        }

        // Resolve input to a TcEntry — three ways:
        //   1) EPS param_id ("EP-019", "EP-19", "EP19") — globally unique
        //   2) Numeric ID — works for ADCS/GPS; for EPS, only if unambiguous
        const TcEntry* hit = nullptr;
        std::string disambig_hint;
        if (eps_style) {
            // Try param_id form first
            auto [prefix, num] = std::pair<std::string,int>{};
            // Quick inline parse: extract digits, look for matching entry
            for (auto const& e : entries) {
                if (e.param_id.empty()) continue;
                // Normalise: strip non-alnum, uppercase both sides
                std::string a, b;
                for (char c : s) if (std::isalnum(static_cast<unsigned char>(c)))
                    a.push_back(static_cast<char>(std::toupper(c)));
                for (char c : e.param_id) if (std::isalnum(static_cast<unsigned char>(c)))
                    b.push_back(c);
                if (a == b) { hit = &e; break; }
                // Also try just the numeric tail of param_id matching user input
                // (so "3" matches "EP-003")
                std::string tail;
                for (char c : e.param_id) if (std::isdigit(static_cast<unsigned char>(c)))
                    tail.push_back(c);
                while (tail.size() > 1 && tail.front() == '0') tail.erase(tail.begin());
                std::string sn;
                for (char c : s) if (std::isdigit(static_cast<unsigned char>(c)))
                    sn.push_back(c);
                while (sn.size() > 1 && sn.front() == '0') sn.erase(sn.begin());
                if (!sn.empty() && sn == tail) {
                    if (hit && hit != &e) {
                        disambig_hint += "  " + e.param_id + "  [" +
                                         (e.node ? *e.node : std::string{"-"}) +
                                         "] " + e.name + "\n";
                    } else {
                        hit = &e;
                    }
                }
            }
            if (!disambig_hint.empty()) {
                std::cout << color::err("  Input '" + s + "' is ambiguous. Be specific:\n")
                          << "  " << hit->param_id << "  [" +
                             (hit->node ? *hit->node : std::string{"-"}) + "] "
                          << hit->name << "\n" << disambig_hint;
                continue;
            }
        }
        if (!hit) {
            auto idv = parse_int(s);
            if (!idv) {
                std::cout << color::err("  Not recognized. ");
                std::cout << (eps_style
                    ? (sub == Subsystem::EPS
                           ? "Try 'EP-19', '19', or a TC name.\n"
                       : sub == Subsystem::SENSOR
                           ? "Try 'SC-128', '128', or a TC name.\n"
                           : "Try 'UHF-4', '4', or a TC name.\n")
                    : "Enter a numeric ID.\n");
                continue;
            }
            int id = *idv;
            for (auto const& e : entries) if (e.id == id) { hit = &e; break; }
            if (!hit) {
                std::cout << color::err("  ID " + std::to_string(id)
                                        + " not in this subsystem's TC list\n");
                continue;
            }
        }

        int id = hit->id;        // keep for the synthesized-LOG path below

        // dispatch
        if (hit->synthesized && sub == Subsystem::GPS) {
            send_and_report(log, sess, sub,
                            *mib.find(Subsystem::GPS, Direction::TC, "LOG"),
                            std::nullopt, json::object(),
                            /*wrap_log*/true, /*log_msg_id*/id, /*empty*/false,
                            "GET " + hit->name + " (via LOG)");
        } else if (hit->synthesized && sub == Subsystem::ADCS) {
            // build a synthetic Message lookup for the TM by name
            auto* tm = mib.find(Subsystem::ADCS, Direction::TM, hit->name);
            if (!tm) { std::cout << color::err("  TM def missing\n"); continue; }
            send_and_report(log, sess, sub, *tm, hit->node, json::object(),
                            false, 0, /*empty_payload_request*/true,
                            "GET " + hit->name + " (empty-payload request)");
        } else {
            auto* tc = mib.find(sub, Direction::TC, hit->name);
            if (!tc) continue;
            json vals = prompt_parameters(*tc, mib);
            send_and_report(log, sess, sub, *tc, hit->node, vals);
        }
        last = { true, sub, id, hit->synthesized };
    }
}

// ─── Payload Server flow (MIB-driven) ───────────────────────────────────────
//
// Per the "Command and Data Format Specification", a payload command is a
// 3-byte header — [3-bit Message Type | 10-bit Command ID | 11-bit Length] —
// followed by the data field.  That frame is carried as CCSDS TC_Data under
// APID 5, with the Command ID mirrored into the CCSDS TC_ID field.
//
// Commands come from mib/payload.json, so this menu stays in step with the
// MIB.  Fixed-layout data fields (545/546) are built by the generic codec;
// the variable-length ones (600 name+args, 601/602 capture parameters) are
// assembled here and sent through send_tc_data.
namespace pf = xdos::framing::payload;

static Byte prompt_payload_id() {
    while (true) {
        std::cout << color::hint("  Payload ID  [0] MSI  [1] SCANWAY  [2] SAR > ");
        std::cout.flush();
        std::string s;
        if (!std::getline(std::cin, s)) return pf::kPayloadMsi;
        auto n = parse_int(trim(s));
        if (n && *n >= 0 && *n <= 2) return static_cast<Byte>(*n);
        std::cout << color::err("  enter 0, 1 or 2\n");
    }
}

static std::string prompt_text(const std::string& label, std::size_t max_len) {
    std::cout << color::hint(label);
    std::cout.flush();
    std::string s;
    if (!std::getline(std::cin, s)) return {};
    if (s.size() > max_len) {
        s = s.substr(0, max_len);
        std::cout << color::warn("  truncated to " + std::to_string(max_len)
                                 + " characters\n");
    }
    return s;
}

// Print the frame breakdown and whatever came back.
static void payload_report(Logger& log, const session::ExchangeResult& r,
                           std::uint16_t command_id) {
    auto& l = log;

    auto sent = pf::decode(r.tx_native);
    if (sent.header_ok) {
        char hdr[160];
        std::snprintf(hdr, sizeof(hdr),
                      "  payload frame  type=%d (%s)  cmd=%u (%s)  len=%u",
                      static_cast<int>(sent.type), pf::type_name(sent.type),
                      sent.command_id, pf::command_name(sent.command_id),
                      sent.length);
        l << color::hint(hdr) << "\n";
    }
    if (r.ccsds_used && r.ccsds_tx)
        l << "  " << color::hint(r.ccsds_tx->summary()) << "\n";
    l << "  " << color::hint("inner ") << fmt_bytes(r.tx_native) << "\n";
    l << "  TX " << fmt_bytes(r.tx_frame) << "\n";
    log_tx_link(l, r);

    if (!r.rx_frame.empty()) {
        l << "  RX " << fmt_bytes(r.rx_frame) << "\n";
        log_rx_link(l, r);
    }
    if (!r.error.empty()) { l << "  " << color::err("[BUS] ERROR: " + r.error) << "\n"; return; }
    if (r.rx_frame.empty()) {
        l << "  " << color::warn("(no response)") << "\n";
    } else {
        auto got = pf::decode(r.rx_native);
        if (!got.header_ok) {
            l << "  " << color::err("reply shorter than the 3-byte header") << "\n";
        } else {
            char hdr[160];
            std::snprintf(hdr, sizeof(hdr), "  reply          cmd=%u  len=%u",
                          got.command_id, got.length);
            l << color::hint(hdr) << "\n";
            if (!got.length_ok)
                l << "  " << color::err("declared length " + std::to_string(got.length)
                    + " but " + std::to_string(got.data.size()) + " bytes present") << "\n";
            if (got.command_id != command_id)
                l << "  " << color::warn("reply Command ID " + std::to_string(got.command_id)
                    + " does not match the command sent (" + std::to_string(command_id) + ")") << "\n";
            // The payload server returns no telemetry — only a single-character
            // acknowledgement that the command was performed.
            if (pf::is_ack(got)) {
                char abuf[80];
                std::snprintf(abuf, sizeof(abuf), "  ACK '%c'  (0x%02X)",
                              static_cast<char>(got.data[0]), got.data[0]);
                l << (got.data[0] == pf::kAckChar ? color::ok(abuf) : color::err(abuf))
                  << (got.data[0] == pf::kAckChar ? color::hint("  command performed")
                                                  : color::hint("  command NOT performed"))
                  << "\n";
            } else {
                l << "  " << color::warn("expected a 1-character ACK, got "
                    + std::to_string(got.data.size()) + " bytes: ") << fmt_bytes(got.data) << "\n";
            }
        }
    }
    char rttbuf[64];
    std::snprintf(rttbuf, sizeof(rttbuf), "  RTT %lld ms", (long long)r.rtt.count());
    l << color::hint(rttbuf) << "\n";
}

static void payload_flow(Logger& log, session::Session& sess, const mib::Mib& mib) {
    auto& l = log;
    auto tcs = mib.messages(Subsystem::Payload, Direction::TC);
    if (tcs.empty()) {
        l << color::err("  no Payload commands in the MIB (mib/payload.json missing?)\n");
        return;
    }

    while (true) {
        l << "\n" << color::header("══ Payload Server ══════════════════════════════════") << "\n";
        l << color::hint("  frame: [3b Type | 10b Command ID | 11b Length] + Data"
                         "   (CCSDS APID 5)\n\n");
        for (std::size_t i = 0; i < tcs.size(); ++i) {
            char line[128];
            std::snprintf(line, sizeof(line), "  [%zu]  %-4d  %s",
                          i + 1, tcs[i]->wire_id, tcs[i]->name.c_str());
            l << line << "\n";
        }
        l << "  [b]  back\n";

        std::string s = trim(read_line("  Choose > "));
        if (s.empty() || s[0] == 'b' || s[0] == 'B' || s[0] == 'q' || s[0] == 'Q') return;
        auto n = parse_int(s);
        if (!n || *n < 1 || *n > static_cast<int>(tcs.size())) {
            l << color::err("  no such entry\n");
            continue;
        }
        const mib::Message& tc = *tcs[*n - 1];
        const auto cmd_id = static_cast<std::uint16_t>(tc.wire_id);

        l << "\n  " << color::header("▸ " + tc.name) << "  "
          << color::hint("Command ID " + std::to_string(cmd_id)) << "\n";
        if (!tc.description.empty()) l << color::hint("  " + tc.description) << "\n";

        session::ExchangeResult r;
        switch (cmd_id) {
            case pf::kCmdContainerOn:
            case pf::kCmdContainerOff: {
                Byte pid = prompt_payload_id();
                l << color::hint(std::string("  target: ") + pf::payload_name(pid)) << "\n";
                r = sess.send_tc(Subsystem::Payload, tc.name,
                                 nlohmann::json{{"Payload ID", pid}});
                break;
            }
            case pf::kCmdPayloadSeq: {
                Byte pid = prompt_payload_id();
                auto name = prompt_text("  Operation name (max 16 chars)  > ",
                                        pf::kMaxSeqNameLen);
                auto args = prompt_text("  Arguments      (max 128 chars) > ",
                                        pf::kMaxSeqArgsLen);
                l << color::hint(std::string("  target: ") + pf::payload_name(pid)
                                 + "   name " + std::to_string(name.size()) + " B"
                                 + "   args " + std::to_string(args.size()) + " B") << "\n";
                r = sess.send_tc_data(Subsystem::Payload, tc.name,
                                      pf::make_sequence_data(pid, name, args));
                break;
            }
            case pf::kCmdCaptureMsi:
            case pf::kCmdCaptureScanway: {
                Byte pid = (cmd_id == pf::kCmdCaptureMsi) ? pf::kPayloadMsi
                                                          : pf::kPayloadScanway;
                l << color::hint(std::string("  target: ") + pf::payload_name(pid)
                                 + "  (implied by Command ID)") << "\n";
                auto params = prompt_text(
                    "  Capture parameters (optional, max 128 chars) > ",
                    pf::kMaxSeqArgsLen);
                Bytes data(params.begin(), params.end());
                r = sess.send_tc_data(Subsystem::Payload, tc.name, data);
                break;
            }
            default:
                l << color::err("  no entry form for this command yet\n");
                continue;
        }
        payload_report(l, r, cmd_id);
    }
}


// ─── XLink flow ──────────────────────────────────────────────────────────────
// XLink uses its own wire format (16-byte product key + 4-byte service_event
// + U32-packed data block).  send_xlink_tc bypasses the generic codec pipeline
// so this flow is short and focused.
static void render_xlink_decoded(Logger& log, const mib::Message& tm,
                                 const nlohmann::json& vals,
                                 const std::string& tc_name);

// Product-key catalog is loaded once at GS startup from mib/xlink_keys/*.txt.
// If loaded, the CLI can offer an interactive picker per session.
static std::optional<xdos::xlink::KeyCatalog>& xlink_catalog() {
    static std::optional<xdos::xlink::KeyCatalog> cat;
    return cat;
}

// Interactive picker: list the (XmtFreq, RcvFreq) configurations the catalog
// knows about, let the operator pick one, apply that config's key to Session.
static void xlink_pick_key(session::Session& sess) {
    auto& cat = xlink_catalog();
    if (!cat) {
        std::cout << color::warn("  No product-key catalog loaded.  Drop the vendor's\n"
                                 "  '20260223_Product_Key_XLKxxxx.txt' file into mib/xlink_keys/\n"
                                 "  and restart the GS.\n");
        return;
    }
    std::cout << "\n" << color::header("Product-key catalog for device")
              << "  " << color::ok("SN " + cat->serial_number)
              << "  " << color::hint("MAC " + cat->mac + "  " + cat->unit_model) << "\n";
    std::cout << color::hint(
        "  Pick the (XmtSymbFreq, RcvSymbFreq) configuration you plan to run.\n"
        "  The matching 128-bit product key will be applied to every subsequent\n"
        "  XLINK-S TC.  Real hardware will NAK 'ACCESS_VIOLATION' if the key\n"
        "  doesn't match the actual XmtSymbFreq/RcvSymbFreq settings on the transceiver.\n");
    int idx = 1;
    for (auto const& e : cat->entries) {
        std::cout << "  [" << std::setw(2) << idx++ << "]  "
                  << "XmtSymb " << std::setw(4) << std::right << e.xmt_symb_freq_khz << " kHz"
                  << "   RcvSymb " << std::setw(4) << e.rcv_symb_freq_khz << " kHz"
                  << "   key " << color::hint(e.raw_key_text.substr(0, 8) + "...") << "\n";
    }
    std::cout << "  [b]ack\n";
    std::string s = trim(read_line("Pick config> "));
    if (s.empty() || s == "b" || s == "B") return;
    auto n = parse_int(s);
    if (!n || *n < 1 || *n > (int)cat->entries.size()) {
        std::cout << color::err("  Not a valid selection.\n"); return;
    }
    auto const& e = cat->entries[*n - 1];
    sess.set_xlink_key(e.key);
    std::cout << color::ok("  Applied key for XmtSymb ")
              << e.xmt_symb_freq_khz << " kHz / RcvSymb " << e.rcv_symb_freq_khz << " kHz.\n";
    // Also surface the parsed capability flags for that config so the operator
    // knows what the transceiver expects (RsMode, XmtCh0_En, Use SPI, etc.)
    if (!e.features.empty()) {
        std::cout << color::hint("  Config flags: ");
        for (std::size_t i = 0; i < e.features.size(); ++i) {
            std::cout << e.features[i];
            if (i + 1 < e.features.size()) std::cout << ", ";
        }
        std::cout << "\n";
    }
}

static void xlink_flow(Logger& log, session::Session& sess,
                       const mib::Mib& mib) {
    auto* def = mib.subsystem(Subsystem::XLINK);
    if (!def) {
        std::cout << color::err("  XLINK MIB not loaded.\n"); return;
    }

    while (true) {
        std::cout << "\n" << color::header("XLINK-S service list") << "\n";
        // Show currently-active key config
        auto k = sess.xlink_key();
        bool zero = std::all_of(k.begin(), k.end(), [](auto x){ return x == 0; });
        if (zero) {
            std::cout << "  " << color::warn("Active product key: [none — sim accepts any key; real HW will NAK]") << "\n";
        } else {
            std::ostringstream kh;
            for (std::size_t i = 0; i < k.size(); ++i) {
                kh << std::hex << std::setw(2) << std::setfill('0') << (int)k[i];
                if (i == 3 || i == 7 || i == 11) kh << "-";
            }
            std::cout << "  " << color::hint("Active product key: " + kh.str()) << "\n";
        }
        int idx = 1;
        std::vector<const mib::Message*> tcs;
        for (auto const& m : def->messages) {
            if (m.direction != Direction::TC) continue;
            tcs.push_back(&m);
        }
        for (auto const* m : tcs) {
            std::cout << "  [" << std::setw(2) << idx++ << "]  "
                      << std::setw(9) << std::left << m->param_id
                      << "  event 0x" << std::right << std::hex << std::setw(8) << std::setfill('0')
                      << m->msg_id << std::dec << std::setfill(' ')
                      << "  " << m->name;
            if (!m->description.empty())
                std::cout << color::hint("  — " + m->description.substr(0, 60));
            std::cout << "\n";
        }
        std::cout << "  [k] Pick product key (from vendor key file)\n";
        std::cout << "  [b]ack\n";

        std::string s = trim(read_line("XLINK-S> "));
        if (s.empty()) continue;
        if (s == "k" || s == "K") {
            xlink_pick_key(sess);
            continue;
        }
        if (s == "b" || s == "B" || s == "q" || s == "Q") return;

        // Accept "XL-4", "4", or the TC name
        const mib::Message* tc = nullptr;
        auto tryn = parse_int(s);
        if (tryn && *tryn >= 1 && *tryn <= static_cast<int>(tcs.size())) {
            tc = tcs[*tryn - 1];
        } else {
            tc = mib.by_param_id(Subsystem::XLINK, s, Direction::TC);
            if (!tc) {
                // maybe user typed the name
                for (auto const* c : tcs) if (c->name == s) { tc = c; break; }
            }
        }
        if (!tc) {
            std::cout << color::err("  Not recognised. Enter menu number, XL-N, or service name.\n");
            continue;
        }

        // Ask GET vs SET.  Header-only services (IDLE, RESET) are SET only.
        // Services with only-GET semantics (VERSION, HOUSE_KEEPING) are GET only.
        // Rest offer both.
        bool is_get = false;
        std::string nm = tc->name;
        // GET-only: pure telemetry reads (no payload the operator would set)
        bool get_only = (nm == "VERSION" || nm == "HOUSE_KEEPING" || nm == "XPA_INF");
        // SET-only: header-only or set-only commands
        bool set_only = (nm == "IDLE" || nm == "RESET" ||
                         nm == "UART_BD_SAVE" || nm == "IP_ADR_SAVE" ||
                         nm == "RF_XMT_DATA" || nm == "XMT_IDLE_FRM" ||
                         nm.rfind("GPIO_", 0) == 0);
        if (get_only) is_get = true;
        else if (set_only) is_get = false;
        else {
            std::cout << "  [g]et or [s]et? > ";
            std::string gs = trim(read_line(""));
            is_get = gs.empty() || gs[0] == 'g' || gs[0] == 'G';
        }

        // For SET commands with data, prompt for each field
        nlohmann::json vals = nlohmann::json::object();
        if (!is_get) {
            for (auto const& p : tc->parameters) {
                if (p.name == "product_key" || p.name == "service_event") continue;
                if (p.name.find("Reserved") != std::string::npos) continue;
                // Same prompt as every other subsystem: enum values listed,
                // names accepted, bad input re-asked rather than silently zeroed.
                vals[p.name] = prompt_one_value(p);
            }
        }

        // Header banner
        log << "\n  " << color::header(
            std::string("▸ XLINK-S ") + tc->name + (is_get ? " GET" : " SET")) << "\n";

        auto r = sess.send_xlink_tc(tc->name, vals, is_get);
        log << "  TX (" << r.tx_frame.size() << " bytes)  "
            << fmt_bytes(r.tx_frame) << "\n";
        log_tx_link(log, r);

        if (!r.error.empty() && r.rx_frame.empty()) {
            log << "  " << color::err("[XLINK-S] " + r.error) << "\n";
            continue;
        }
        log << "  RX (" << r.rx_frame.size() << " bytes)  "
            << fmt_bytes(r.rx_frame) << "\n";
        log_rx_link(log, r);

        // OBC path: the reply arrived as a 1432-byte TM downlink frame —
        // show the same header block the other subsystem flows print.
        log_tm_frame_header(log, r);

        // ACK/NAK banner
        if (r.error == "XLink NAK") {
            log << "  " << color::err("✗ NAK — device rejected request (wrong key or bad params)") << "\n";
        } else if (r.ok) {
            log << "  " << color::ok("✓ ACK") << "\n";
        }

        // Decoded values — grouped by section for a DSP/comms operator
        auto* tm = mib.find(Subsystem::XLINK, Direction::TM, tc->name + "_RSP");
        if (tm && !r.decoded_values.empty()) {
            render_xlink_decoded(log, *tm, r.decoded_values, tc->name);
        }
    }
}

// Convert a MIB parameter + decoded value into one display line.  Uses the
// standard format_value() and adds the [type unit] tag column.
static void xlink_row(Logger& log, const mib::Parameter& p, const json& v) {
    std::string tag = "[" + p.data_type;
    if (p.unit) tag += " " + *p.unit;
    tag += "]";
    log << "      " << std::setw(28) << std::left << p.name
        << "  " << std::setw(12) << tag << " = "
        << format_value(v, p) << "\n";
}

// Render a section header (blank line + colored title).
static void xlink_section(Logger& log, const std::string& title) {
    log << "\n    " << color::header(title) << "\n";
}

// Decoded-values renderer, XLINK-S–flavoured.  Groups the flat MIB parameter
// list into meaningful sections a comms engineer scans naturally:
//   VERSION       → firmware date-time header + one line per file
//   HOUSE_KEEPING → voltages | temperatures | RCV Ch0 | RCV Ch1 | XMT Ch0/Ch1 | uptime
//   DEVICE_CONF   → RFE freqs | Xmt Ch0 | Xmt Ch1 | Rcv Ch0 | Rcv Ch1 | symbol rates
static void render_xlink_decoded(Logger& log, const mib::Message& tm,
                                 const json& vals, const std::string& tc_name) {
    log << "\n  " << color::header("decoded:") << "\n";

    auto V = [&](const std::string& k) -> json {
        auto it = vals.find(k);
        return (it != vals.end()) ? *it : json{};
    };
    auto get_i = [&](const std::string& k) -> long long {
        auto v = V(k);
        if (v.is_number_integer()) return v.get<long long>();
        if (v.is_number())         return static_cast<long long>(v.get<double>());
        return 0;
    };
    auto find_param = [&](const std::string& name) -> const mib::Parameter* {
        for (auto const& p : tm.parameters) if (p.name == name) return &p;
        return nullptr;
    };

    // Set of parameter names we've already rendered in a group, so the
    // fallback loop at the bottom doesn't print them twice.
    std::set<std::string> handled;

    // ─── VERSION: date-time + one line per firmware file ────────────────────
    if (tc_name == "VERSION") {
        // Firmware build date-time (from F1 date fields)
        long long y = 2000 + get_i("F1_Year");
        long long mo = get_i("F1_Months"), d = get_i("F1_Day");
        long long h = get_i("F1_Hours"), mi = get_i("F1_Minutes"), s = get_i("F1_Seconds");
        char buf[64];
        std::snprintf(buf, sizeof(buf),
                      "Firmware date-code: %04lld-%02lld-%02lld  %02lld:%02lld:%02lld UTC",
                      y, mo, d, h, mi, s);
        xlink_section(log, buf);
        for (auto k : {"F1_Year","F1_Months","F1_Day","F1_Hours","F1_Minutes","F1_Seconds"})
            handled.insert(k);

        auto ascii = [&](long long raw) {
            std::string s;
            for (int i = 0; i < 4; ++i) {
                char c = static_cast<char>((raw >> (i*8)) & 0xFF);
                s.push_back(std::isprint((unsigned char)c) ? c : '.');
            }
            return s;
        };
        xlink_section(log, "Files in program memory:");
        for (int i = 1; i <= 3; ++i) {
            std::string nm  = "F" + std::to_string(i) + "_FileName";
            std::string ln  = "F" + std::to_string(i) + "_len";
            if (!vals.contains(nm) || !vals.contains(ln)) continue;
            auto fname = ascii(get_i(nm));
            long long size = get_i(ln);
            log << "      " << color::ok(fname) << "   " << std::setw(10) << std::right
                << size << " bytes";
            std::string fl = "F" + std::to_string(i) + "_Flags";
            if (vals.contains(fl)) {
                long long flags = get_i(fl);
                if (flags & 0x1) log << "  " << color::hint("[SOLL_PROG]");
                if (flags & 0x2) log << "  " << color::ok  ("[RUNNING]");
            }
            log << "\n";
            handled.insert(nm); handled.insert(ln); handled.insert(fl);
            if (i == 1) {
                handled.insert("F1_start_adr");   // address is a debug detail
            }
        }
        return;
    }

    // ─── HOUSE_KEEPING: voltages | temperatures | RCV | XMT ─────────────────
    if (tc_name == "HOUSE_KEEPING") {
        xlink_section(log, "CPU voltage rails");
        for (auto k : {"V_VCCINT","V_VCCAUX","V_VCCBRAM","V_VCCPINT","V_VCCPAUX","V_VCCPDRO"}) {
            if (auto* p = find_param(k)) { xlink_row(log, *p, V(k)); handled.insert(k); }
        }
        xlink_section(log, "Temperatures");
        for (auto k : {"T_AD9361","T_EthPhy","T_Zynq"}) {
            if (auto* p = find_param(k)) { xlink_row(log, *p, V(k)); handled.insert(k); }
        }
        handled.insert("T_Reserved");

        for (int ch = 0; ch < 2; ++ch) {
            std::string prefix = "Rcv" + std::to_string(ch) + "_";
            std::string title  = "RCV channel " + std::to_string(ch);
            // Lock summary
            bool on   = get_i(prefix + "On") != 0;
            bool sync = get_i(prefix + "Sync") != 0;
            long long blocks = get_i(prefix + "BchBlockCount");
            long long errs   = get_i(prefix + "BchErrCount");
            std::ostringstream head; head << title;
            if (!on)         head << " " << color::hint("(disabled)");
            else if (sync)   head << " " << color::ok  ("(SYNC — locked)");
            else             head << " " << color::warn("(HW on, no sync)");
            xlink_section(log, head.str());
            if (on) {
                // Ordered subset: state, then quality metrics
                for (auto k : {"On","Sync","Modulat","Fec","idxGain","SirMin","SirMax",
                               "PwrMin","PwrMax","DopplerFreq","RxState",
                               "BchBlockCount","BchErrCount"}) {
                    std::string key = prefix + k;
                    if (auto* p = find_param(key)) {
                        xlink_row(log, *p, V(key));
                        handled.insert(key);
                    }
                }
                // Derived: bit-error rate from BCH counters
                if (blocks > 0) {
                    double ber = double(errs) / double(blocks);
                    char buf[64];
                    std::snprintf(buf, sizeof(buf),
                                  "      %-28s  %-12s = %.2e",
                                  "→ Bit-error rate", "[derived]", ber);
                    log << color::hint(buf) << "\n";
                }
            }
            // Also mark the reserved fields as handled
            for (auto k : {"Rest0","Rest1","DoppExt"}) handled.insert(prefix + k);
        }

        for (int ch = 0; ch < 2; ++ch) {
            std::string prefix = "Xmt" + std::to_string(ch) + "_";
            std::string title  = "XMT channel " + std::to_string(ch);
            bool on = get_i(prefix + "On") != 0;
            std::ostringstream head; head << title;
            head << (on ? color::ok(" (ON)") : color::hint(" (off)"));
            xlink_section(log, head.str());
            if (on) {
                for (auto k : {"On","Modulation","Fec","Att"}) {
                    std::string key = prefix + k;
                    if (auto* p = find_param(key)) {
                        xlink_row(log, *p, V(key));
                        handled.insert(key);
                    }
                }
            }
            handled.insert(prefix + "Rest");
        }

        // Uptime
        if (vals.contains("card_run_counter")) {
            long long ticks = get_i("card_run_counter");
            double secs = ticks * 0.01;  // 10-ms ticks
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                          "      %-28s  %-12s = %.1f s  (%lld ticks)",
                          "Card uptime", "[UINT s]", secs, ticks);
            xlink_section(log, "Uptime");
            log << buf << "\n";
            handled.insert("card_run_counter");
        }
        handled.insert("MoreHkInfo");
        return;
    }

    // ─── DEVICE_CONF: RFE | XMT Ch0/1 | RCV Ch0/1 | symbol rates ────────────
    if (tc_name == "DEVICE_CONF") {
        xlink_section(log, "RF frequencies (kHz)");
        for (auto k : {"RxFreq_Ch0","RxFreq_Ch1","TxFreq_Ch0","TxFreq_Ch1"}) {
            if (auto* p = find_param(k)) { xlink_row(log, *p, V(k)); handled.insert(k); }
        }
        xlink_section(log, "Symbol rates (locked by product key)");
        for (auto k : {"XmtSymbFreq","RcvSymbFreq"}) {
            if (auto* p = find_param(k)) { xlink_row(log, *p, V(k)); handled.insert(k); }
        }
        for (int ch = 0; ch < 2; ++ch) {
            std::string suffix = "_Ch" + std::to_string(ch);
            xlink_section(log, "XMT channel " + std::to_string(ch));
            for (auto k : {"Modulat","Mode","Fec","ChEn","PRnd","ChSwitchOn","AttTxGain","TxLevel"}) {
                std::string key = std::string(k) + suffix;
                if (auto* p = find_param(key)) { xlink_row(log, *p, V(key)); handled.insert(key); }
            }
        }
        for (int ch = 0; ch < 2; ++ch) {
            std::string suffix = "_Ch" + std::to_string(ch);
            xlink_section(log, "RCV channel " + std::to_string(ch));
            for (auto k : {"Rx_ChEn","Rx_Modulat","Rx_Fec","Rx_GainMax","Rx_BchDec",
                           "Rx_PRnd","Rx_AccBchErr","Rx_Calib"}) {
                std::string key = std::string(k) + suffix;
                if (auto* p = find_param(key)) { xlink_row(log, *p, V(key)); handled.insert(key); }
            }
        }
        if (auto* p = find_param("TrFrmSz")) {
            xlink_section(log, "Transfer frame");
            xlink_row(log, *p, V("TrFrmSz")); handled.insert("TrFrmSz");
        }
        // Suppress the calibration-only debug fields from the friendly view
        for (auto k : {"RfeHwPins","AdReserved","XmtCtr4","RcvMode","RcvReserved",
                       "Rcv_Reserved","Ch1_diff_Ch2","OffsetMod_Ch0","OffsetMod_Ch1",
                       "OffsetModNormal","ASMStart_Ch0","ASMStart_Ch1","RcvChannel",
                       "Use_2Rx2Tx","RefClk","Rcv_ChSwitchOn_Ch0","Rcv_ChSwitchOn_Ch1",
                       "Rcv_PRnd_Global","Rx_MsbFirst_Ch0","Rx_MsbFirst_Ch1",
                       "Rx_BchCountMode_Ch0","Rx_BchCountMode_Ch1",
                       "Rx_PwrOff_Ch0","Rx_PwrOff_Ch1",
                       "RxLow_Ch0","RxLow_Ch1","RxHigh_Ch0","RxHigh_Ch1",
                       "TxLow_Ch0","TxLow_Ch1","TxHigh_Ch0","TxHigh_Ch1",
                       "RxMix_Ch0","RxMix_Ch1","TxMix_Ch0","TxMix_Ch1"})
            handled.insert(k);
    }

    // ─── Anything else: fall back to a flat listing ─────────────────────────
    for (auto const& p : tm.parameters) {
        if (p.name == "product_key" || p.name == "service_event") continue;
        if (handled.count(p.name)) continue;
        if (!vals.contains(p.name)) continue;
        xlink_row(log, p, vals.at(p.name));
    }
}

// ─── MIB statistics ─────────────────────────────────────────────────────────
static void print_mib_stats(const mib::Mib& mib) {
    auto counts = mib.counts();
    auto subs = mib.loaded_subsystems();
    std::cout << "  " << color::header("MIB loaded: ")
              << subs.size() << " subsystems, "
              << counts.tc << " TCs, " << counts.tm << " TMs, "
              << counts.event << " events, " << counts.params << " parameters\n";
    std::cout << "    ";
    for (size_t i = 0; i < subs.size(); ++i) {
        auto sub_msgs = mib.messages(subs[i]);
        int tc_count = 0, tm_count = 0;
        for (auto const& m : sub_msgs) {
            if (m.direction == Direction::TC) ++tc_count;
            else if (m.direction == Direction::TM) ++tm_count;
        }
        if (i > 0) std::cout << "   ";
        std::cout << color::addr(to_string(subs[i])) << " " << tc_count << "tc/" << tm_count << "tm";
    }
    std::cout << "\n";
}

// ─── batch script mode ──────────────────────────────────────────────────────
static void run_script(Logger& log, session::Session& sess, const mib::Mib& mib,
                       const std::string& script_path) {
    std::ifstream f(script_path);
    if (!f.is_open()) {
        std::cerr << color::err("Cannot open script file: " + script_path) << "\n";
        return;
    }
    std::cout << "\n" << color::header("Running script: " + script_path) << "\n";
    std::string line;
    int line_num = 0;
    while (std::getline(f, line)) {
        ++line_num;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;  // skip blanks and comments

        // Parse: SUBSYSTEM TC_ID_OR_NAME [param=value ...]
        // e.g.  "EPS EP-3"  or  "ADCS 42"  or  "WAIT 2"
        std::istringstream iss(line);
        std::string sub_str;
        iss >> sub_str;

        // WAIT command: pause for N seconds
        if (sub_str == "WAIT" || sub_str == "wait") {
            int secs = 1;
            iss >> secs;
            std::cout << "  " << color::hint("[script L" + std::to_string(line_num)
                      + "] WAIT " + std::to_string(secs) + "s...") << "\n";
            std::this_thread::sleep_for(std::chrono::seconds(secs));
            continue;
        }

        auto sub_opt = subsystem_from_string(sub_str);
        if (!sub_opt) {
            std::cout << color::err("  [script L" + std::to_string(line_num)
                      + "] Unknown subsystem: " + sub_str) << "\n";
            continue;
        }
        Subsystem sub = *sub_opt;

        std::string tc_id_str;
        iss >> tc_id_str;
        if (tc_id_str.empty()) {
            std::cout << color::err("  [script L" + std::to_string(line_num)
                      + "] Missing TC ID") << "\n";
            continue;
        }

        // Resolve the TC
        const mib::Message* tc = nullptr;
        // Try param_id first (e.g. "EP-3")
        tc = mib.by_param_id(sub, tc_id_str, Direction::TC);
        if (!tc) {
            // Try numeric ID
            auto idv = parse_int(tc_id_str);
            if (idv) tc = mib.by_wire(sub, Direction::TC, *idv);
        }
        if (!tc) {
            // Try by name
            tc = mib.find(sub, Direction::TC, tc_id_str);
        }
        if (!tc) {
            std::cout << color::err("  [script L" + std::to_string(line_num)
                      + "] TC not found: " + tc_id_str) << "\n";
            continue;
        }

        std::cout << "  " << color::header("[script L" + std::to_string(line_num)
                  + "] " + to_string(sub) + " → " + tc->name) << "\n";

        // Use default-zero params for script mode (no interactive prompting)
        json vals = json::object();
        for (auto const& p : tc->parameters) {
            bool is_float = (p.data_type == "FLOAT" || p.data_type == "DOUBLE");
            vals[p.name] = is_float ? json(0.0) : json(0);
        }

        send_and_report(log, sess, sub, *tc, tc->node, vals);
    }
    std::cout << "\n" << color::ok("Script complete.") << "\n";
}

// ─── history replay ─────────────────────────────────────────────────────────
static void show_and_replay_history(Logger& log, session::Session& sess,
                                    const mib::Mib& mib,
                                    CommandHistory& history) {
    if (history.empty()) {
        std::cout << color::warn("  No commands in history yet.\n");
        return;
    }
    auto now = std::chrono::steady_clock::now();
    std::cout << "\n" << color::header("Recent commands:") << "\n";
    int idx = 1;
    for (auto const& e : history.entries()) {
        auto ago = std::chrono::duration_cast<std::chrono::seconds>(now - e.when).count();
        std::string age_str;
        if (ago < 60) age_str = std::to_string(ago) + "s ago";
        else          age_str = std::to_string(ago / 60) + "m ago";
        std::cout << "  [" << std::setw(2) << idx++ << "]  "
                  << std::setw(7) << std::left << to_string(e.sub)
                  << "  " << std::setw(30) << std::left << e.tc_name
                  << "  " << color::hint(age_str) << "\n";
    }
    std::cout << "  Pick [1-" << history.entries().size() << "] to re-send, [Enter] = cancel > ";
    std::cout.flush();
    std::string s;
    if (!std::getline(std::cin, s)) return;
    s = trim(s);
    if (s.empty()) return;
    auto n = parse_int(s);
    if (!n || *n < 1 || *n > static_cast<int>(history.entries().size())) {
        std::cout << color::err("  Not a valid selection.\n");
        return;
    }
    auto const& e = history.entries()[*n - 1];
    auto* tc = mib.find(e.sub, Direction::TC, e.tc_name);
    if (!tc) {
        // Maybe it was a synthesized TM request
        auto* tm_msg = mib.find(e.sub, Direction::TM, e.tc_name);
        if (tm_msg && e.synthesized) {
            if (e.sub == Subsystem::GPS) {
                send_and_report(log, sess, e.sub,
                    *mib.find(Subsystem::GPS, Direction::TC, "LOG"),
                    std::nullopt, json::object(),
                    true, e.tc_id, false,
                    "GET " + e.tc_name + " (via LOG)");
            } else if (e.sub == Subsystem::ADCS) {
                send_and_report(log, sess, e.sub, *tm_msg, e.node,
                    json::object(), false, 0, true,
                    "GET " + e.tc_name + " (empty-payload)");
            }
        } else {
            std::cout << color::err("  TC '" + e.tc_name + "' not found in MIB.\n");
        }
        return;
    }
    // Default-zero params for replay (interactive prompt for SET would be nicer
    // but adds complexity; this is the simple-and-safe path).
    json vals = json::object();
    for (auto const& p : tc->parameters) {
        bool is_float = (p.data_type == "FLOAT" || p.data_type == "DOUBLE");
        vals[p.name] = is_float ? json(0.0) : json(0);
    }
    send_and_report(log, sess, e.sub, *tc, e.node, vals);
}

// ─── main loop ──────────────────────────────────────────────────────────────
// Project resources (mib/, config/) live at a fixed place in the tree, but the
// CWD the CLI inherits is whatever the operator happened to be in — the repo
// root, build/, scripts/ (what run_*.sh leave you in), or somewhere unrelated.
// Anchor on the project root instead of guessing with "../": walk up from both
// the CWD and the executable's own location and take the first tree that looks
// like ours.
static fs::path find_project_root() {
    std::error_code ec;
    std::vector<fs::path> starts;
    if (auto cwd = fs::current_path(ec); !ec) starts.push_back(cwd);
#if defined(_WIN32)
    char buf[MAX_PATH];
    if (DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH); n > 0 && n < MAX_PATH)
        starts.push_back(fs::path(buf).parent_path());
#elif defined(__linux__)
    if (auto exe = fs::read_symlink("/proc/self/exe", ec); !ec)
        starts.push_back(exe.parent_path());
#endif
    for (const auto& start : starts)
        for (fs::path d = start; ; d = d.parent_path()) {
            if (fs::is_directory(d / "mib", ec) || fs::is_directory(d / "config", ec))
                return d;
            if (d == d.parent_path()) break;   // hit the filesystem root
        }
    return fs::current_path(ec);               // give up: behave as before
}

static fs::path find_mib_dir() {
    std::error_code ec;
    auto d = find_project_root() / "mib";
    return fs::is_directory(d, ec) ? d : fs::path("mib");
}


// ─── [p] power control ──────────────────────────────────────────────────────
// Friendly replacement for hand-building Set_PDU_CH_State masks: numbered
// subsystem list with live state, toggled via REAL EPS TCs (read-modify-write
// of the channel mask through the OBC), plus the EGSE kill switch.
static void power_flow(Logger& log, session::Session& sess, const mib::Mib& mib) {
    if (!g_pm) {
        std::cout << color::warn("  power monitor unavailable\n");
        return;
    }
    const mib::Message* set_tc =
        mib.find(Subsystem::EPS, Direction::TC, "Set_PDU_CH_State");

    while (true) {
        g_pm->refresh_now();
        auto ps = g_pm->snap();
        std::cout << "\n" << render_power_bar(*g_pm) << "\n\n"
                  << color::header("Power control") << "\n";

        const auto& items = g_pm->items();
        for (size_t i = 0; i < items.size(); ++i) {
            int p = g_pm->powered(items[i].sub);
            std::string d = p == 1 ? color::ok("●") : p == 0 ? color::err("●")
                                                             : color::hint("·");
            std::cout << "  [" << (i + 1) << "] " << d << " "
                      << items[i].name
                      << color::hint("  (" + items[i].chans + ")")
                      << (p == 1 ? color::hint("  — ON, select to turn OFF")
                                 : p == 0 ? color::hint("  — OFF, select to turn ON")
                                          : std::string()) << "\n";
        }
        std::cout << "  [k] Kill switch: "
                  << (ps.eps_ok ? (ps.eps.kill ? color::ok("ON") : color::err("OFF"))
                                : color::hint("?"))
                  << color::hint("  (EGSE — powers/kills the whole bus incl. OBC)") << "\n"
                  << "  [b] back\n";

        auto in = read_line("power > ");
        if (in.empty()) continue;
        char c = in[0];
        if (c == 'b' || c == 'q') return;

        if (c == 'k' || c == 'K') {
            if (!ps.eps_ok) { std::cout << color::err("  EPS EGSE unreachable\n"); continue; }
            bool target = !ps.eps.kill;
            if (!target) {
                auto a = read_line(std::string("  ") +
                    "kill switch OFF de-powers EVERYTHING including the OBC. Sure? [y/N] > ");
                if (a.empty() || (a[0] != 'y' && a[0] != 'Y')) continue;
            }
            if (g_pm->send_kill(target)) {
                std::cout << (target
                    ? color::ok("  kill switch ON — OBC will boot, then bring up "
                                "ADCS -> GPS -> SBAND (5 s apart)\n")
                    : color::err("  kill switch OFF — bus dead\n"));
            } else {
                std::cout << color::err("  EGSE command failed (EPS sim running?)\n");
            }
            continue;
        }

        if (c >= '1' && c <= '9') {
            size_t ix = static_cast<size_t>(c - '1');
            if (ix >= items.size()) { std::cout << color::warn("  no such entry\n"); continue; }
            if (!ps.eps_ok || !ps.eps.kill) {
                std::cout << color::err("  bus is dead — kill switch first ([k])\n");
                continue;
            }
            if (ps.obc_ok && ps.obc.state != egse::ObcState::On) {
                std::cout << color::err(std::string("  OBC is ")
                          + egse::obc_state_name(ps.obc.state)
                          + " — TCs can\'t bridge yet\n");
                continue;
            }
            if (!set_tc) { std::cout << color::err("  Set_PDU_CH_State missing from Database\n"); continue; }
            std::uint16_t nm = ps.eps.chan_mask ^ items[ix].bits;
            json vals = {
                { "CH1 to CH8 Enable/Disable",  nm & 0xFF },
                { "CH9 to CH16 Enable/Disable", (nm >> 8) & 0xFF },
            };
            send_and_report(log, sess, Subsystem::EPS, *set_tc,
                            set_tc->node, vals, false, 0, false,
                            std::string("Power ")
                              + ((ps.eps.chan_mask & items[ix].bits) ? "OFF " : "ON ")
                              + items[ix].name);
            g_pm->refresh_now();
        }
    }
}


// ─── TM downlink codec (SOF FCA1) ────────────────────────────────────────────
// File-scope singleton: used by the session's FCA1 decode path.  The rate-
// group Master Frame views moved to the standalone xdos_tm_view terminal.
static ccsds::CcsdsTmCodec g_tm_codec;


// ─── [g] Binary rate-group LUT — live composer, generator, uplink ────────────
// New workflow (no static input file):
//   * the operator COMPOSES the table here: select TCs, choose TM parameter
//     picks (multiple per TM), assign VH/H/M/L priority — all at the prompt
//   * [b]uild auto-generates BOTH local artifacts:
//       config/rate_group_lut.bin          (OBC binary — the ONLY uplink)
//       config/rate_group_lut_bin_gs.json  (ground-only log/debug twin;
//                                           also the resume state for edits)
//   * [s]end transmits ONLY the .bin over the backdoor link to the OBC's
//     LUT listener (routing port + 3).  The .json NEVER leaves the ground.

// Both artifacts live in the project's config/ directory.  These used to be
// plain CWD-relative strings, so launching the CLI from anywhere without a
// config/ beside it (scripts/, build/, $HOME) made [b]uild fail with
// "cannot write config/rate_group_lut.bin".  find_project_root() anchors them.
static const fs::path& blut_config_dir() {
    static const fs::path dir = find_project_root() / "config";
    return dir;
}
static const std::string& kBlutBin() {
    static const std::string p = (blut_config_dir() / "rate_group_lut.bin").string();
    return p;
}
static const std::string& kBlutTwin() {
    static const std::string p =
        (blut_config_dir() / "rate_group_lut_bin_gs.json").string();
    return p;
}

static void blut_print_summary(const xdos::lut::BuildResult& r) {
    using namespace xdos::lut;
    for (int t = 0; t < 4; ++t) {
        const auto& sg = r.segs[t];
        char line[96];
        std::snprintf(line, sizeof(line), "  %-2s  %2zu cmds   %4d / %4d B",
                      kTierName[t], sg.entries.size(), sg.used, sg.budget);
        std::cout << (sg.used > sg.budget ? color::err(line) : std::string(line));
        std::cout << "\n";
    }
    for (const auto& w : r.warnings) std::cout << color::warn("  ! " + w) << "\n";
    for (const auto& e : r.errors)   std::cout << color::err ("  ✗ " + e) << "\n";
}

// Rebuild a composer source table from the ground twin (resume-for-edit).
static nlohmann::json blut_twin_to_source(const nlohmann::json& twin) {
    using namespace xdos::lut;
    nlohmann::json src = { {"version", 1}, {"tiers", nlohmann::json::object()} };
    for (int t = 0; t < 4; ++t) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : twin["tiers"].value(kTierName[t], nlohmann::json::array())) {
            nlohmann::json je = { {"id", e.value("id","")},
                                  {"subsystem", e.value("subsystem","")},
                                  {"tc",  e.value("tc","")},
                                  {"rsp", e.value("rsp","")} };
            if (e.contains("gps_log_id")) je["gps_log_id"] = e["gps_log_id"];
            nlohmann::json picks = nlohmann::json::array();
            for (const auto& p : e.value("picks", nlohmann::json::array())) {
                auto params = p.value("params", nlohmann::json::array());
                bool full = false, raw = false;
                nlohmann::json names = nlohmann::json::array();
                for (const auto& n : params) {
                    std::string nn = n.get<std::string>();
                    if (nn == "<full payload>") full = true;
                    else if (nn == "<bytes>")   raw  = true;
                    else names.push_back(nn);
                }
                if (full) continue;                       // empty picks = full
                if (!names.empty()) picks.push_back({ {"params", names} });
                if (raw && names.empty())
                    picks.push_back({ {"bytes", { p.value("src",0),
                                                  p.value("size",0) }} });
            }
            je["picks"] = picks;
            arr.push_back(std::move(je));
        }
        src["tiers"][kTierName[t]] = std::move(arr);
    }
    return src;
}

// Parameter-pick selector for one response TM.  Returns the picks json
// (empty array = one full-payload pick).  Multiple picks per TM supported;
// adjacent selections coalesce at build time.
static nlohmann::json blut_select_picks(xdos::lut::BinaryLutBuilder& bld,
                                        Subsystem sub, const std::string& rsp) {
    auto pmap = bld.response_param_map(sub, rsp);
    int rlen  = bld.raw_response_len(sub, rsp);
    std::cout << "  " << color::header("Response " + rsp)
              << color::hint("  — " + std::to_string(rlen) + " B raw payload") << "\n";
    std::cout << color::hint("   #   off  size  name                            type\n");
    for (size_t i = 0; i < pmap.size(); ++i) {
        char l[128];
        std::snprintf(l, sizeof(l), "   %-3zu %4d  %4d  %-30s  %-6s %s",
                      i+1, pmap[i].off, pmap[i].size, pmap[i].name.c_str(),
                      pmap[i].type.c_str(), pmap[i].unit.c_str());
        std::cout << l << "\n";
    }
    std::string in = trim(read_line(
        "  Picks: numbers/names comma-list | bytes OFF:SIZE | [Enter]=full payload > "));
    nlohmann::json picks = nlohmann::json::array();
    if (in.empty() || in == "all") return picks;
    nlohmann::json params = nlohmann::json::array();
    std::stringstream ssv(in); std::string tok;
    while (std::getline(ssv, tok, ',')) {
        tok = trim(tok);
        if (tok.empty()) continue;
        if (tok.rfind("bytes", 0) == 0) {
            int off = 0, szv = 0;
            if (std::sscanf(tok.c_str(), "bytes %d:%d", &off, &szv) == 2)
                picks.push_back({ {"bytes", {off, szv}} });
            else std::cout << color::err("  bad bytes spec ignored: " + tok + "\n");
        } else if (auto n = parse_int(tok)) {
            if (*n >= 1 && *n <= (int)pmap.size()) params.push_back(pmap[*n-1].name);
            else std::cout << color::err("  bad param # ignored: " + tok + "\n");
        } else params.push_back(tok);                 // by name; build validates
    }
    if (!params.empty()) picks.push_back({ {"params", params} });
    return picks;
}

// Numbered flat listing of all composed entries; returns {tier, index} pairs.
static std::vector<std::pair<std::string,int>>
blut_list(const nlohmann::json& src) {
    using namespace xdos::lut;
    std::vector<std::pair<std::string,int>> map;
    for (int t = 0; t < 4; ++t) {
        const auto& arr = src["tiers"][kTierName[t]];
        for (size_t i = 0; i < arr.size(); ++i) {
            const auto& e = arr[i];
            char l[128];
            std::snprintf(l, sizeof(l), "   %-3zu [%-2s] %-8s %-6s %s",
                          map.size()+1, kTierName[t],
                          e.value("id","?").c_str(),
                          e.value("subsystem","?").c_str(),
                          e.value("rsp","?").c_str());
            std::cout << l << color::hint(
                e.value("picks", nlohmann::json::array()).empty()
                    ? "   picks: full" : "   picks: selected") << "\n";
            map.push_back({ kTierName[t], (int)i });
        }
    }
    if (map.empty()) std::cout << color::hint("   (table is empty — [a]dd commands)\n");
    return map;
}

static void blut_add_command(xdos::lut::BinaryLutBuilder& bld,
                             const mib::Mib& mib, nlohmann::json& src) {
    std::string ss = trim(read_line("  Subsystem [ADCS/EPS/GPS/XLINK/UHF] > "));
    for (auto& ch : ss) ch = std::toupper((unsigned char)ch);
    Subsystem sub;
    if      (ss == "ADCS")  sub = Subsystem::ADCS;
    else if (ss == "EPS")   sub = Subsystem::EPS;
    else if (ss == "GPS")   sub = Subsystem::GPS;
    else if (ss == "XLINK") sub = Subsystem::XLINK;
    else if (ss == "UHF")   sub = Subsystem::UHF;
    else { std::cout << color::err("  unknown subsystem\n"); return; }

    nlohmann::json je = { {"subsystem", ss} };
    std::string rsp;
    if (sub == Subsystem::ADCS) {
        // ADCS: the poll IS a TM request — choose the TM by id or name.
        std::string q = trim(read_line("  ADCS TM id or name > "));
        const mib::Message* tm = nullptr;
        if (auto idn = parse_int(q)) {
            for (const auto& m : mib.messages(Subsystem::ADCS, Direction::TM))
                if (m->msg_id == *idn) { tm = m; break; }
        } else tm = mib.find(Subsystem::ADCS, Direction::TM, q);
        if (!tm) { std::cout << color::err("  ADCS TM not found\n"); return; }
        rsp = tm->name;
        je["id"] = "AD-" + std::to_string(tm->msg_id);
        je["tc"] = rsp; je["rsp"] = rsp;
    } else if (sub == Subsystem::GPS) {
        std::cout << color::hint("   GPS logs:\n");
        std::vector<const mib::Message*> logs;
        for (const auto& m : mib.messages(Subsystem::GPS, Direction::TM)) {
            char l[96];
            std::snprintf(l, sizeof(l), "   %-3zu id %-4d %s",
                          logs.size()+1, m->msg_id, m->name.c_str());
            std::cout << l << "\n";
            logs.push_back(m);
        }
        auto n = parse_int(trim(read_line("  Log # > ")));
        if (!n || *n < 1 || *n > (int)logs.size()) { std::cout << color::err("  bad index\n"); return; }
        rsp = logs[*n-1]->name;
        je["id"] = "GPS-" + std::to_string(logs[*n-1]->msg_id);
        je["tc"] = "LOG"; je["rsp"] = rsp;
        je["gps_log_id"] = logs[*n-1]->msg_id;
    } else {
        // EPS / XLINK: list TCs that have a matching _RSP response TM.
        // UHF: replies drop the GET_ prefix instead (GET_TELEM -> TELEM).
        std::vector<std::pair<const mib::Message*, const mib::Message*>> cands;
        for (const auto& tc : mib.messages(sub, Direction::TC)) {
            std::string rn = (sub == Subsystem::UHF && tc->name.rfind("GET_", 0) == 0)
                                 ? tc->name.substr(4) : tc->name + "_RSP";
            const mib::Message* rm = mib.find(sub, Direction::TM, rn);
            if (rm) cands.push_back({tc, rm});
        }
        for (size_t i = 0; i < cands.size(); ++i) {
            char l[110];
            std::snprintf(l, sizeof(l), "   %-3zu %-38s -> %s",
                          i+1, cands[i].first->name.c_str(),
                          cands[i].second->name.c_str());
            std::cout << l << "\n";
        }
        auto n = parse_int(trim(read_line("  TC # > ")));
        if (!n || *n < 1 || *n > (int)cands.size()) { std::cout << color::err("  bad index\n"); return; }
        rsp = cands[*n-1].second->name;
        je["id"] = std::string(sub == Subsystem::EPS ? "EP-"
                             : sub == Subsystem::UHF ? "UH-" : "XL-")
                 + std::to_string(cands[*n-1].first->msg_id);
        je["tc"]  = cands[*n-1].first->name;
        je["rsp"] = rsp;
    }
    std::string tier = trim(read_line("  Priority tier [VH/H/M/L] > "));
    for (auto& ch : tier) ch = std::toupper((unsigned char)ch);
    if (!src["tiers"].contains(tier)) { std::cout << color::err("  no such tier\n"); return; }
    je["picks"] = blut_select_picks(bld, sub, rsp);
    src["tiers"][tier].push_back(std::move(je));
    std::cout << color::ok("  added to " + tier) << "\n";
}

static void binary_lut_flow(const mib::Mib& mib, const GsConfig& cfg) {
    using namespace xdos::lut;
    BinaryLutBuilder bld(mib);
    // Bake the real XLINK product key into replayed TCs when the vendor
    // catalog is loaded (same policy as xdos_lut_csv; zero key = sim only).
    if (xlink_catalog() && !xlink_catalog()->entries.empty())
        bld.set_xlink_key(xlink_catalog()->entries.front().key);
    nlohmann::json src = { {"version",1},
        {"tiers", { {"VH", nlohmann::json::array()}, {"H", nlohmann::json::array()},
                    {"M",  nlohmann::json::array()}, {"L", nlohmann::json::array()} }} };
    // Resume-for-edit: the ground twin of the last generated binary.
    {
        std::ifstream f(kBlutTwin());
        if (f) {
            nlohmann::json twin;
            try {
                f >> twin;
                std::string a = trim(read_line(
                    "  Existing LUT found — [r]esume it for editing or [n]ew empty table? [r/n] > "));
                if (a.empty() || a[0]=='r' || a[0]=='R')
                    src = blut_twin_to_source(twin);
            } catch (...) {}
        }
    }
    while (true) {
        auto r = bld.build(src);
        std::cout << "\n  " << color::header("Binary rate-group LUT — composer") << "\n";
        blut_print_summary(r);
        std::string c = trim(read_line(
            "  [a]dd  [i]mport CSV  [p]icks  [t]ier  [d]elete  [l]ist  [b]uild  [s]end .bin to OBC  [q] back > "));
        if (c.empty() || c == "q" || c == "Q") break;
        if (c == "l" || c == "L") { blut_list(src); continue; }
        if (c == "a" || c == "A") { blut_add_command(bld, mib, src); continue; }
        if (c == "i" || c == "I") {
            // Import the operator's CSV master list — replaces the table.
            std::string def = (blut_config_dir() / "rate_group_commands.csv").string();
            std::string path = trim(read_line("  CSV file [" + def + "] > "));
            if (path.empty()) path = def;
            auto parsed = parse_rate_group_csv_file(path, mib);
            for (const auto& w : parsed.warnings)
                std::cout << color::warn("  ! " + w) << "\n";
            if (!parsed.ok()) {
                for (const auto& e : parsed.errors)
                    std::cout << color::err("  ✗ " + e) << "\n";
                std::cout << color::err("  import aborted — fix the CSV and retry\n");
                continue;
            }
            src = parsed.source;
            std::cout << color::ok("  imported " + path
                + " — review below, then [b]uild + [s]end") << "\n";
            continue;
        }
        if (c == "p" || c == "P" || c == "t" || c == "T" || c == "d" || c == "D") {
            auto map = blut_list(src);
            if (map.empty()) continue;
            auto n = parse_int(trim(read_line("  Entry # > ")));
            if (!n || *n < 1 || *n > (int)map.size()) { std::cout << color::err("  bad index\n"); continue; }
            std::string tier = map[*n-1].first;
            int idx = map[*n-1].second;
            auto& je = src["tiers"][tier][idx];
            if (c == "d" || c == "D") {
                src["tiers"][tier].erase(idx);
                std::cout << color::ok("  deleted\n");
            } else if (c == "t" || c == "T") {
                std::string nt = trim(read_line("  New tier [VH/H/M/L] > "));
                for (auto& ch : nt) ch = std::toupper((unsigned char)ch);
                if (!src["tiers"].contains(nt)) { std::cout << color::err("  no such tier\n"); continue; }
                src["tiers"][nt].push_back(je);
                src["tiers"][tier].erase(idx);
                std::cout << color::ok("  moved to " + nt) << "\n";
            } else {
                std::string ss = je.value("subsystem","EPS");
                Subsystem sub = ss=="GPS"?Subsystem::GPS: ss=="XLINK"?Subsystem::XLINK:
                                ss=="ADCS"?Subsystem::ADCS:
                                ss=="UHF"?Subsystem::UHF:Subsystem::EPS;
                je["picks"] = blut_select_picks(bld, sub, je.value("rsp",""));
            }
            continue;
        }
        if (c == "b" || c == "B") {
            if (!r.ok()) { std::cout << color::err("  fix the errors above first\n"); continue; }
            std::string werr;
            if (!BinaryLutBuilder::write(r, kBlutBin(), kBlutTwin(), &werr)) {
                std::cout << color::err("  " + werr + "\n"); continue;
            }
            char sig[16]; std::snprintf(sig, sizeof(sig), "0x%04X", r.crc);
            std::cout << color::ok("  wrote " + kBlutBin())
                      << color::hint("  (" + std::to_string(r.file.size())
                         + " B, CRC-16 " + std::string(sig) + ")") << "\n"
                      << color::ok("  wrote " + kBlutTwin())
                      << color::hint("  (ground-only log/debug — NEVER uplinked)") << "\n";
            continue;
        }
        if (c == "s" || c == "S") {
            // STRICT: only the BINARY crosses the link.
            std::ifstream f(kBlutBin(), std::ios::binary);
            if (!f) { std::cout << color::err("  " + kBlutBin()
                        + " not found — [b]uild first\n"); continue; }
            std::string body((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
            Bytes frame = { 'X','L','U','T' };
            std::uint32_t len = (std::uint32_t)body.size();
            frame.push_back((len >> 24) & 0xFF); frame.push_back((len >> 16) & 0xFF);
            frame.push_back((len >> 8) & 0xFF);  frame.push_back(len & 0xFF);
            frame.insert(frame.end(), body.begin(), body.end());
            transport::TcpClient cl;
            int port = cfg.obc_port + 3;
            if (!cl.connect(cfg.obc_host, port, 1500)) {
                std::cout << color::err("  cannot reach OBC LUT port "
                    + cfg.obc_host + ":" + std::to_string(port) + "\n");
                continue;
            }
            if (!cl.send(frame)) { std::cout << color::err("  send failed\n"); continue; }
            auto reply = cl.recv(256, 3000);
            std::string rr(reply.begin(), reply.end());
            std::cout << (rr.rfind("OK",0)==0 ? color::ok("  OBC: " + rr)
                                              : color::err("  OBC: " + (rr.empty()?"no reply":rr)))
                      << "\n";
            continue;
        }
    }
}

int main(int argc, char** argv) {
    xdos::net::Startup _net_init;        // no-op on POSIX, WSAStartup on Windows
    GsConfig cfg;
    std::string script_path;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--no-color") color::set_use_color(false);
        else if (a == "--no-obc")   cfg.via_obc = false;
        else if (a == "--via-obc")  cfg.via_obc = true;
        else if (a == "--obc-port" && i+1 < argc) cfg.obc_port = std::atoi(argv[++i]);
        else if (a == "--obc-host" && i+1 < argc) cfg.obc_host = argv[++i];
        else if (a == "--adcs-bare-tc")    cfg.adcs_bare_tc = true;
        else if (a == "--no-adcs-bare-tc") cfg.adcs_bare_tc = false;
        else if (a == "--xlink-host" && i+1 < argc) cfg.xlink_host = argv[++i];
        else if (a == "--xlink-port" && i+1 < argc) cfg.xlink_port = std::atoi(argv[++i]);
        else if (a == "--xlink-key"  && i+1 < argc) cfg.xlink_key  = argv[++i];
        else if (a == "--script" && i+1 < argc) script_path = argv[++i];
        else if (a == "--help" || a == "-h") {
            std::cout <<
                "xdos_gs — ground station CLI\n"
                "  --via-obc / --no-obc      route through OBC bridge (default on)\n"
                "  --obc-host HOST           OBC host (default 192.168.1.111)\n"
                "  --obc-port PORT           OBC port (default 7000)\n"
                "  --adcs-bare-tc            send ADCS TCs without 1F 7F / 1F FF\n"
                "                            (the flight OBC adds them; default:\n"
                "                             auto — on for a non-loopback OBC host)\n"
                "  --no-adcs-bare-tc         always send complete ADCS frames\n"
                "  --xlink-host HOST         XLINK-S transceiver host (only with --no-obc)\n"
                "  --xlink-port PORT         XLINK-S service port (default 10050 sim / 51003 real HW)\n"
                "  --xlink-key HEX32         product key 'aabbccdd-eeff-...' (32 hex digits)\n"
                "  --script FILE             run commands from a script file\n"
                "  --no-color                strip ANSI escapes\n";
            return 0;
        }
    }

    mib::Mib mib; mib.load_dir(find_mib_dir());
    print_mib_stats(mib);
    session::Session sess(mib);

    // ── CCSDS codec — load profile and attach to session (default ON) ──
    ccsds::CcsdsCodec ccsds_codec;
    bool ccsds_loaded = false;
    try {
        auto mib_dir = find_mib_dir();
        auto path = mib_dir.string() + "/ccsds_tc.json";
        if (std::filesystem::exists(path)) {
            ccsds_codec.load(path);
            ccsds_loaded = true;
            sess.set_ccsds_codec(&ccsds_codec);
            g_link_gs_id = ccsds_codec.ground_station_id();
    // TM downlink codec (SOF FCA1) — file-scope singleton (see g_tm_codec).
    // Resolve via find_mib_dir() like the TC profile above so the codec loads
    // regardless of the working directory the GS was launched from.
    {
        auto tm_path = (mib_dir / "ccsds_tm.json").string();
        if (std::filesystem::exists(tm_path) && g_tm_codec.load(tm_path)) {
            sess.set_tm_codec(&g_tm_codec);
            g_link_radio_id = g_tm_codec.default_radio_module_id();
        }
    }
            sess.set_ccsds_enabled(ccsds_codec.enabled_by_default());
        }
    } catch (const std::exception& e) {
        std::cerr << color::warn(std::string("[GS] CCSDS profile load failed: ")
                                 + e.what()) << "\n";
    }

    // ── ADCS TC framing — bare body vs complete frame ──────────────────────
    // The flight OBC wraps the ADCS TC data field in 1F 7F … 1F FF itself
    // before putting it on the ADCS UART, so sending a complete frame from
    // the ground would reach the ADCS with a doubled SOM/EOM.  Neither the
    // ADCS sim (--no-obc) nor the OBC sim adds delimiters, and both run on
    // loopback, so "auto" keys off the OBC host: real bench hardware gets the
    // bare body, every simulator topology keeps the framing it has today.
    {
        bool loopback = cfg.obc_host == "127.0.0.1" ||
                        cfg.obc_host == "localhost"  ||
                        cfg.obc_host == "::1";
        bool bare = cfg.adcs_bare_tc.value_or(cfg.via_obc && !loopback);
        sess.set_adcs_bare_tc(bare);
        if (bare)
            std::cout << color::hint("  ADCS TC framing: bare body — the OBC "
                                     "adds 1F 7F / 1F FF\n");
    }

    // ── XLINK-S product-key catalog — auto-load first vendor file we find.
    // Real hardware requires per-config keys; without a loaded catalog the GS
    // sends the all-zero key and the sim will accept it (real HW will NAK).
    try {
        auto mib_dir = find_mib_dir();
        auto keys_dir = mib_dir / "xlink_keys";
        if (std::filesystem::is_directory(keys_dir)) {
            for (auto const& entry : std::filesystem::directory_iterator(keys_dir)) {
                if (entry.path().extension() != ".txt") continue;
                auto cat = xdos::xlink::KeyCatalog::load(entry.path().string());
                if (cat) {
                    // Use the first entry as the default active key; operator
                    // can override with [k] in the XLINK-S menu.
                    sess.set_xlink_key(cat->entries.front().key);
                    xlink_catalog() = std::move(cat);
                    std::cout << color::hint("  Loaded XLINK-S key catalog: ")
                              << color::addr(entry.path().filename().string())
                              << color::hint("  (SN ") << xlink_catalog()->serial_number
                              << color::hint(", ") << xlink_catalog()->entries.size()
                              << color::hint(" configs)") << "\n";
                    break;
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << color::warn(std::string("[GS] XLINK-S key catalog load failed: ")
                                 + e.what()) << "\n";
    }

    print_banner(cfg);
    Logger log;
    std::cout << "  Log file: " << color::addr(log.filename()) << "\n";
    if (ccsds_loaded) {
        std::cout << "  CCSDS framing: "
                  << (sess.ccsds_enabled() ? color::ok("ON ") : color::warn("OFF "))
                  << color::hint("(profile ") << color::addr(ccsds_codec.profile_name())
                  << color::hint(", SCID ") << color::addr("0x"
                                + ([](int v){ char b[8]; std::snprintf(b,8,"%03X",v); return std::string(b); })(ccsds_codec.spacecraft_id()))
                  << color::hint(")") << "\n";
    } else {
        std::cout << "  CCSDS framing: " << color::warn("not loaded — native only") << "\n";
    }
    std::cout << "\n";
    std::cout << color::header("Connecting...") << "\n";
    if (!connect_session(sess, cfg)) {
        std::cout << (cfg.via_obc
            ? color::err("  OBC link down — telecommands will fail until it connects.\n")
            : color::err("  No buses connected. Are the sims running?\n"));
    }

    // ── batch script mode — run file and exit ──
    if (!script_path.empty()) {
        run_script(log, sess, mib, script_path);
        sess.disconnect_all();
        return 0;
    }

    // ── flight power monitor (EGSE) — drives the status bar and [p] menu ──
    PowerMonitor pm(mib, FlightConfig::load());
    g_pm = &pm;
    pm.start();

    // ── Master Frame store: receive the OBC's autonomous rate-group stream ──
    // Rate-group Master Frame telemetry is displayed by the standalone
    // xdos_tm_view terminal (OBC rate-group port = routing port + 2).

    LastSent last;
    CommandHistory history;
    while (true) {
        print_main_menu(sess.ccsds_enabled(), ccsds_loaded, &sess, !history.empty(),
                        cfg.via_obc);
        std::string s = trim(read_line(""));
        if (s.empty()) continue;
        char c = s[0];
        if (c == '1') subsystem_flow(log, sess, mib, Subsystem::ADCS, last);
        else if (c == '2') subsystem_flow(log, sess, mib, Subsystem::EPS,  last);
        else if (c == '3') subsystem_flow(log, sess, mib, Subsystem::GPS,  last);
        else if (c == '4' && !cfg.via_obc) {
            std::cout << color::warn("  Payload TC/TM is routed through the OBC only —\n"
                                     "  there is no direct link to the payload server.\n"
                                     "  Restart without --no-obc to command the payload.\n");
        }
        else if (c == '4' && !sess.ccsds_enabled()) {
            std::cout << color::warn("  Payload requires CCSDS ON. Press [c] to enable.\n");
        }
        else if (c == '4') payload_flow(log, sess, mib);
        else if (c == '5') xlink_flow(log, sess, mib);
        else if (c == '6') subsystem_flow(log, sess, mib, Subsystem::UHF, last);
        else if (c == '7') subsystem_flow(log, sess, mib, Subsystem::SENSOR, last);
        else if ((c == 'c' || c == 'C') && ccsds_loaded) {
            bool now = !sess.ccsds_enabled();
            sess.set_ccsds_enabled(now);
            std::cout << "\n  CCSDS framing is now "
                      << (now ? color::ok("ON") : color::warn("OFF")) << "\n";
        }
        else if (c == 'h' || c == 'H') {
            show_main_help();
        }
        else if (c == '!') {
            show_and_replay_history(log, sess, mib, history);
        }
        else if (c == 'p' || c == 'P') {
            power_flow(log, sess, mib);
        }
        else if (c == 'g' || c == 'G') {
            binary_lut_flow(mib, cfg);
        }
        else if (c == 'r' || c == 'R') {
            std::cout << "\n" << color::header("Reconnecting...") << "\n";
            sess.disconnect_all();
            connect_session(sess, cfg);
        }
        else if (c == 'q' || c == 'Q') break;
        else std::cout << color::warn("  (unknown: 1/2/3/4/5/6/p/g/c/!/h/r/q)\n");
    }
    g_pm = nullptr;
    pm.stop();
    std::cout << "\nGround station exiting.\n";
    sess.disconnect_all();
    return 0;
}
