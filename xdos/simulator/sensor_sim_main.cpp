// SPDX-License-Identifier: Apache-2.0
// xdos_sensor_sim — avionics sensor controller (Tracker) simulator.
//
// Listens on 127.0.0.1:10070 and speaks the frame format from
// SENSOR_CONTROLLER_Tracker_FRAME_FORMAT_ENUMS:
//
//   [0xA5 0xAA] [dest_id] [cmd_id] [len] [payload: len] [0xBA]
//
// Models the five documented commands:
//   128  TC_SET_POWER_CTRL_PS_ES_BRD     power PS/ES ON, OFF or restart
//   129  TC_SET_PS_ES_SSD_SELECTION      pick the boot SSD for PS/ES
//     1  TC_GET_TEMPERATURE_SENSOR_DATA  OBC/PS/NIC temperature, BIG-endian
//     4  TC_GET_SSD_SELC                 read back the SSD selection
//     7  TC_GET_BOARD_PWR_CTRL_STS       power state of PSA/PSB/ES1/ES2
//
// Where the sheets are silent the sim takes the most defensible reading and
// says so in a comment.  Two such choices are load-bearing, and both are open
// questions with the board team:
//
//   * board_type addresses PS or ES only, while the status response reports
//     four boards (PSA/PSB/ES1/ES2).  The sim drives the A-side of the pair
//     (PSA, ES1) and leaves B/2 as configured — so a round trip through
//     command 7 shows the effect of a 128 without inventing an addressing
//     scheme the spec does not define.
//   * A NACK is (error_event, error_byte) under the same cmd_id.  The sim
//     NACKs on unknown cmd_id, wrong dest_id, and out-of-range parameters.
//
//   $ ./xdos_sensor_sim [--port 10070] [--dest 50] [--quiet] [--no-color]

#include "common/color.hpp"
#include "common/types.hpp"
#include "framing/sensor_framing.hpp"
#include "transport/tcp_transport.hpp"
#include "common/socket_compat.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <thread>

using namespace xdos;
using namespace std::chrono;
namespace sf = xdos::framing::sensor;

static volatile std::sig_atomic_t g_stop = 0;
static void on_signal(int) { g_stop = 1; }

static bool g_quiet = false;
static Byte g_dest  = sf::kDestId;

// ── command ids (TC SET / TC GET sheets) ────────────────────────────────────
enum : Byte {
    kGetTemperature = 1,
    kGetSsdSelect   = 4,
    kGetPwrCtrlSts  = 7,
    kSetPowerCtrl   = 128,
    kSetSsdSelect   = 129,
};

// ── CMD STATUS table (TC sheets) — the error_field inside an ACK payload ────
enum : Byte {
    kCmdSuccess      = 0,
};

// ── Events table (frame-format sheet) — the ERROR EVENT byte of a NACK ──────
// Deliberately separate from CMD STATUS above: the two tables disagree from
// code 5 upward (Events has no ERR_INVALID_VALUE, so everything after it
// shifts by one).  Mixing them would report ERR_PARAM_RANGE as ERR_HW_FAULT.
enum : Byte {
    kEvtInvalidDest  = 2,
    kEvtInvalidCmd   = 3,
    kEvtLenMismatch  = 4,
    kEvtParamRange   = 8,
};

// ── enumerations — note PS is 0 in board_type but 1 in hw_instance ──────────
enum : Byte { kBoardPS = 0, kBoardES = 1 };
enum : Byte { kOpPowerOn = 0, kOpPowerOff = 1, kOpRestart = 2 };
enum : Byte { kHwObc = 0, kHwPs = 1, kHwNic = 2 };
enum : Byte { kPowerOn = 0, kPowerOff = 1 };

struct SimState {
    // Power state of the four boards, as reported by command 7.
    // Cold start: everything off, which is what the ground should see before
    // it has commanded anything.
    Byte psa = kPowerOff, psb = kPowerOff;
    Byte es1 = kPowerOff, es2 = kPowerOff;

    // Selected boot device per board_type (index 0 = PS, 1 = ES).
    Byte ssd[2] = { 0, 0 };

    steady_clock::time_point boot = steady_clock::now();
};
static SimState g_state;

// Plausible temperature: a slow sine around a per-instance baseline, in
// hundredths of a degree.  Signed, so the OBC sensor dips below zero and the
// negative worked example from the sheet is exercised on a live link.
static std::int16_t temperature_c100(Byte hw_instance) {
    double base = 0.0, swing = 0.0;
    switch (hw_instance) {
        case kHwObc: base =  -4.0; swing =  8.0; break;   // crosses zero
        case kHwPs:  base =  32.0; swing =  6.0; break;   // warm compute board
        case kHwNic: base =  27.5; swing =  4.0; break;
        default:     base =  20.0; swing =  1.0; break;
    }
    double t = duration<double>(steady_clock::now() - g_state.boot).count();
    double c = base + swing * std::sin(t / 45.0);
    return static_cast<std::int16_t>(std::lround(c * 100.0));
}

// NACK payload per the frame-format sheet: ERROR EVENT then ERROR BYTE.
// error_byte carries the offending cmd_id so the ground can correlate.
static Bytes nack(Byte cmd_id, Byte error_event) {
    return sf::encode(g_dest, cmd_id, Bytes{ error_event, cmd_id });
}

static Bytes handle(const sf::Decoded& c) {
    // A frame for somebody else is not ours to answer.  The real controller
    // would stay silent; the sim NACKs so a misaddressed frame is visible on
    // the bench instead of looking like a dead link.
    if (c.dest_id != g_dest) return nack(c.cmd_id, kEvtInvalidDest);

    switch (c.cmd_id) {

    case kSetPowerCtrl: {
        if (c.data.size() != 2)  return nack(c.cmd_id, kEvtLenMismatch);
        const Byte board = c.data[0], op = c.data[1];
        if (board > kBoardES)    return nack(c.cmd_id, kEvtParamRange);
        if (op > kOpRestart)     return nack(c.cmd_id, kEvtParamRange);

        // Restart lands in the same observable state as ON: the board is up
        // once the command completes.
        const Byte now = (op == kOpPowerOff) ? kPowerOff : kPowerOn;
        if (board == kBoardPS) g_state.psa = now; else g_state.es1 = now;

        return sf::encode(g_dest, c.cmd_id,
                          Bytes{ board, g_state.ssd[board], kCmdSuccess });
    }

    case kSetSsdSelect: {
        if (c.data.size() != 2)  return nack(c.cmd_id, kEvtLenMismatch);
        const Byte board = c.data[0], ssd = c.data[1];
        if (board > kBoardES)    return nack(c.cmd_id, kEvtParamRange);
        if (ssd > 1)             return nack(c.cmd_id, kEvtParamRange);

        g_state.ssd[board] = ssd;
        return sf::encode(g_dest, c.cmd_id,
                          Bytes{ board, ssd, kCmdSuccess });
    }

    case kGetTemperature: {
        if (c.data.size() != 1)  return nack(c.cmd_id, kEvtLenMismatch);
        const Byte hw = c.data[0];
        if (hw > kHwNic)         return nack(c.cmd_id, kEvtParamRange);

        // BIG-endian, unlike every other subsystem on this bus.
        const std::int16_t v = temperature_c100(hw);
        const auto u = static_cast<std::uint16_t>(v);
        return sf::encode(g_dest, c.cmd_id,
                          Bytes{ hw,
                                 static_cast<Byte>((u >> 8) & 0xFF),
                                 static_cast<Byte>(u & 0xFF) });
    }

    case kGetSsdSelect: {
        if (c.data.size() != 1)  return nack(c.cmd_id, kEvtLenMismatch);
        const Byte board = c.data[0];
        if (board > kBoardES)    return nack(c.cmd_id, kEvtParamRange);
        return sf::encode(g_dest, c.cmd_id,
                          Bytes{ board, g_state.ssd[board] });
    }

    case kGetPwrCtrlSts:
        // "NO Structure (CMD-ID Follows)" — the request carries no payload.
        if (!c.data.empty())     return nack(c.cmd_id, kEvtLenMismatch);
        return sf::encode(g_dest, c.cmd_id,
                          Bytes{ g_state.psa, g_state.psb,
                                 g_state.es1, g_state.es2 });

    default:
        return nack(c.cmd_id, kEvtInvalidCmd);
    }
}

static const char* board_name(Byte b) { return b == kBoardPS ? "PS" : "ES"; }
static const char* hw_name(Byte h) {
    return h == kHwObc ? "OBC" : h == kHwPs ? "PS" : h == kHwNic ? "NIC" : "?";
}

static void trace(const sf::Decoded& c) {
    if (g_quiet) return;
    std::printf("[sensor] dest %u cmd %u len %zu", c.dest_id, c.cmd_id,
                c.data.size());
    switch (c.cmd_id) {
        case kSetPowerCtrl:
            if (c.data.size() == 2)
                std::printf("  — %s %s", board_name(c.data[0]),
                            c.data[1] == kOpPowerOn  ? "ON"
                          : c.data[1] == kOpPowerOff ? "OFF" : "RESTART");
            break;
        case kSetSsdSelect:
            if (c.data.size() == 2)
                std::printf("  — %s boot from SSD%u", board_name(c.data[0]),
                            c.data[1]);
            break;
        case kGetTemperature:
            if (c.data.size() == 1)
                std::printf("  — %s temperature = %.2f C", hw_name(c.data[0]),
                            temperature_c100(c.data[0]) / 100.0);
            break;
        case kGetSsdSelect:
            if (c.data.size() == 1)
                std::printf("  — %s SSD selection", board_name(c.data[0]));
            break;
        case kGetPwrCtrlSts:
            std::printf("  — board power status");
            break;
        default: break;
    }
    std::printf("\n");
}

static void serve(int cfd) {
    Bytes acc;
    while (!g_stop) {
        auto rx = transport::fd_recv(cfd, 4096, 300);
        if (rx.empty()) continue;
        acc.insert(acc.end(), rx.begin(), rx.end());

        // Pull out complete frames; the length byte makes each one exact.
        for (;;) {
            std::size_t i = 0;
            while (i + 1 < acc.size() &&
                   !(acc[i] == sf::kStart0 && acc[i+1] == sf::kStart1)) ++i;
            if (i > 0) acc.erase(acc.begin(), acc.begin() + i);
            if (acc.size() < 5) break;                    // header incomplete
            const std::size_t total = sf::frame_size(acc[4]);
            if (acc.size() < total) break;                // payload still coming

            Bytes frame(acc.begin(), acc.begin() + total);
            acc.erase(acc.begin(), acc.begin() + total);

            auto c = sf::decode(frame);
            if (!c.framed_ok) {
                // Sync matched but EOF did not land where the length said.
                if (!g_quiet)
                    std::cout << color::warn("[sensor] malformed frame dropped\n");
                continue;
            }
            trace(c);
            Bytes reply = handle(c);
            if (!reply.empty()) transport::fd_send(cfd, reply);
        }
    }
}

int main(int argc, char** argv) {
    xdos::net::Startup _net_init;
    int port = 10070;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--no-color") color::set_use_color(false);
        else if (a == "--quiet")    g_quiet = true;
        else if (a == "--port" && i+1 < argc) port = std::atoi(argv[++i]);
        else if (a == "--dest" && i+1 < argc)
            g_dest = static_cast<Byte>(std::atoi(argv[++i]));
        else if (a == "--help" || a == "-h") {
            std::cout << "xdos_sensor_sim [--port 10070] [--dest 50] "
                         "[--quiet] [--no-color]\n";
            return 0;
        }
    }
    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    if (!g_quiet) {
        std::cout << color::header(
            "╔══════════════════════════════════════════════════════════╗\n"
            "║        XDOS sensor controller sim (Tracker / AVN)        ║\n"
            "╚══════════════════════════════════════════════════════════╝") << "\n"
            << "  listening on 127.0.0.1:" << port << "  (OBC sensor downlink)\n"
            << "  dest-id " << (int)g_dest
            << "  ·  framing A5 AA | dest | cmd | len | payload | BA\n"
            << color::hint("  Ctrl-C to stop.") << "\n\n";
    }

    transport::TcpServer server;
    bool started = server.start("0.0.0.0", port,
        [](int cfd, const std::string&) { serve(cfd); });
    if (!started) {
        std::cerr << "cannot bind port " << port << "\n";
        return 1;
    }
    while (!g_stop) std::this_thread::sleep_for(milliseconds(200));
    server.stop();
    return 0;
}
