// SPDX-License-Identifier: Apache-2.0
// XDOS Ground Station — xdos_tm_view: standalone Master Frame telemetry
// terminal.
//
// Runs in its OWN terminal, separate from the telecommanding CLI (xdos_gs).
// Connects to the OBC's dedicated rate-group port (routing port + 2) and
// reassembles the tiered master frame the binary rate-group LUT defines
// (subframe = VH/H/M/L segments; H/M/L blocks span 2/4/8 subframes).  Entry
// names and picks come from the GS twin (config/rate_group_lut_bin_gs.json);
// the OBC transmits nothing until a binary LUT has been uplinked.
//   [m] Master Frame browser   (MF list -> 16 slots -> decoded entries)
//   [t] Rate Group TM Response (per-subframe hex/decode window, auto-log)
//
// Usage:  ./xdos_tm_view [--obc-host H] [--obc-port P] [--rg-port N]
//                        [--twin FILE] [--no-color]
//   default H:P = 192.168.1.111:7000 (bench), rate-group port = P + 2
//   sims:   ./xdos_tm_view --obc-host 127.0.0.1 --obc-port 10000

#include "codec/ccsds_tm_codec.hpp"
#include "common/color.hpp"
#include "common/socket_compat.hpp"
#include "mib/mib.hpp"
#include "cli/master_frame_store.hpp"
#include "cli/mcc_forward.hpp"
#include "cli/value_format.hpp"
#include "subsystems/xlink_codec.hpp"
#include "codec/codec.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <sstream>
#include <iomanip>
#include <thread>

using namespace xdos;
using namespace xdos::lut;
using nlohmann::json;
using xdos::cli::format_value;
using xdos::cli::EntryView;
using xdos::cli::TwinEntry;
namespace fs = std::filesystem;

// ─── tiny local helpers (duplicated from the GS CLI) ─────────────────────────
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
static fs::path find_mib_dir() {
    for (auto p : { "mib", "../mib", "../../mib" })
        if (fs::is_directory(p)) return p;
    return "mib";
}

static xdos::cli::MasterFrameStore* g_mfs = nullptr;
static xdos::cli::MccForwarder    g_mcc;
static ccsds::CcsdsTmCodec g_tm_codec;
static const mib::Mib* g_mf_mib = nullptr;

// Per-subsystem colour for hex bytes.
static std::string_view rg_sub_color(Subsystem s) {
    switch (s) {
        case Subsystem::EPS:   return color::ansi::CYAN;
        case Subsystem::GPS:   return color::ansi::GREEN;
        case Subsystem::XLINK: return color::ansi::MAGENTA;
        case Subsystem::ADCS:  return color::ansi::YELLOW;
        case Subsystem::UHF:   return color::ansi::BLUE;
        default:               return color::ansi::WHITE;
    }
}

static std::string status_text(EntryView::Status st) {
    switch (st) {
        case EntryView::UP:      return color::ok("UP");
        case EntryView::DOWN:    return color::err("DOWN");
        default:                 return color::warn("PARTIAL");
    }
}

// ─── entry decode: sparse raw payload -> named parameter lines ───────────────
// The entry's picks say which parameters (or raw byte ranges) the LUT keeps;
// only those are decoded — the rest of the sparse payload is padding.  XLINK
// payloads use absolute-from-frame-start offsets, so 20 zero bytes are
// prepended to realign before the XLink codec (native header stripped by the
// OBC).  `with_tags` adds the [TYPE unit] column for the browser view.
static std::string entry_decode_text(const TwinEntry& e, const Bytes& raw,
                                     bool with_tags) {
    if (!g_mf_mib) return "";
    const mib::Message* tm = g_mf_mib->find(e.sub, Direction::TM, e.rsp);
    if (!tm) return "      (no Database definition for " + e.rsp + ")\n";

    // Which parameters do the picks cover?
    bool full = false;
    std::set<std::string> wanted;
    std::vector<const xdos::cli::TwinPick*> byte_picks;
    for (const auto& p : e.picks) {
        for (const auto& n : p.params) {
            if (n == "<full payload>") full = true;
            else if (n == "<bytes>")   byte_picks.push_back(&p);
            else                       wanted.insert(n);
        }
    }

    std::ostringstream os;
    auto emit = [&](const mib::Parameter& p, const json& v) {
        os << "      " << std::left << std::setw(28) << p.name;
        if (with_tags) {
            std::string tag = "[" + p.data_type + (p.unit ? " " + *p.unit : "") + "]";
            os << "  " << std::setw(14) << std::left << tag;
        }
        os << " = " << format_value(v, p) << "\n";
    };
    try {
        if (e.sub == Subsystem::XLINK) {
            Bytes framed(20, 0x00);
            framed.insert(framed.end(), raw.begin(), raw.end());
            auto dec = xdos::xlink::codec::decode_frame(framed, tm);
            if (!dec.framed_ok) return "      (decode failed)\n";
            for (const auto& p : tm->parameters) {
                if (p.name == "product_key" || p.name == "service_event") continue;
                if (p.data_type == "RESERVED" || p.data_type == "HEX") continue;
                if (!full && !wanted.count(p.name)) continue;
                auto it = dec.values.find(p.name);
                if (it == dec.values.end()) continue;
                emit(p, *it);
            }
        } else {
            json vals = xdos::codec::decode(*tm, raw);
            for (const auto& p : tm->parameters) {
                if (p.data_type == "RESERVED") continue;
                if (!full && !wanted.count(p.name)) continue;
                auto it = vals.find(p.name);
                if (it == vals.end()) continue;
                emit(p, *it);
            }
            // UHF radio-config replies: engineering values from the registers
            if (e.sub == Subsystem::UHF)
                os << xdos::cli::uhf_derived_text(e.rsp, vals, "      ");
        }
        // raw byte-range picks have no names — show them as hex
        for (const auto* p : byte_picks) {
            os << "      " << std::left << std::setw(28)
               << ("bytes " + std::to_string(p->src) + ":"
                   + std::to_string(p->size)) << " =";
            char b[4];
            for (int i = 0; i < p->size && p->src + i < (int)raw.size(); ++i) {
                std::snprintf(b, sizeof(b), " %02x", raw[p->src + i]);
                os << b;
            }
            os << "\n";
        }
    } catch (const std::exception& ex) {
        os << "      (decode error: " << ex.what() << ")\n";
    }
    return os.str();
}

// Fetch the EntryViews for one subframe (needs its containing master frame).
static std::vector<EntryView> views_for(const xdos::cli::SubFrame& sf) {
    if (!g_mfs || !g_mfs->has_twin()) return {};
    auto mf = g_mfs->master_by_id(sf.mf_id);
    if (!mf) return {};
    return g_mfs->decode_entries(*mf, sf.slot);
}

// Raw fallback: the four tier segments of this subframe as coloured hex.
static void render_raw_segments(const xdos::cli::SubFrame& sf) {
    static const std::string_view seg_col[4] = {
        color::ansi::YELLOW, color::ansi::CYAN,
        color::ansi::GREEN,  color::ansi::MAGENTA };
    for (int t = 0; t < 4; ++t) {
        std::cout << "  " << color::header(std::string(kTierName[t]) + " segment")
                  << color::hint("  [" + std::to_string(kSegOffset[t]) + ".."
                     + std::to_string(kSegOffset[t] + kSegBytes[t]) + ") slice "
                     + std::to_string(sf.slot % kBlockSpan[t] + 1) + "/"
                     + std::to_string(kBlockSpan[t])) << "\n      ";
        char b[4];
        for (int i = 0; i < kSegBytes[t]; ++i) {
            std::snprintf(b, sizeof(b), "%02x", sf.tm_data[kSegOffset[t] + i]);
            std::cout << color::wrap(seg_col[t], b) << " ";
            if ((i + 1) % 16 == 0) std::cout << "\n      ";
        }
        std::cout << "\n";
    }
}

// Every tier's state for THIS subframe — printed whether or not the tier's
// block completes here, so a glance answers "are VH, H, M and L all coming?".
static void render_tier_coverage(const xdos::cli::SubFrame& sf) {
    if (!g_mfs || !g_mfs->has_twin()) return;
    auto mf = g_mfs->master_by_id(sf.mf_id);
    if (!mf) return;
    auto cov = g_mfs->tier_coverage(*mf, sf.slot);
    if (cov.empty()) return;
    std::cout << "\n  " << color::header("── tier coverage (this subframe) ──")
              << "\n";
    for (const auto& c : cov) {
        std::string slices;
        for (int j = 0; j < c.span && j < 8; ++j) slices += c.have[j] ? 'x' : '.';
        char hd[80];
        std::snprintf(hd, sizeof(hd), "   %-2s  %2d entries  slice %d/%d ",
                      kTierName[c.tier], c.entries, c.slice + 1, c.span);
        std::cout << hd << color::hint("[" + slices + "]") << "  ";
        // A tier is healthy when every entry decoded UP.
        if (c.down == 0 && c.partial == 0)
            std::cout << color::ok("all UP");
        else {
            if (c.up)      std::cout << color::ok(std::to_string(c.up) + " UP") << " ";
            if (c.down)    std::cout << color::err(std::to_string(c.down) + " DOWN") << " ";
            if (c.partial) std::cout << color::warn(std::to_string(c.partial) + " PARTIAL");
        }
        std::cout << (c.block_complete
                      ? color::hint("   block COMPLETE")
                      : color::hint("   block FILLING, "
                            + std::to_string(c.missing_slices()) + " missing"))
                  << "\n";
    }
    std::cout << "\n";
}

// ─── shared subframe renderer ([t] window and [m] browser) ───────────────────
static void render_subframe(const xdos::cli::SubFrame& sf,
                            const std::string& title, bool hex_mode) {
    std::cout << "\n" << color::header(title)
              << "   mode: " << (hex_mode ? color::wrap(color::ansi::BR_YELLOW, "HEX")
                                          : color::wrap(color::ansi::BR_CYAN, "DECODE")) << "\n";
    std::cout << "  time " << color::addr(sf.date_ymd() + " " + sf.time_hms())
              << "." << color::hint(std::to_string(sf.ms))
              << "   MF#" << sf.mf_id << " slot " << (sf.slot + 1) << "/16"
              << "   MFC=" << sf.master_count << " SFC=" << sf.sub_count
              << " RM=" << sf.rm_id
              << "   CRC " << (sf.crc_ok ? color::ok("OK") : color::err("BAD")) << "\n";

    if (!g_mfs || !g_mfs->has_twin()) {
        std::cout << color::warn("  No GS twin loaded — raw segment view.\n\n");
        render_raw_segments(sf);
        return;
    }
    auto views = views_for(sf);
    if (views.empty()) {
        std::cout << color::hint("  (master frame no longer in the store)\n");
        return;
    }
    render_tier_coverage(sf);
    int last_tier = -1;
    for (std::size_t i = 0; i < views.size(); ++i) {
        const auto& v = views[i];
        const auto& e = *v.entry;
        if (v.tier != last_tier) {
            last_tier = v.tier;
            const int span = kBlockSpan[v.tier];
            std::string blk = span == 1
                ? "refreshes every subframe"
                : "block slice " + std::to_string(sf.slot % span + 1) + "/"
                  + std::to_string(span);
            std::cout << "\n  " << color::header(std::string("── ")
                      + kTierName[v.tier] + " ── ") << color::hint(blk) << "\n";
        }
        char hd[96];
        std::snprintf(hd, sizeof(hd), "  [%2zu] %-9s %-8s ",
                      i + 1, e.id.c_str(), to_string(e.sub));
        std::cout << hd << status_text(v.status)
                  << color::hint("   " + e.rsp) << "\n";
        if (v.status == EntryView::DOWN) {
            std::cout << "      " << color::wrap(color::ansi::BR_RED,
                "0xFF — subsystem not responding") << "\n";
            continue;
        }
        if (v.status == EntryView::PARTIAL)
            std::cout << "      " << color::warn(
                "block slices missing — values may be incomplete") << "\n";
        if (hex_mode) {
            std::string_view col = rg_sub_color(e.sub);
            std::cout << "      ";
            char b[4];
            for (std::size_t j = 0; j < v.pick_bytes.size(); ++j) {
                std::snprintf(b, sizeof(b), "%02x", v.pick_bytes[j]);
                std::cout << color::wrap(col, b) << " ";
                if ((j + 1) % 16 == 0) std::cout << "\n      ";
            }
            std::cout << "\n";
        } else {
            std::string dec = entry_decode_text(e, v.raw, true);
            std::cout << (dec.empty() ? "      (no fields)\n" : dec);
        }
    }
    std::cout << "\n  Type an entry number for its raw payload bytes, "
                 "[h]ex/[d]ecode, or [b]ack\n";
}

// Interactive loop around one subframe; returns the (possibly toggled) mode.
static void subframe_loop(const xdos::cli::SubFrame& sf,
                          const std::string& title, bool& hex_mode) {
    bool local_hex = hex_mode;
    while (true) {
        render_subframe(sf, title, local_hex);
        auto k = trim(read_line("subframe> "));
        if (k.empty() || k == "b" || k == "B") break;
        if (k == "h" || k == "H") { local_hex = true;  hex_mode = true;  continue; }
        if (k == "d" || k == "D") { local_hex = false; hex_mode = false; continue; }
        auto n = parse_int(k);
        auto views = views_for(sf);
        if (n && *n >= 1 && *n <= (int)views.size()) {
            const auto& v = views[*n - 1];
            std::cout << "\n  " << color::header(v.entry->id + " raw payload")
                      << color::hint("  (sparse " + std::to_string(v.raw.size())
                         + " B, picks filled)") << "\n  ";
            char b[4];
            for (std::size_t j = 0; j < v.raw.size(); ++j) {
                std::snprintf(b, 4, "%02x ", v.raw[j]);
                std::cout << b;
                if ((j + 1) % 16 == 0) std::cout << "\n  ";
            }
            std::cout << "\n";
            (void)read_line("  [enter] back ");
        } else {
            std::cout << color::warn("  type an entry number, h, d, or b\n");
        }
    }
}

// ─── [t] Rate Group Command TM Response Window ───────────────────────────────
static void rate_group_tm_window() {
    if (!g_mfs) { std::cout << color::warn("  TM response store unavailable.\n"); return; }
    bool hex_mode = false;   // default to DECODE; toggle with h/d
    while (true) {
        auto subs = g_mfs->recent_subframes(200);
        std::cout << "\n" << color::header("Rate Group Command TM Response Window") << "   ";
        if (g_mfs->connected()) std::cout << color::ok("● receiving");
        else std::cout << color::err("● not connected (OBC rate-group stream, port +2)");
        std::cout << color::hint("   subframes: " + std::to_string(g_mfs->frames_received())
                  + "   log: " + (g_mfs->log_path().empty() ? "off" : g_mfs->log_path())) << "\n";
        std::cout << color::hint("  Each subframe = one rate-group push (slot k of 16). "
                  "View mode toggles: [h] hex  [d] decode\n\n");

        if (subs.empty()) {
            std::cout << color::hint("  No subframes yet — the OBC transmits nothing until a\n"
                      "  binary LUT is uplinked (xdos_gs [g] menu, [s]end).\n");
        } else {
            std::size_t start = subs.size() > 20 ? subs.size() - 20 : 0;
            for (std::size_t i = start; i < subs.size(); ++i) {
                const auto& sf = subs[i];
                std::cout << "  [" << std::setw(3) << (i + 1) << "]  "
                          << color::addr(sf.date_ymd() + " " + sf.time_hms()) << "."
                          << std::setw(3) << std::setfill('0') << sf.ms << std::setfill(' ')
                          << "   MF#" << sf.mf_id << " slot "
                          << std::setw(2) << (sf.slot + 1) << "/16";
                if (sf.up_count >= 0)
                    std::cout << color::hint("   " + std::to_string(sf.up_count) + "/"
                              + std::to_string(sf.entry_total) + " up");
                std::cout << "\n";
            }
        }
        std::cout << "  Select a subframe number to view · [h]ex/[d]ecode default: "
                  << (hex_mode ? "HEX" : "DECODE") << " · [r]efresh · [b]ack\n";
        auto in = trim(read_line("TM> "));
        if (in.empty()) continue;
        if (in == "b" || in == "B") return;
        if (in == "r" || in == "R") continue;
        if (in == "h" || in == "H") { hex_mode = true;  std::cout << color::hint("  default view → HEX\n"); continue; }
        if (in == "d" || in == "D") { hex_mode = false; std::cout << color::hint("  default view → DECODE\n"); continue; }
        auto n = parse_int(in);
        if (!n || *n < 1 || *n > (int)subs.size()) {
            std::cout << color::warn("  type a subframe number, h, d, r, or b\n");
            continue;
        }
        subframe_loop(subs[*n - 1],
                      "TM Response  ›  Subframe " + std::to_string(*n), hex_mode);
    }
}

// ─── [m] Master Frame browser ────────────────────────────────────────────────
// Whole-master-frame tier matrix: 16 slots down, the four tiers across.
// One screen that answers "over this master frame, did every tier keep
// arriving, and where did it break?".
static void mf_tier_matrix(const xdos::cli::MasterFrame& mf) {
    if (!g_mfs || !g_mfs->has_twin()) {
        std::cout << color::warn("  No GS twin loaded — tier debug needs the twin.\n");
        return;
    }
    std::cout << "\n" << color::header("Tier coverage matrix — Master Frame "
                                       + std::to_string(mf.index)) << "\n";
    std::cout << color::hint("  up/entries per tier on every slot · "
                             "D=down (0xFF, silent) · P=partial (slice missing)\n\n");
    std::cout << "  slot   ";
    for (int t = 0; t < 4; ++t) {
        char h[20]; std::snprintf(h, sizeof(h), "%-14s", kTierName[t]);
        std::cout << color::header(h);
    }
    std::cout << "\n";
    // Tally so the footer can state the whole frame's health per tier.
    int seen[4] = {0,0,0,0}, bad[4] = {0,0,0,0};
    for (int i = 0; i < 16; ++i) {
        char sl[10]; std::snprintf(sl, sizeof(sl), "  [%2d]   ", i + 1);
        std::cout << sl;
        if (!mf.by_slot[i]) { std::cout << color::hint("-- subframe not received --") << "\n"; continue; }
        for (const auto& c : g_mfs->tier_coverage(mf, i)) {
            char cell[24];
            std::snprintf(cell, sizeof(cell), "%2d/%-2d", c.up, c.entries);
            std::string s = cell;
            if (c.down)    s += " D" + std::to_string(c.down);
            if (c.partial) s += " P" + std::to_string(c.partial);
            ++seen[c.tier];
            if (c.down || c.partial) ++bad[c.tier];
            while (s.size() < 14) s += ' ';
            std::cout << (c.down ? color::err(s)
                        : c.partial ? color::warn(s) : color::ok(s));
        }
        std::cout << "\n";
    }
    std::cout << "\n";
    for (int t = 0; t < 4; ++t) {
        std::cout << "  " << kTierName[t] << ": ";
        if (!seen[t])       std::cout << color::hint("never seen (no subframes)");
        else if (!bad[t])   std::cout << color::ok("healthy on all "
                                      + std::to_string(seen[t]) + " received slots");
        else                std::cout << color::warn(std::to_string(bad[t]) + " of "
                                      + std::to_string(seen[t])
                                      + " slots had DOWN or PARTIAL entries");
        std::cout << "\n";
    }
    std::cout << "\n";
    (void)read_line("  [enter] back ");
}

static void mf_show_master(const xdos::cli::MasterFrame& mf) {
    bool hex_mode = false;
    while (true) {
        std::cout << "\n" << color::header("Master Frame " + std::to_string(mf.index))
                  << color::hint("  (id " + std::to_string(mf.mf_id) + ")")
                  << "   " << color::addr(mf.date())
                  << "   " << color::addr(mf.start_time() + " – " + mf.end_time())
                  << color::hint("   (" + std::to_string(mf.filled()) + "/16 slots)") << "\n\n";
        for (int i = 0; i < 16; ++i) {
            const auto& sf = mf.by_slot[i];
            std::cout << "  [" << std::setw(2) << (i+1) << "]  ";
            if (!sf) {
                std::cout << color::hint("-- not received --") << "\n";
                continue;
            }
            std::cout << color::addr(sf->time_hms()) << "." << std::setw(3)
                      << std::setfill('0') << sf->ms << std::setfill(' ');
            if (sf->up_count >= 0)
                std::cout << "   " << color::hint(std::to_string(sf->up_count) + "/"
                          + std::to_string(sf->entry_total) + " entries up");
            std::cout << "\n";
        }
        std::cout << "  Select a slot number · [g] tier coverage matrix · [b]ack\n";
        auto in = trim(read_line("master frame> "));
        if (in.empty() || in == "b" || in == "B") return;
        if (in == "g" || in == "G") { mf_tier_matrix(mf); continue; }
        auto n = parse_int(in);
        if (n && *n >= 1 && *n <= 16 && mf.by_slot[*n - 1])
            subframe_loop(*mf.by_slot[*n - 1],
                          "Master Frame " + std::to_string(mf.index)
                          + "  › Slot " + std::to_string(*n), hex_mode);
        else
            std::cout << color::warn("  no such (received) slot\n");
    }
}

static void master_frame_browser() {
    if (!g_mfs) { std::cout << color::warn("  Master Frame store unavailable.\n"); return; }
    while (true) {
        auto masters = g_mfs->master_frames();
        std::cout << "\n" << color::header("Master Frame — Rate Group Commands Data") << "   ";
        if (g_mfs->connected())
            std::cout << color::ok("● receiving") ;
        else
            std::cout << color::err("● not connected to OBC rate-group stream (port +2)");
        std::cout << color::hint("   frames rx: " + std::to_string(g_mfs->frames_received())) << "\n";
        std::cout << color::hint("  16 subframes = 1 master frame · slots keyed by the "
                                 "frame's sub-frame count\n\n");

        if (masters.empty()) {
            std::cout << color::hint("  No master frames yet. The OBC transmits nothing until a\n"
                                     "  binary LUT is uplinked (xdos_gs [g] menu, [s]end).\n"
                                     "  Press [r] to refresh, [b] back.\n");
        } else {
            // Newest last; show most recent 20 for a clean screen.
            std::size_t start = masters.size() > 20 ? masters.size() - 20 : 0;
            for (std::size_t i = start; i < masters.size(); ++i) {
                const auto& mf = masters[i];
                std::string span = mf.date() + "  " + mf.start_time() + " – " + mf.end_time();
                std::string tag = mf.complete() ? "" : color::hint("  ("
                                   + std::to_string(mf.filled()) + "/16, filling)");
                std::cout << "  [" << std::setw(3) << mf.index << "]  "
                          << color::addr(span) << tag << "\n";
            }
        }
        std::cout << "  [r]efresh   [b]ack\n";
        auto in = trim(read_line("MF> "));
        if (in.empty()) continue;
        if (in == "b" || in == "B") return;
        if (in == "r" || in == "R") continue;
        auto n = parse_int(in);
        if (!n) { std::cout << color::warn("  type a master-frame number, r, or b\n"); continue; }
        const xdos::cli::MasterFrame* sel = nullptr;
        for (const auto& mf : masters) if (mf.index == *n) { sel = &mf; break; }
        if (sel) mf_show_master(*sel);
        else std::cout << color::warn("  no master frame with that number\n");
    }
}

// ─── entry point ─────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    xdos::net::Startup _net_init;        // no-op on POSIX, WSAStartup on Windows
    std::string obc_host = "192.168.1.111";  // bench default: real OBC
    int obc_port = 7000;
    int rg_port  = 0;                      // 0 = derive as obc_port + 2
    std::string twin_path;                 // "" = default search
    std::string mcc_listen;                // "[HOST:]PORT" — MCC connects in
    std::string mcc_connect;               // "HOST:PORT"   — we dial the MCC
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--no-color") color::set_use_color(false);
        else if (a == "--obc-host" && i+1 < argc) obc_host = argv[++i];
        else if (a == "--obc-port" && i+1 < argc) obc_port = std::atoi(argv[++i]);
        else if (a == "--rg-port"  && i+1 < argc) rg_port  = std::atoi(argv[++i]);
        else if (a == "--twin"     && i+1 < argc) twin_path = argv[++i];
        else if (a == "--mcc-listen"  && i+1 < argc) mcc_listen  = argv[++i];
        else if (a == "--mcc-connect" && i+1 < argc) mcc_connect = argv[++i];
        else {
            std::cout << "usage: xdos_tm_view [--obc-host H] [--obc-port P] "
                         "[--rg-port N] [--twin FILE] [--no-color]\n"
                         "  connects to the OBC rate-group stream on H:(P+2) "
                         "(or N if given)\n"
                         "  --twin: GS twin of the binary LUT "
                         "(default config/rate_group_lut_bin_gs.json)\n"
                         "  --mcc-listen  [HOST:]PORT  forward every subframe "
                         "payload to MCC clients that connect here\n"
                         "  --mcc-connect HOST:PORT    forward by dialling out "
                         "to an MCC that is listening\n"
                         "     wire: \"XMCC\" + mf_no u32 + subframe_no u16 + "
                         "len u32 + payload (all big-endian)\n";
            return (a == "--help" || a == "-h") ? 0 : 1;
        }
    }
    if (rg_port == 0) rg_port = obc_port + 2;

    std::cout << color::header(
        "╔══════════════════════════════════════════════════════════╗\n"
        "║        XDOS TM View — Master Frame telemetry             ║\n"
        "╚══════════════════════════════════════════════════════════╝") << "\n";

    mib::Mib mib;
    try { mib.load_dir(find_mib_dir()); }
    catch (const std::exception& e) {
        std::cerr << color::err(std::string("MIB load failed: ") + e.what()) << "\n";
        return 1;
    }
    g_mf_mib = &mib;

    bool tm_ok = false;
    for (const auto& p : { "mib/ccsds_tm.json", "../mib/ccsds_tm.json",
                           "../../mib/ccsds_tm.json" }) {
        if (fs::exists(p) && g_tm_codec.load(p)) { tm_ok = true; break; }
    }
    if (!tm_ok) {
        std::cerr << color::err("TM downlink profile (mib/ccsds_tm.json) not found — "
                                "cannot decode subframes.") << "\n";
        return 1;
    }

    xdos::cli::MasterFrameStore store(mib, g_tm_codec);
    std::string err;
    bool twin_ok = twin_path.empty() ? store.init(&err)
                                     : store.init(twin_path, &err);
    if (!twin_ok || !store.has_twin()) {
        std::cout << color::warn("  GS twin not loaded ("
                  + (err.empty() ? "unknown" : err) + ") — raw-hex mode.\n"
                  "  Build it with xdos_lut_csv or the xdos_gs [g] composer.\n");
    } else {
        char crc[16];
        std::snprintf(crc, sizeof(crc), "0x%04X", store.twin().crc16);
        std::cout << "  GS twin: " << color::ok(std::to_string(store.twin().entry_count())
                  + " entries") << color::hint("  CRC-16 " + std::string(crc)
                  + "  (compare with the OBC's 'applied' reply)") << "\n";
    }
    g_mfs = &store;
    store.set_decode_text_fn([](const TwinEntry& e, const Bytes& raw) {
        return entry_decode_text(e, raw, false);
    });
    {
        auto t = std::time(nullptr); auto lt = *std::localtime(&t);
        char fn[64];
        std::snprintf(fn, sizeof(fn), "RateGroup_TM_%02d_%02d_%02d_%02d.log",
                      lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min);
        store.enable_logging(fn);
        std::cout << "  Log file: " << fn << "\n";
    }
    // ── MCC forwarding ──────────────────────────────────────────────────────
    // Every received subframe's payload is pushed on, tagged with the master
    // frame and subframe numbers, so the MCC software can place it without
    // re-deriving position from the TM header.
    if (!mcc_listen.empty() || !mcc_connect.empty()) {
        auto split = [](const std::string& s, const std::string& def_host) {
            auto c = s.rfind(':');
            if (c == std::string::npos) return std::make_pair(def_host, std::atoi(s.c_str()));
            return std::make_pair(s.substr(0, c), std::atoi(s.c_str() + c + 1));
        };
        std::string merr;
        bool ok;
        if (!mcc_listen.empty()) {
            auto [h, prt] = split(mcc_listen, std::string("0.0.0.0"));
            ok = g_mcc.listen(h, prt, &merr);
        } else {
            auto [h, prt] = split(mcc_connect, std::string("127.0.0.1"));
            ok = g_mcc.connect(h, prt, &merr);
        }
        if (!ok) {
            std::cerr << color::err("  MCC forward: " + merr) << "\n";
            return 1;
        }
        std::cout << "  MCC forward: " << color::addr(g_mcc.endpoint())
                  << color::hint("   header = master-frame no + subframe no, "
                                 "then the subframe payload") << "\n";
        if (!merr.empty())
            std::cout << color::hint("               " + merr) << "\n";
        // slot is 0-based internally; the wire carries 1..16 as displayed.
        store.set_subframe_fn([](const xdos::cli::SubFrame& sf) {
            // Pass the frame's own CCS Time Code through unconverted.
            xdos::cli::SubframeTime ts;
            ts.year = sf.year; ts.month = sf.month; ts.day = sf.day;
            ts.hour = sf.hour; ts.minute = sf.minute; ts.second = sf.second;
            ts.ms = sf.ms;
            g_mcc.send(sf.mf_id, sf.slot + 1, ts, sf.tm_data);
        });
    }

    std::cout << "  Rate-group stream: " << color::addr(obc_host + ":" + std::to_string(rg_port))
              << color::hint("  (OBC routing port + 2)") << "\n";
    store.start(obc_host, rg_port);

    while (true) {
        std::cout << "\n";
        if (store.connected()) std::cout << "  " << color::ok("● receiving");
        else                   std::cout << "  " << color::warn("○ waiting for OBC stream");
        std::cout << color::hint("   subframes: " + std::to_string(store.frames_received()));
        if (g_mcc.active()) {
            std::cout << "   MCC " << (g_mcc.linked() ? color::ok("linked") : color::warn("no peer"));
            std::cout << color::hint(" (" + std::to_string(g_mcc.peers()) + " peer, sent "
                      + std::to_string(g_mcc.sent()) + ", dropped "
                      + std::to_string(g_mcc.dropped()) + ")");
        }
        std::cout << "\n";
        std::string c = trim(read_line("  [m] Master Frame browser   [t] TM Response window   [q] quit > "));
        if (!std::cin) {
            // stdin is closed — headless, e.g. run_stack.sh starts us with
            // < /dev/null.  Re-reading EOF here reprinted this menu millions
            // of times a second (~55 MB/s into logs/tm_view.log).  Stop
            // prompting; ingest, logging and the MCC forward keep running on
            // their own threads until we are signalled.
            std::cout << "\n  stdin closed — running headless until signalled\n";
            std::cout.flush();
            while (true) std::this_thread::sleep_for(std::chrono::hours(1));
        }
        if (c.empty()) continue;
        if      (c == "m" || c == "M") master_frame_browser();
        else if (c == "t" || c == "T") rate_group_tm_window();
        else if (c == "q" || c == "Q") break;
    }
    g_mfs = nullptr;
    store.stop();
    g_mcc.stop();
    std::cout << "TM View exiting.\n";
    return 0;
}
