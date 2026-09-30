// SPDX-License-Identifier: Apache-2.0
// xdos_gps_sim — standalone GPS receiver simulator with Keplerian orbit.

#include "engines/gps_engine.hpp"
#include "handlers/engine_responder.hpp"
#include "responders/responder.hpp"
#include "transport/tcp_transport.hpp"
#include "common/socket_compat.hpp"
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
    int  port  = 10030;
    bool quiet = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--no-color") color::set_use_color(false);
        else if (a == "--quiet")    quiet = true;
        else if (a == "--port" && i+1 < argc) port = std::atoi(argv[++i]);
    }
    std::signal(SIGINT,  on_sigint);
    std::signal(SIGTERM, on_sigint);

    mib::Mib mib; mib.load_dir(find_mib_dir());

    sim::GpsEngine gps;
    gps.start();

    auto er = std::make_shared<sim::EngineResponder>(mib, Subsystem::GPS);
    er->set_gps_engine(&gps);

    sim::Responder resp(mib, Subsystem::GPS);
    resp.attach_engine_responder(er);

    transport::TcpServer srv;
    auto* r = &resp;
    bool ok = srv.start("127.0.0.1", port,
        [r, quiet](int fd, const std::string& peer) {
            while (true) {
                auto first = transport::fd_recv_blocking(fd, 8192);
                if (first.empty()) break;
                auto until = steady_clock::now() + milliseconds(30);
                while (steady_clock::now() < until) {
                    auto more = transport::fd_recv(fd, 8192, 5);
                    if (more.empty()) break;
                    first.insert(first.end(), more.begin(), more.end());
                }
                auto out = r->handle(first, peer, /*verbose*/!quiet);
                if (!out.empty()) transport::fd_send(fd, out);
            }
        });
    if (!ok) { std::cerr << "GPS sim: bind failed on port " << port << "\n"; return 1; }

    std::cout << color::header("=== XDOS GPS Simulator ===") << "\n";
    std::cout << "  bus    : " << color::ok("NovAtel OEM7 binary over TCP") << "\n";
    std::cout << "  listen : " << color::addr("127.0.0.1:" + std::to_string(port)) << "\n";
    std::cout << "  engine : 500 km LEO, inc 51.6°, ~7.6 km/s ground speed\n";
    std::cout << "  Ctrl-C to stop.\n\n";

    auto next = steady_clock::now() + seconds(5);
    while (!g_stop) {
        std::this_thread::sleep_for(milliseconds(200));
        if (!quiet && steady_clock::now() >= next) {
            auto s = gps.snapshot();
            std::cout << "  [SIM] t=" << (int)s.sim_elapsed_sec
                      << "s  lat=" << s.lat_deg << "°"
                      << "  lon=" << s.lon_deg << "°"
                      << "  alt=" << (int)(s.alt_m/1000) << "km"
                      << "  course=" << (int)s.course_deg << "°\n";
            next = steady_clock::now() + seconds(5);
        }
    }
    std::cout << "\nGPS sim shutting down...\n";
    gps.stop();
    srv.stop();
    return 0;
}
