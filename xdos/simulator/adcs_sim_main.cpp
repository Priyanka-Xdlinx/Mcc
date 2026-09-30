// SPDX-License-Identifier: Apache-2.0
// xdos_adcs_sim — standalone ADCS bus simulator with attitude dynamics.

#define _USE_MATH_DEFINES   // unlock M_PI etc. on MSVC/MinGW <cmath>
#include "engines/adcs_engine.hpp"
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
    int  port  = 10020;
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

    sim::AdcsEngine adcs;
    adcs.start();

    auto er = std::make_shared<sim::EngineResponder>(mib, Subsystem::ADCS);
    er->set_adcs_engine(&adcs);

    sim::Responder resp(mib, Subsystem::ADCS);
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
    if (!ok) { std::cerr << "ADCS sim: bind failed on port " << port << "\n"; return 1; }

    std::cout << color::header("=== XDOS ADCS Simulator ===") << "\n";
    std::cout << "  bus    : " << color::ok("CubeSpace UART over TCP") << "\n";
    std::cout << "  listen : " << color::addr("127.0.0.1:" + std::to_string(port)) << "\n";
    std::cout << "  engine : J=[0.05,0.05,0.08] kg·m²  PD: Kp=0.01 Kd=0.05\n";
    std::cout << "  Ctrl-C to stop.\n\n";

    auto next = steady_clock::now() + seconds(5);
    while (!g_stop) {
        std::this_thread::sleep_for(milliseconds(200));
        if (!quiet && steady_clock::now() >= next) {
            auto s = adcs.snapshot();
            std::cout << "  [SIM] t=" << (int)s.sim_elapsed_sec
                      << "s  rpy=(" << (int)s.roll_deg
                      << "°, " << (int)s.pitch_deg
                      << "°, " << (int)s.yaw_deg << "°)"
                      << "  |ω|=" << (s.omega[0]*180/M_PI) << "°/s\n";
            next = steady_clock::now() + seconds(5);
        }
    }
    std::cout << "\nADCS sim shutting down...\n";
    adcs.stop();
    srv.stop();
    return 0;
}
