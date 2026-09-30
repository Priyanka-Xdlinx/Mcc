// SPDX-License-Identifier: Apache-2.0
// xdos_xlink_sim — XLINK-S Service Server simulator.
//
// Listens on TCP 51003 by default (see XLink Service Server V1.8.1 §2.1).
// Accepts any product key (real hardware validates against a device-specific
// 128-bit key; this sim skips auth so any client can talk to it).
// Handles the 7 services modelled in mib/xlink.json:
//
//    0x00000000  IDLE          → ACK (keepalive / reset 6s timeout)
//    0x009AE41B  RESET         → ACK (sim ignores; real HW would reboot)
//    0x00000003  VERSION  GET  → returns 3 file entries (FPGA/SWAP/CPU1)
//    0x00000004  HOUSE_KEEP GET→ returns voltages / temperatures / RCV+XMT status
//    0x00000007  UART_BD   GET/SET → returns baud-mode 3 (115200)
//    0x00000009  IP_ADR    GET/SET → returns 192.168.1.1
//    0x00000001  DEVICE_CONF GET/SET → returns a plausible RF configuration
//
// Responses echo the client's product_key in the first 16 bytes and set the
// ACK bit (0x40000000) on service_event.  On a request Header-only services
// (IDLE, RESET) send a 20-byte ACK.  Data-bearing services send a full frame
// matching the MIB layout, filled with representative values that a comms
// engineer would find plausible (BPSK, FEC 3/4, +10dBm at 460800 baud, etc.).

#include "transport/tcp_transport.hpp"
#include "subsystems/xlink_codec.hpp"
#include "mib/mib.hpp"
#include "common/color.hpp"
#include "common/socket_compat.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

using namespace xdos;
using namespace std::chrono;
using json = nlohmann::json;

static std::atomic<bool> g_stop{false};
static void on_signal(int) { g_stop = true; }

// Locate the MIB directory the same way other binaries do.
static std::filesystem::path find_mib_dir() {
    for (const auto& p : { std::filesystem::path("mib"),
                           std::filesystem::path("../mib"),
                           std::filesystem::path("../../mib") }) {
        if (std::filesystem::is_directory(p)) return p;
    }
    return "mib";  // will throw upstream if missing
}

// Build a plausible response for one service, using the sim's own uptime as
// a driver so consecutive calls show sensible telemetry drift.
static nlohmann::json make_response_values(const std::string& tc_name,
                                           std::uint32_t sim_uptime_ms) {
    json v;
    if (tc_name == "VERSION") {
        // Firmware release 1.7.11 (20260101 date-code) — plausible for our sim.
        v["F1_Seconds"] = 30;   v["F1_Minutes"] = 30;  v["F1_Hours"] = 12;
        v["F1_Year"]    = 26;   v["F1_Months"]  = 1;   v["F1_Day"]   = 1;
        v["F1_FileName"] = 0x41475046;  // "FPGA" in ASCII (little-endian)
        v["F1_start_adr"] = 0x1D46C06F;
        v["F1_len"]     = 2500000;
        v["F1_Flags"]   = 0x3;      // SOLL_PROG | RUNNING
        v["F2_FileName"] = 0x50415753; // "SWAP"
        v["F2_len"]     = 400000;
        v["F3_FileName"] = 0x31555043; // "CPU1"
        v["F3_len"]     = 1200000;
    }
    else if (tc_name == "HOUSE_KEEPING") {
        v["MoreHkInfo"] = 0;
        v["V_VCCINT"]  = 1.001;   v["V_VCCAUX"] = 1.800;
        v["V_VCCBRAM"] = 1.000;   v["V_VCCPINT"]= 0.999;
        v["V_VCCPAUX"] = 1.802;   v["V_VCCPDRO"]= 1.499;
        v["T_AD9361"] = 42;       v["T_EthPhy"] = 38;
        v["T_Zynq"]   = 55;       v["T_Reserved"] = 0;
        // RCV Ch0 — sync locked, BPSK BCH, decent SIR
        v["Rcv0_On"] = 1;         v["Rcv0_Sync"] = 1;
        v["Rcv0_idxGain"] = 42;   v["Rcv0_SirMin"] = 18;   v["Rcv0_SirMax"] = 25;
        v["Rcv0_DopplerFreq"] = -12;
        v["Rcv0_PwrMin"] = 8;     v["Rcv0_PwrMax"] = 12;
        v["Rcv0_RxState"] = 4;
        v["Rcv0_Modulat"] = 1;    v["Rcv0_Fec"] = 6;  // BCH
        v["Rcv0_BchBlockCount"] = 12000 + sim_uptime_ms / 100;
        v["Rcv0_BchErrCount"]   = 5;
        // RCV Ch1 — idle
        v["Rcv1_On"] = 0;
        // XMT Ch0 — QPSK, FEC 3/4, mid-power
        v["Xmt0_On"] = 1;         v["Xmt0_Modulation"] = 2;  // QPSK
        v["Xmt0_Fec"] = 3;                                    // 3/4
        v["Xmt0_Att"] = 150;                                  // 15.0 dB
        v["Xmt1_On"] = 0;
        v["card_run_counter"] = sim_uptime_ms / 10;   // 10-ms ticks
    }
    else if (tc_name == "UART_BD") {
        v["BdMode"] = 3;   // 115200
    }
    else if (tc_name == "IP_ADR") {
        v["ip_address"] = 0xC0A80101u;  // 192.168.1.1 → 0x01 0x01 0xA8 0xC0 on wire
    }
    else if (tc_name == "UART_BD_SAVE" || tc_name == "IP_ADR_SAVE") {
        // Save-to-NVM services echo the previously-set value; here we return
        // the same defaults as the temporary GETs.
        if (tc_name == "UART_BD_SAVE") v["BdMode"] = 3;
        else                            v["ip_address"] = 0xC0A80101u;
    }
    else if (tc_name == "XPA_INF") {
        // External X-band Power Amplifier — PSU good, ~2 A, 45 °C
        v["XPA0_PGOOD"]   = 1;
        v["XPA0_Current"] = 2100;    // mA
        v["XPA0_Temp"]    = 45;
        v["XPA1_PGOOD"]   = 0;       // second XPA not fitted
        v["XPA1_Current"] = 0;
        v["XPA1_Temp"]    = 0;
    }
    else if (tc_name == "DVBS2_CONF") {
        v["ModCod"]         = 7;     // QPSK 3/4 — common satellite downlink choice
        v["FrmType"]        = 0;     // NORMAL
        v["InsertPilots"]   = 1;
        v["Acm_Ccm"]        = 1;     // CCM
        v["RollOffVal"]     = 2;     // 0.20
        v["PlSpreadNumber"] = 0;
    }
    else if (tc_name == "START_RF_RCV") {
        v["ip_address"]       = 0xC0A80102u; // 192.168.1.2
        v["port"]             = 51050;
        v["send_format"]      = 0;    // NORMAL
        v["output_interface"] = 0;    // UDP over Ethernet
    }
    else if (tc_name == "RF_XMT_DATA") {
        // Real hardware ACKs with occupancy info; sim just says OK.
        v["PktType"]  = 2;     // TRANSFER_FRM
        v["ErrInf"]   = 0;     // OK
        v["PktLen"]   = 892;
        v["MoreInfo"] = 100;   // 100 bytes free in TrFrm memory
    }
    else if (tc_name == "RCV_TRF_LEN") {
        v["TrFrmLen"] = 892;
    }
    else if (tc_name == "DEVICE_CONF") {
        // A believable X-band DVB-S2 configuration.
        v["RfeHwPins"] = 0x0007FFFF;
        // S-band carrier frequencies (XLINK-S).  Per the device: RX 2.02415 GHz,
        // TX 2.2425 GHz — both within the 2–2.4 GHz S-band. (Values are kHz.)
        v["RxFreq_Ch0"] = 2024150;  v["RxFreq_Ch1"] = 2024150;
        v["TxFreq_Ch0"] = 2242500;  v["TxFreq_Ch1"] = 2242500;
        v["Use_2Rx2Tx"] = 1;
        v["TxLevel_Ch0"] = 1024;    v["TxLevel_Ch1"] = 1024;
        v["RefClk"] = 40;
        // BPSK on both channels, FEC 1/2, PRnd on
        v["Modulat_Ch0"] = 1;       v["Mode_Ch0"] = 8;     // Normal
        v["Fec_Ch0"] = 1;           v["ChEn_Ch0"] = 1;     v["PRnd_Ch0"] = 1;
        v["AttTxGain_Ch0"] = 300;                          // 30.0 dB
        v["AttTxGain_Ch1"] = 300;
        v["TrFrmSz"] = 892;                                // 892-byte transfer frame
        // RCV BPSK BCH
        v["Rx_GainMax_Ch0"] = 60;   v["Rx_ChEn_Ch0"] = 1;
        v["Rx_Modulat_Ch0"] = 1;    v["Rx_Fec_Ch0"] = 6;   // BCH
        v["Rx_BchDec_Ch0"] = 1;
        // From the product key file we loaded earlier: 2048 kHz XMT, 512 kHz RCV
        v["XmtSymbFreq"] = 2048;    v["RcvSymbFreq"] = 512;
    }
    return v;
}

static void handle_connection(int cfd, const mib::Mib& mib,
                              const std::string& peer,
                              steady_clock::time_point sim_start) {
    while (!g_stop) {
        // XLink frames are at most 2076 bytes (spec §3).  Read up to that.
        auto rx = transport::fd_recv_blocking(cfd, 2076, 500);
        if (rx.empty()) return;   // peer closed or idle timeout

        std::cout << color::hint("[XLINK-S sim] ← " + peer + " (" +
                                 std::to_string(rx.size()) + " bytes)") << "\n";

        // Decode header only — we don't need the request's data content, we
        // just need to know what service was asked for.
        auto d = xdos::xlink::codec::decode_frame(rx, nullptr);
        if (!d.framed_ok) {
            std::cout << color::warn("  [XLINK-S sim] short frame, ignoring\n");
            continue;
        }
        bool is_get = (d.event_flags & xdos::xlink::codec::FLAG_GET) != 0;
        bool is_set = (d.event_flags & xdos::xlink::codec::FLAG_SET) != 0;

        // Find the request message in the MIB by event base
        const mib::Message* tc = nullptr;
        auto* def = mib.subsystem(Subsystem::XLINK);
        if (def) {
            for (auto const& m : def->messages) {
                if (m.direction == Direction::TC &&
                    static_cast<std::uint32_t>(m.msg_id) == d.event_base) {
                    tc = &m; break;
                }
            }
        }
        if (!tc) {
            std::cout << color::warn("  [XLINK-S sim] unknown event 0x")
                      << std::hex << d.event_base << std::dec << "\n";
            // NAK: header only, NAK flag set
            std::vector<std::uint8_t> nak(20, 0);
            std::memcpy(nak.data(), d.key_echoed.data(), 16);
            std::uint32_t se = d.event_base | 0x80000000;   // NAK
            nak[16] = static_cast<std::uint8_t>(se);
            nak[17] = static_cast<std::uint8_t>(se >>  8);
            nak[18] = static_cast<std::uint8_t>(se >> 16);
            nak[19] = static_cast<std::uint8_t>(se >> 24);
            send(cfd, reinterpret_cast<const char*>(nak.data()), (int)nak.size(), 0);
            continue;
        }

        std::cout << color::header("  " + tc->name)
                  << (is_get ? "  [GET]" : (is_set ? "  [SET]" : "  [?]")) << "\n";

        // The TM message has the same layout for both GET responses and SET-echoes.
        const mib::Message* tm = nullptr;
        for (auto const& m : def->messages) {
            if (m.direction == Direction::TM &&
                static_cast<std::uint32_t>(m.msg_id) == d.event_base) {
                tm = &m; break;
            }
        }

        auto uptime_ms = duration_cast<milliseconds>(
                            steady_clock::now() - sim_start).count();
        auto vals = make_response_values(tc->name,
                                         static_cast<std::uint32_t>(uptime_ms));

        // Build the response frame.  Encode using ACK|GET or ACK|SET on the wire.
        std::uint32_t flag = is_get ? xdos::xlink::codec::FLAG_GET
                                    : xdos::xlink::codec::FLAG_SET;
        std::uint32_t ack_flag = flag | 0x40000000;
        // If no TM is defined (IDLE/RESET), send a 20-byte header-only ACK.
        std::vector<std::uint8_t> tx;
        if (tm && !vals.empty()) {
            xdos::xlink::codec::ProductKey key{};
            std::memcpy(key.data(), d.key_echoed.data(), 16);
            tx = xdos::xlink::codec::encode_frame(key, d.event_base, ack_flag, *tm, vals);
        } else {
            tx.assign(20, 0);
            std::memcpy(tx.data(), d.key_echoed.data(), 16);
            std::uint32_t se = d.event_base | ack_flag;
            tx[16] = static_cast<std::uint8_t>(se);
            tx[17] = static_cast<std::uint8_t>(se >>  8);
            tx[18] = static_cast<std::uint8_t>(se >> 16);
            tx[19] = static_cast<std::uint8_t>(se >> 24);
        }

        std::cout << "  → ACK (" << tx.size() << " bytes)\n";
        int sent = send(cfd, reinterpret_cast<const char*>(tx.data()), (int)tx.size(), 0);
        if (sent < 0) return;
    }
}

int main(int argc, char** argv) {
    xdos::net::Startup _net_init;
    int  port  = 10050;
    bool quiet = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--no-color") color::set_use_color(false);
        else if (a == "--quiet")    quiet = true;
        else if (a == "--port" && i+1 < argc) port = std::atoi(argv[++i]);
        else if (a == "--help" || a == "-h") {
            std::cout << "xdos_xlink_sim [--port 10050] [--no-color] [--quiet]\n";
            return 0;
        }
    }
    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    mib::Mib mib;
    try { mib.load_dir(find_mib_dir()); }
    catch (const std::exception& e) {
        std::cerr << "MIB load failed: " << e.what() << "\n";
        return 1;
    }
    if (!mib.subsystem(Subsystem::XLINK)) {
        std::cerr << "XLink MIB missing.  Add mib/xlink.json.\n";
        return 1;
    }

    if (!quiet) {
        std::cout << color::header(
            "╔══════════════════════════════════════════════════════════╗") << "\n"
            << color::header(
            "║              XDOS XLINK-S service-server sim               ║") << "\n"
            << color::header(
            "╚══════════════════════════════════════════════════════════╝") << "\n"
            << "  listening on 127.0.0.1:" << port << "  (OBC XLINK-S downlink)\n"
            << "  Spec default is 51003 for real hardware — the sim uses 10050 so\n"
            << "  the OBC bridge can route to it consistently with EPS/ADCS/GPS/Payload.\n"
            << "  MIB services: 7   (IDLE / RESET / VERSION / HOUSE_KEEPING /\n"
            << "                     UART_BD / IP_ADR / DEVICE_CONF)\n"
            << color::hint("  Ctrl-C to stop.") << "\n\n";
    }

    transport::TcpServer server;
    auto sim_start = steady_clock::now();
    bool started = server.start("0.0.0.0", port,
        [&](int fd, const std::string& peer) {
            handle_connection(fd, mib, peer, sim_start);
        });
    if (!started) {
        std::cerr << "failed to bind :" << port << "\n";
        return 1;
    }
    while (!g_stop) std::this_thread::sleep_for(milliseconds(200));
    server.stop();
    std::cout << "\n[XLINK-S sim] shutting down.\n";
    return 0;
}
