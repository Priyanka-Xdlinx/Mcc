// SPDX-License-Identifier: Apache-2.0
// xdos_eps_sim — standalone EPS bus simulator with live power model.
//
//   $ ./xdos_eps_sim [--port 10010] [--no-color] [--quiet]

#include "engines/eps_engine.hpp"
#include "handlers/engine_responder.hpp"
#include "responders/responder.hpp"
#include "transport/tcp_transport.hpp"
#include "common/socket_compat.hpp"
#include "common/egse.hpp"
#include "common/flight_config.hpp"
#include "mib/mib.hpp"
#include "common/color.hpp"

#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>

using namespace xdos;
using namespace std::chrono;

static volatile std::sig_atomic_t g_stop = 0;
static void on_sigint(int) { g_stop = 1; }

static std::filesystem::path find_mib_dir() {
    for (auto p : { "mib", "../mib", "../../mib" })
        if (std::filesystem::is_directory(p)) return p;
    return "mib";
}

int main(int argc, char** argv) {
    xdos::net::Startup _net_init;        // no-op on POSIX, WSAStartup on Windows
    int   port = 10010;
    bool  quiet = false;
    bool  instant_on = false;            // legacy behaviour: bus live, 9 ch on
    float time_scale = 1.0f;             // flight default: REAL orbit time
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--no-color") color::set_use_color(false);
        else if (a == "--quiet")    quiet = true;
        else if (a == "--instant-on") instant_on = true;
        else if (a == "--port" && i+1 < argc) port = std::atoi(argv[++i]);
        else if (a == "--time-scale" && i+1 < argc)
            time_scale = std::atof(argv[++i]);
    }
    std::signal(SIGINT,  on_sigint);
    std::signal(SIGTERM, on_sigint);

    mib::Mib mib; mib.load_dir(find_mib_dir());
    const FlightConfig fc = FlightConfig::load();

    const std::uint16_t obc_bits = FlightConfig::bits(fc.obc_channels);

    sim::EpsEngineConfig ecfg;
    if (!instant_on) {
        ecfg.flight_power      = true;
        ecfg.chemistry         = 1;                       // Li-ion 3S
        ecfg.time_scale        = time_scale;
        ecfg.orbit_period_sec  = fc.sunlit_s + fc.eclipse_s;
        ecfg.sun_fraction      = fc.sunlit_s / ecfg.orbit_period_sec;
        ecfg.thr_full = fc.thr_full;   ecfg.thr_normal   = fc.thr_normal;
        ecfg.thr_safe = fc.thr_safe;   ecfg.thr_critical = fc.thr_critical;
        ecfg.hysteresis_v      = fc.hysteresis_v;
        ecfg.shed_safe_bits     = FlightConfig::bits(fc.shed_safe_ch);
        ecfg.shed_critical_bits = FlightConfig::bits(fc.shed_critical_ch);
        ecfg.keep_below_bits    = FlightConfig::bits(fc.keep_below_ch);
    }

    sim::EpsEngine eps(ecfg);
    if (!instant_on) {
        eps.set_kill(false);                              // bench start: bus dead
        std::lock_guard<std::mutex> g(eps.pdu().m);
        eps.pdu().channel_mask = obc_bits;                // OBC energizes on kill
        eps.pdu().current_mode = 3;
    }
    eps.start();

    auto er = std::make_shared<sim::EngineResponder>(mib, Subsystem::EPS);
    er->set_eps_engine(&eps);

    sim::Responder resp(mib, Subsystem::EPS);
    resp.attach_engine_responder(er);

    transport::TcpServer srv;
    auto* r = &resp;
    auto* e = &eps;
    bool ok = srv.start("127.0.0.1", port,
        [r, e, quiet](int fd, const std::string& peer) {
            while (true) {
                auto first = transport::fd_recv_blocking(fd, 8192);
                if (first.empty()) break;
                auto until = steady_clock::now() + milliseconds(30);
                while (steady_clock::now() < until) {
                    auto more = transport::fd_recv(fd, 8192, 5);
                    if (more.empty()) break;
                    first.insert(first.end(), more.begin(), more.end());
                }

                // ── EGSE umbilical (bench harness, outside the Database) ──
                if (egse::is_egse(first)) {
                    auto cmd = egse::parse_cmd(first);
                    if (cmd == egse::CMD_KILL_SET) {
                        bool on = egse::parse_kill_set(first).value_or(false);
                        bool was = e->kill();
                        e->set_kill(on);
                        if (on != was)
                            std::cout << (on
                                ? color::ok("  [EPS] KILL SWITCH ON — battery bus energized\n")
                                : color::err("  [EPS] KILL SWITCH OFF — all outputs dead\n"));
                        transport::fd_send(fd, egse::make_kill_reply(true));
                    } else if (cmd == egse::CMD_EPS_STATUS) {
                        auto st = e->snapshot();
                        egse::EpsStatus s;
                        s.kill  = e->kill();
                        s.mode  = st.eps_mode;
                        s.phase = (st.phase == sim::OrbitPhase::Eclipse) ? 1 : 0;
                        s.phase_remaining_s =
                            static_cast<std::uint16_t>(std::min(st.phase_remaining_s, 65535.0f));
                        s.vbatt_mv  = static_cast<std::uint16_t>(st.batt_voltage * 1000.0f);
                        s.soc_pct   = static_cast<std::uint8_t>(st.batt_soc * 100.0f);
                        s.chan_mask = e->effective_mask();
                        s.shed_mask = e->shed_mask();
                        transport::fd_send(fd, egse::make_eps_status_reply(s));
                    }
                    continue;
                }

                auto out = r->handle(first, peer, /*verbose*/!quiet);
                if (!out.empty()) transport::fd_send(fd, out);
            }
        });
    if (!ok) { std::cerr << "EPS sim: bind failed on port " << port << "\n"; return 1; }

    std::cout << color::header("=== XDOS EPS Simulator ===") << "\n";
    std::cout << "  bus    : " << color::ok("RS485 over TCP") << "\n";
    std::cout << "  listen : " << color::addr("127.0.0.1:" + std::to_string(port)) << "\n";
    if (instant_on) {
        std::cout << "  power  : " << color::warn("instant-on (legacy)") << "\n";
        std::cout << "  engine : orbit 90 min, 60% sun, 40 W panel, 240 Wh batt\n";
    } else {
        char ob[96];
        std::snprintf(ob, sizeof(ob),
                      "  engine : orbit %.0f min sunlit / %.0f min eclipse, "
                      "Li-ion 3S, time ×%.0f\n",
                      fc.sunlit_s / 60.0f, fc.eclipse_s / 60.0f,
                      static_cast<double>(time_scale));
        std::cout << "  power  : " << color::err("KILL SWITCH OFF")
                  << " — flight model armed (EGSE to enable)\n" << ob;
        std::cout << "  modes  : Full>=" << fc.thr_full << "V  Normal>=" << fc.thr_normal
                  << "V  Safe>=" << fc.thr_safe << "V  Critical>=" << fc.thr_critical
                  << "V (load shed on drop)\n";
    }
    std::cout << "  nodes  : "
              << "PDU1=1  BMS1=9  BMS2=10  BMS3=11  MPPT1=12  (OBC1=201, OBC2=202)\n";

    // Channel map from mib/eps.json (sourced from NL Spacehawk Power Mapping Sheet)
    const auto& chans = mib.channels(Subsystem::EPS);
    if (!chans.empty() && !quiet) {
        std::cout << "  channels:\n";
        for (const auto& c : chans) {
            if (!c.wired()) continue;
            char buf[160];
            std::string powerstr;
            if (c.peak_power_w) {
                char pb[32];
                std::snprintf(pb, sizeof(pb), "peak %5.1fW",
                              static_cast<double>(*c.peak_power_w));
                powerstr = pb;
            }
            std::snprintf(buf, sizeof(buf), "    CH%-2d  %-14s  %-12s  %s\n",
                          c.id,
                          c.subsystem.c_str(),
                          c.voltage ? c.voltage->c_str() : "",
                          powerstr.c_str());
            std::cout << buf;
        }
    }
    std::cout << "  Ctrl-C to stop.\n\n";

    // Periodic engine heartbeat to stdout (every ~5 s real-time)
    auto next = steady_clock::now() + seconds(5);
    while (!g_stop) {
        std::this_thread::sleep_for(milliseconds(200));
        if (!quiet && steady_clock::now() >= next) {
            auto s = eps.snapshot();
            const char* ph = (s.phase == sim::OrbitPhase::Sun) ? "SUN " : "ECL ";
            std::cout << "  [SIM] t=" << (int)s.sim_elapsed_sec
                      << "s  orbit=" << (int)s.orbit_angle_deg << "°  " << ph
                      << "  P_solar=" << (int)s.solar_power_w
                      << "W  load=" << s.load_current_a
                      << "A  SOC=" << (int)(s.batt_soc*100) << "%"
                      << "  Vbatt=" << s.batt_voltage << "V\n";
            next = steady_clock::now() + seconds(5);
        }
    }
    std::cout << "\nEPS sim shutting down...\n";
    eps.stop();
    srv.stop();
    return 0;
}
