// SPDX-License-Identifier: Apache-2.0
// xdos_obc — bridge between xdos_gs and the subsystem sims, with its own
// inspector CLI.  Routes frames by sniffing their native framing prefix.
//
//   $ ./xdos_obc [--port 10000] [--no-color]
//
// Defaults: listens on 127.0.0.1:10000 for the GS, forwards to
//           127.0.0.1:10010 (EPS), 10020 (ADCS), 10030 (GPS).

#include "bridge.hpp"
#include "ccsds_validator.hpp"
#include "ccsds_tm_validator.hpp"
#include "power_fsm.hpp"
#include "binary_rate_lut.hpp"
#include "rate_group_engine.hpp"
#include "codec/ccsds_codec.hpp"
#include "common/color.hpp"
#include "common/egse.hpp"
#include "common/flight_config.hpp"
#include "common/socket_compat.hpp"
#include "mib/mib.hpp"
#include "session/session.hpp"
#include "transport/tcp_transport.hpp"

#include <chrono>
#include <cstdio>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <thread>

using namespace xdos;
using namespace std::chrono;

static volatile std::sig_atomic_t g_stop = 0;
static void on_sigint(int) { g_stop = 1; }

static void print_banner(int port) {
    std::cout << color::header(
        "╔══════════════════════════════════════════════════════════╗") << "\n";
    std::cout << color::header(
        "║              XDOS OBC — Bridge + Inspector               ║") << "\n";
    std::cout << color::header(
        "╚══════════════════════════════════════════════════════════╝") << "\n";
    std::cout << "  GS-facing port : " << color::addr("127.0.0.1:" + std::to_string(port)) << "\n";
    std::cout << "  Downlinks      : EPS     " << color::addr("→ 10010") << "\n"
              << "                   ADCS    " << color::addr("→ 10020") << "\n"
              << "                   GPS     " << color::addr("→ 10030") << "\n"
              << "                   Payload " << color::addr("→ 10040") << "\n"
              << "                   XLINK-S " << color::addr("→ 10050") << "\n"
              << "                   UHF     " << color::addr("→ 10060") << "\n"
              << "                   SENSOR  " << color::addr("→ 10070") << "\n\n";
}

static void print_menu() {
    std::cout << "\n" << color::header("OBC Menu:") << "\n"
              << "  [1] Watch traffic\n"
              << "  [2] Show bridge stats\n"
              << "  [3] Subsystem downlink status\n"
              << "  [q] Quit\n"
              << color::hint("OBC> ");
    std::cout.flush();
}

static void cmd_watch_traffic(obc::Bridge& br) {
    std::cout << "\n" << color::header("=== Live traffic (Enter to stop) ===") << "\n";
    size_t last_seen = 0;
    bool stop = false;
    std::thread reader([&stop](){
        std::string line; std::getline(std::cin, line); stop = true;
    });
    while (!stop) {
        auto log = br.recent_log(1000);
        for (; last_seen < log.size(); ++last_seen) {
            const auto& e = log[last_seen];
            const std::string& d = e.direction;
            std::string colored;
            if (d.find("EPS")  != std::string::npos) colored = color::wrap(color::ansi::YELLOW, d);
            else if (d.find("ADCS") != std::string::npos) colored = color::wrap(color::ansi::GREEN,  d);
            else if (d.find("GPS")  != std::string::npos) colored = color::wrap(color::ansi::CYAN,   d);
            else colored = color::err(d);
            std::cout << "  " << e.when << "  " << colored
                      << "  " << e.size << "B  " << e.preview << "\n";
        }
        std::this_thread::sleep_for(milliseconds(150));
    }
    reader.join();
    std::cout << color::hint("(stopped watching)") << "\n";
}

static void cmd_show_stats(obc::Bridge& br) {
    auto s = br.stats();
    std::cout << "\n" << color::header("=== Bridge Stats ===") << "\n";
    std::cout << "  frames in     : " << s.frames_in   << "\n"
              << "  frames out    : " << s.frames_out  << "\n"
              << "  routed → ADCS    : " << s.routed[(int)Subsystem::ADCS]    << "\n"
              << "  routed → EPS     : " << s.routed[(int)Subsystem::EPS]     << "\n"
              << "  routed → GPS     : " << s.routed[(int)Subsystem::GPS]     << "\n"
              << "  routed → Payload : " << s.routed[(int)Subsystem::Payload] << "\n"
              << "  routed → XLINK-S : " << s.routed[(int)Subsystem::XLINK]   << "\n"
              << "  routed → UHF     : " << s.routed[(int)Subsystem::UHF]     << "\n"
              << "  routed → SENSOR  : " << s.routed[(int)Subsystem::SENSOR]  << "\n"
              << "  unrecognised  : " << s.unrecognised << "\n";
}

static void cmd_show_downlinks(obc::Bridge& br) {
    auto log = br.recent_log(5);
    std::cout << "\n" << color::header("=== Recent traffic (last 5) ===") << "\n";
    if (log.empty()) std::cout << "  " << color::hint("(no traffic yet)") << "\n";
    for (const auto& e : log)
        std::cout << "  " << e.when << "  " << e.direction
                  << "  " << e.size << "B  " << e.preview << "\n";
}

int main(int argc, char** argv) {
    xdos::net::Startup _net_init;        // no-op on POSIX, WSAStartup on Windows
    int gs_port = 10000;
    int eps_port = 10010, adcs_port = 10020, gps_port = 10030, payload_port = 10040;
    int xlink_port = 10050, uhf_port = 10060, sensor_port = 10070;
    bool validate_tc = false, validate_tm = false;
    bool instant_on = false;             // legacy: bridge live immediately
    float time_scale = 1.0f;             // flight default: real 30 s boot
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--no-color")  color::set_use_color(false);
        else if (a == "--port" && i+1 < argc) gs_port = std::atoi(argv[++i]);
        else if (a == "--eps-port"  && i+1 < argc) eps_port  = std::atoi(argv[++i]);
        else if (a == "--adcs-port" && i+1 < argc) adcs_port = std::atoi(argv[++i]);
        else if (a == "--gps-port"  && i+1 < argc) gps_port  = std::atoi(argv[++i]);
        else if (a == "--payload-port" && i+1 < argc) payload_port = std::atoi(argv[++i]);
        else if (a == "--xlink-port"   && i+1 < argc) xlink_port   = std::atoi(argv[++i]);
        else if (a == "--uhf-port"     && i+1 < argc) uhf_port     = std::atoi(argv[++i]);
        else if (a == "--sensor-port"  && i+1 < argc) sensor_port  = std::atoi(argv[++i]);
        else if (a == "--validate")    { validate_tc = true; validate_tm = true; }
        else if (a == "--validate-tc") validate_tc = true;
        else if (a == "--validate-tm") validate_tm = true;
        else if (a == "--instant-on") instant_on = true;
        else if (a == "--time-scale" && i+1 < argc)
            time_scale = std::atof(argv[++i]);
    }

    std::signal(SIGINT,  on_sigint);
    std::signal(SIGTERM, on_sigint);

    obc::Bridge::EndpointMap endpoints = {
        { Subsystem::EPS,     { "127.0.0.1", eps_port      } },
        { Subsystem::ADCS,    { "127.0.0.1", adcs_port     } },
        { Subsystem::GPS,     { "127.0.0.1", gps_port      } },
        { Subsystem::Payload, { "127.0.0.1", payload_port  } },
        { Subsystem::XLINK,   { "127.0.0.1", xlink_port    } },
        { Subsystem::UHF,     { "127.0.0.1", uhf_port      } },
        { Subsystem::SENSOR,  { "127.0.0.1", sensor_port   } },
    };
    obc::Bridge br(endpoints);

    // Try to load the CCSDS profile so the bridge can detect and unwrap
    // CCSDS frames from the GS.  If the profile is missing, the bridge
    // still works in native pass-through mode.
    ccsds::CcsdsCodec ccsds_codec;
    bool ccsds_loaded = false;
    try {
        for (const auto& p : { "mib/ccsds_tc.json",
                               "../mib/ccsds_tc.json",
                               "../../mib/ccsds_tc.json" }) {
            if (std::filesystem::exists(p)) {
                ccsds_codec.load(p);
                ccsds_loaded = true;
                break;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << color::warn(std::string("[OBC] CCSDS profile load failed: ")
                                 + e.what()) << "\n";
    }
    if (ccsds_loaded) br.set_ccsds_codec(&ccsds_codec);

    // TM downlink codec (fixed 1432-byte frame, SOF FCA1).  References the TC
    // profile for shared SCID/APID, so load it from the same mib directory.
    ccsds::CcsdsTmCodec tm_codec;
    bool tm_loaded = false;
    try {
        for (const auto& p : { "mib/ccsds_tm.json", "../mib/ccsds_tm.json",
                               "../../mib/ccsds_tm.json" }) {
            if (std::filesystem::exists(p)) { tm_loaded = tm_codec.load(p); break; }
        }
    } catch (const std::exception& e) {
        std::cerr << color::warn(std::string("[OBC] TM profile load failed: ")
                                 + e.what()) << "\n";
    }
    if (tm_loaded) {
        br.set_tm_codec(&tm_codec);
        br.use_tm_frames(true);
    }

    // Install validation observers.  TC frames are validated as they arrive
    // from the GS; TM frames as the OBC emits them.  --validate turns on both;
    // --validate-tc / --validate-tm select one direction.
    if (validate_tc && ccsds_loaded) {
        br.set_frame_observer([&ccsds_codec](const Bytes& wire) {
            // Only validate CCSDS (TC) frames — native frames are pass-through.
            if (!ccsds_codec.looks_like_ccsds(wire)) return;
            obc::print_validation_report(std::cout,
                obc::validate_ccsds_frame(ccsds_codec, wire));
            std::cout.flush();
        });
    }
    if (validate_tm && tm_loaded) {
        br.set_tm_observer([&tm_codec](const Bytes& wire, Subsystem) {
            obc::print_tm_validation_report(std::cout,
                obc::validate_tm_frame(tm_codec, wire));
            std::cout.flush();
        });
    }

    if (!br.start("127.0.0.1", gs_port)) {
        std::cerr << color::err("OBC: failed to bind GS-facing port "
                                + std::to_string(gs_port)) << "\n";
        return 1;
    }

    // ── Flight power model: OBC is powered by EPS channel O1 ───────────────
    // Until the EPS kill switch goes ON (+ boot time) the bridge drops every
    // GS frame; EGSE status frames are always answered so the ground station
    // can watch the boot.  Once ON, the FSM commands the default subsystems
    // through REAL EPS TCs, staggered, then enables bridging.
    std::unique_ptr<mib::Mib>        fmib;
    std::unique_ptr<session::Session> eps_sess;
    std::unique_ptr<obc::PowerFsm>   fsm;
    std::thread                      fsm_thread;
    // Forward reference so the power FSM's enable callback can gate the
    // rate-group engine: polling starts only when the OBC reaches ON, and
    // stops if power is lost.  Assigned once the engine is constructed below.
    obc::RateGroupEngine* rg_ptr = nullptr;

    if (!instant_on) {
        br.set_enabled(false);
        fmib = std::make_unique<mib::Mib>();
        for (auto p : { "mib", "../mib", "../../mib" })
            if (std::filesystem::is_directory(p)) { fmib->load_dir(p); break; }
        const FlightConfig fc = FlightConfig::load();

        obc::PowerFsm::Config pcfg;
        pcfg.boot_s           = static_cast<float>(fc.obc_boot_s);
        pcfg.stagger_s        = static_cast<float>(fc.stagger_s);
        pcfg.restore_period_s = static_cast<float>(fc.restore_period_s);
        pcfg.time_scale       = time_scale;
        for (const auto& name : fc.default_on_sequence) {
            std::uint16_t bits = fc.entry_bits(name);
            if (bits) pcfg.sequence.emplace_back(name, bits);
        }

        eps_sess = std::make_unique<session::Session>(*fmib);
        eps_sess->configure(Subsystem::EPS, { "127.0.0.1", eps_port });

        auto query = [eps_port]() -> std::optional<egse::EpsStatus> {
            transport::TcpClient c;
            if (!c.connect("127.0.0.1", eps_port, 250)) return std::nullopt;
            if (!c.send(egse::make_req(egse::CMD_EPS_STATUS))) return std::nullopt;
            return egse::parse_eps_status(c.recv(64, 400));
        };
        auto send_mask = [&eps_sess](std::uint16_t m) -> bool {
            if (!eps_sess->connected(Subsystem::EPS) &&
                !eps_sess->connect(Subsystem::EPS)) return false;
            nlohmann::json vals;
            vals["CH1 to CH8 Enable/Disable"]  = m & 0xFF;
            vals["CH9 to CH16 Enable/Disable"] = (m >> 8) & 0xFF;
            // node = nullopt → send_tc uses the message's own node (PDU1)
            auto r = eps_sess->send_tc(Subsystem::EPS, "Set_PDU_CH_State",
                                       vals, std::nullopt);
            if (!r.ok)
                std::cout << color::warn("[OBC] EPS TC error: " + r.error) << "\n";
            return r.ok;
        };
        auto enable = [&br, &rg_ptr](bool on) {
            br.set_enabled(on);
            // OBC just reached ON → begin autonomous rate-group polling.
            // Power lost → stop polling.  This is the fix for "who polls
            // before the OBC is up": nobody — the engine is gated here.
            if (rg_ptr) { if (on) rg_ptr->start(); else rg_ptr->stop(); }
        };
        auto logfn  = [](const std::string& s) {
            std::cout << (s.find("LOST") != std::string::npos
                          ? color::err(s) : color::ok(s)) << "\n" << std::flush;
        };
        fsm = std::make_unique<obc::PowerFsm>(pcfg, query, send_mask, enable, logfn);

        br.set_egse_handler([&fsm](const Bytes& b) -> Bytes {
            if (egse::parse_cmd(b) == egse::CMD_OBC_STATUS)
                return egse::make_obc_status_reply(fsm->status());
            return {};
        });
        fsm_thread = std::thread([&fsm] {
            while (!g_stop) {
                fsm->tick(steady_clock::now());
                std::this_thread::sleep_for(milliseconds(500));
            }
        });
    }

    // Dedicated EGSE status listener, ISOLATED from the TC/TM routing port.
    // The GS status bar polls OBC state a few times a second; routing that
    // over the same port as telemetry caused reply contention on the shared
    // connection.  A separate port keeps housekeeping polls off the TM path.
    const int egse_port = gs_port + 1;               // 10001 by default
    transport::TcpServer egse_srv;
    if (!instant_on) {
        egse_srv.start("127.0.0.1", egse_port,
            [&fsm](int cfd, const std::string&) {
                while (!g_stop) {
                    auto req = transport::fd_recv_blocking(cfd, 256, 200);
                    if (req.empty()) return;
                    if (egse::parse_cmd(req) == egse::CMD_OBC_STATUS && fsm)
                        transport::fd_send(cfd,
                            egse::make_obc_status_reply(fsm->status()));
                }
            });
    }

    // Dedicated rate-group housekeeping stream.  The OBC autonomously polls
    // the 13 rate-group commands and pushes a 1432-byte frame once per second
    // to any GS connected on this port (routing port + 2).  Kept off the main
    // routing port so the unprompted 1 Hz stream never mixes with TC/TM.
    const int rg_port = gs_port + 2;                  // 10002 by default
    transport::TcpServer rg_srv;
    std::mutex rg_clients_mu;
    std::vector<int> rg_clients;
    std::unique_ptr<obc::RateGroupEngine> rg_engine;
    if (tm_loaded) {
        // OBC has NO subsystem Database.  The engine executes the BINARY
        // rate-group LUT uplinked from the ground; until one is active the
        // OBC polls nothing and transmits nothing on this port.

        rg_srv.start("127.0.0.1", rg_port,
            [&rg_clients_mu, &rg_clients](int cfd, const std::string&) {
                { std::lock_guard<std::mutex> g(rg_clients_mu); rg_clients.push_back(cfd); }
                while (!g_stop) {
                    auto probe = transport::fd_recv(cfd, 16, 500);
                    (void)probe;   // GS does not send on this channel
                }
            });

        obc::RateGroupEngine::Config rgcfg;
        rgcfg.time_scale = time_scale;
        auto sink = [&rg_clients_mu, &rg_clients](const Bytes& frame) -> bool {
            std::lock_guard<std::mutex> g(rg_clients_mu);
            bool any = false;
            for (auto it = rg_clients.begin(); it != rg_clients.end();) {
                if (transport::fd_send(*it, frame)) { any = true; ++it; }
                else it = rg_clients.erase(it);
            }
            return any;
        };
        rg_engine = std::make_unique<obc::RateGroupEngine>(tm_codec, rgcfg, sink);
        rg_ptr = rg_engine.get();

        // Auto-reload: a previously uplinked LUT persists in obc_lut_store.bin
        // (written by the uplink listener).  If it is present and passes CRC
        // the engine resumes it; otherwise the OBC stays silent until the
        // first binary uplink.
        bool restored = false;
        for (const auto& p : { "obc_lut_store.bin",
                               "../obc_lut_store.bin",
                               "../../obc_lut_store.bin" }) {
            if (!std::filesystem::exists(p)) continue;
            obc::BinaryRateLut lut;
            std::string err;
            if (obc::BinaryRateLut::load_file(p, lut, &err)
                && rg_engine->swap_lut(lut, &err)) {
                char msg[128];
                std::snprintf(msg, sizeof(msg),
                    "[OBC] binary rate-group LUT restored from %s — "
                    "%d entries, crc 0x%04X.", p, lut.entry_count(), lut.crc);
                std::cout << color::hint(msg) << "\n";
                restored = true;
            } else {
                std::cerr << color::warn(std::string("[OBC] stored LUT ") + p
                    + " rejected: " + err) << "\n";
            }
            break;
        }
        if (!restored)
            std::cout << color::hint("[OBC] no valid stored LUT — rate group "
                "idle until a binary LUT is uplinked.") << "\n";
        else {
            // Pacing is derived from the table: T_sf spread over the busiest
            // window's polls.  It fits by construction, so this is a plain
            // statement of the schedule, not a warning.
            const int polls = std::max(1, rg_engine->max_polls_per_window());
            std::cout << color::hint("[OBC] rate-group pacing: "
                + std::to_string(polls) + " polls in a "
                + std::to_string(rg_engine->t_sf_ms()) + " ms window → "
                + std::to_string(rg_engine->t_sf_ms() / polls)
                + " ms gap (T_sf / entries)") << "\n";
        }

        if (instant_on) {
            // Legacy mode: no power FSM, bridge is live immediately, so
            // start the engine right away (idle until a LUT is active).
            rg_engine->start();
        } else {
            // Flight mode: the power FSM's enable() callback starts the
            // engine when the OBC finishes booting + sequencing (state ON).
            std::cout << color::hint("[OBC] rate-group engine armed — "
                "polling begins once the OBC is powered and booted.") << "\n";
        }
    }

    // ── Rate-group LUT UPLINK listener ──────────────────────────────────────
    // A new LUT edited on the ground is sent here (routing port + 3).  The OBC
    // validates it and hot-swaps the running table — no restart, no Database.
    // Wire protocol: 4-byte magic "XLUT" + 4-byte big-endian length + JSON.
    const int uplink_port = gs_port + 3;              // 10003 by default
    transport::TcpServer uplink_srv;
    if (rg_engine) {
        obc::RateGroupEngine* rgp = rg_engine.get();
        uplink_srv.start("127.0.0.1", uplink_port,
            [rgp](int cfd, const std::string&) {
                // Read header: magic(4) + length(4, BE).
                Bytes hdr;
                while (hdr.size() < 8 && !g_stop) {
                    auto c = transport::fd_recv(cfd, 8 - hdr.size(), 1000);
                    if (c.empty()) return;
                    hdr.insert(hdr.end(), c.begin(), c.end());
                }
                if (hdr.size() < 8 ||
                    hdr[0] != 'X' || hdr[1] != 'L' || hdr[2] != 'U' || hdr[3] != 'T') {
                    transport::fd_send(cfd, Bytes{'E','R','R','!'});
                    return;
                }
                std::uint32_t len = (std::uint32_t(hdr[4]) << 24) |
                                    (std::uint32_t(hdr[5]) << 16) |
                                    (std::uint32_t(hdr[6]) << 8)  |
                                     std::uint32_t(hdr[7]);
                if (len == 0 || len > 1u * 1024 * 1024) {
                    transport::fd_send(cfd, Bytes{'E','R','R','!'});
                    return;
                }
                std::string body;
                body.reserve(len);
                while (body.size() < len && !g_stop) {
                    auto c = transport::fd_recv(cfd, len - body.size(), 2000);
                    if (c.empty()) break;
                    body.append(reinterpret_cast<const char*>(c.data()), c.size());
                }
                // ── BINARY LUT (body starts with the XLUT file magic):
                // full structural + CRC validation, then persist AND apply —
                // the engine hot-swaps to the new table and starts polling.
                if (body.rfind("XLUT", 0) == 0) {
                    Bytes image(body.begin(), body.end());
                    obc::BinaryRateLut lut;
                    std::string err;
                    if (obc::BinaryRateLut::parse(image, lut, &err)
                        && rgp->swap_lut(lut, &err)) {
                        std::ofstream bf("obc_lut_store.bin", std::ios::binary);
                        bf.write(body.data(), (std::streamsize)body.size());
                        char msg[96];
                        std::snprintf(msg, sizeof(msg),
                            "OK bin %d entries crc 0x%04X applied",
                            lut.entry_count(), lut.crc);
                        Bytes reply(msg, msg + std::strlen(msg));
                        transport::fd_send(cfd, reply);
                        std::cout << color::ok(std::string(
                            "[OBC] BINARY LUT uplink applied — ") + msg
                            + (bf ? "" : " (WARNING: persist failed)")) << "\n";
                    } else {
                        std::string msg = "ERR " + err;
                        Bytes reply(msg.begin(), msg.end());
                        transport::fd_send(cfd, reply);
                        std::cout << color::warn("[OBC] BINARY LUT uplink rejected: "
                            + err) << "\n";
                    }
                    return;
                }
                // Legacy JSON LUTs are no longer executable — the rate group
                // runs exclusively from the binary LUT.
                {
                    std::string msg = "ERR json LUT retired — uplink binary";
                    Bytes reply(msg.begin(), msg.end());
                    transport::fd_send(cfd, reply);
                    std::cout << color::warn(
                        "[OBC] rate-group LUT UPLINK rejected: json retired") << "\n";
                }
            });
    }

    print_banner(gs_port);
    std::cout << color::ok("[OBC] Bridge running.") << "\n";
    if (!instant_on) {
        char pb[160];
        std::snprintf(pb, sizeof(pb),
            "[OBC] flight power model: UNPOWERED — waiting for EPS kill switch "
            "(boot %ds, stagger %ds%s)",
            FlightConfig::load().obc_boot_s, FlightConfig::load().stagger_s,
            time_scale != 1.0f ? ", scaled" : "");
        std::cout << color::warn(pb) << "\n";
    } else {
        std::cout << color::warn("[OBC] --instant-on: legacy mode, bridge live immediately.") << "\n";
    }
    if (ccsds_loaded) {
        std::cout << color::ok("[OBC] CCSDS codec attached — frames with SOF 0x1ACF will be unwrapped.") << "\n";
    }
    if (tm_loaded) {
        std::cout << color::ok("[OBC] TM downlink codec attached — responses wrapped as 1432-byte frames (SOF FCA1).") << "\n";
    } else {
        std::cout << color::warn("[OBC] CCSDS codec NOT loaded — native pass-through only.") << "\n";
    }
    if (validate_tc && ccsds_loaded)
        std::cout << color::ok("[OBC] TC validation ON — each incoming CCSDS frame will be field-checked.") << "\n";
    if (validate_tm && tm_loaded)
        std::cout << color::ok("[OBC] TM validation ON — each emitted TM downlink frame will be field-checked.") << "\n";
    if (validate_tc && !ccsds_loaded)
        std::cout << color::warn("[OBC] --validate-tc requested but no CCSDS codec loaded; nothing to validate.") << "\n";
    if (validate_tm && !tm_loaded)
        std::cout << color::warn("[OBC] --validate-tm requested but no TM codec loaded; nothing to validate.") << "\n";

    // Daemon mode: if stdin isn't a terminal (backgrounded with `&`, piped,
    // or redirected to /dev/null), skip the interactive menu and just keep
    // the bridge alive until killed.  Otherwise SIGTTIN suspends the whole
    // process the moment we try to read stdin.
    if (!xdos::net::isatty_stdin()) {
        std::cout << color::hint("[OBC] No TTY on stdin — running as bridge daemon.\n"
                                 "      Signal SIGINT / SIGTERM to stop.") << "\n";
        while (!g_stop) std::this_thread::sleep_for(milliseconds(200));
        std::cout << "\nOBC daemon shutting down...\n";
        g_stop = 1;
        if (rg_engine) rg_engine->stop();
        if (fsm_thread.joinable()) fsm_thread.join();
        br.stop();
        return 0;
    }

    while (!g_stop) {
        print_menu();
        std::string line;
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;
        char c = line[0];
        if      (c == '1') cmd_watch_traffic(br);
        else if (c == '2') cmd_show_stats(br);
        else if (c == '3') cmd_show_downlinks(br);
        else if (c == 'q' || c == 'Q') break;
        else std::cout << color::warn("  (unknown: try 1/2/3/q)") << "\n";
    }

    std::cout << "\nOBC shutting down...\n";
    g_stop = 1;
    if (rg_engine) rg_engine->stop();
    if (fsm_thread.joinable()) fsm_thread.join();
    br.stop();
    return 0;
}
