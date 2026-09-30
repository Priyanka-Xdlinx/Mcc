// SPDX-License-Identifier: Apache-2.0
// xdos_uhf_sim — Drishti SAT UHF transceiver simulator (OpenLST-based).
//
// Byte-faithful model of SAT_UHF_Firmware open-lst/radio/commands.c
// (firmware v6.1.3).  Listens on 127.0.0.1:10060 and speaks the ESP UART
// framing the real radio uses on its OBC-facing UART1:
//
//   [0x22 0x69] [len] [hwid(2 LE) seqnum(2 LE) system(1) command(1) data...]
//
// Behaviour mirrored from the firmware:
//   * hwid filter: accepts its own HWID or HWID_LOCAL (0xFFFF); system must
//     be MSG_TYPE_RADIO_IN (1).  Anything else is silently ignored.
//   * set_tx/rx_radio_config share ONE radio_config struct — only the three
//     frequency bytes are banked per direction (freq_mem), exactly like the
//     firmware.
//   * Range verification per config_variables.c — invalid values NACK.
//   * GET_TIME NACKs until SET_TIME has been received (rtc_set).
//   * REBOOT with no payload reboots immediately; with a 4-byte postpone it
//     schedules a reboot at uptime+postpone (NACK above 604800 s).
//   * BEACON_MODE_ON/OFF ack only on a state change (else NACK); the flag is
//     visible in telemetry as reserved0 ("Beacon Mode Active").
//   * SEND_TELEM sets the beacon flag and sends NO reply (fires the RF
//     beacon on the real unit).
//   * Unknown opcodes NACK.
//
//   $ ./xdos_uhf_sim [--port 10060] [--hwid 0001] [--quiet] [--no-color]

#include "common/color.hpp"
#include "common/types.hpp"
#include "framing/uhf_framing.hpp"
#include "transport/tcp_transport.hpp"
#include "common/socket_compat.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>

using namespace xdos;
using namespace std::chrono;
namespace uf = xdos::framing::uhf;

static volatile std::sig_atomic_t g_stop = 0;
static void on_signal(int) { g_stop = 1; }

// ── opcodes (open-lst/radio/radio_commands.h) ───────────────────────────────
enum : Byte {
    kAck               = 0x10, kAscii            = 0x11,
    kReboot            = 0x12, kGetTime          = 0x13, kSetTime  = 0x14,
    kRanging           = 0x15, kGetTelem         = 0x17, kTelem    = 0x18,
    kGetCallsign       = 0x19, kSetCallsign      = 0x1a, kCallsign = 0x1b,
    kSendTelem         = 0x1c,
    kSetBeaconTimers   = 0x1e, kGetBeaconTimers  = 0x1f, kBeaconTimersAck = 0x20,
    kSetTxRadioCfg     = 0x24, kSetTxRadioCfgAck = 0x25,
    kGetTxRadioCfg     = 0x26, kGetTxRadioCfgResp= 0x27,
    kSetRxRadioCfg     = 0x28, kSetRxRadioCfgAck = 0x29,
    kGetRxRadioCfg     = 0x2a, kGetRxRadioCfgResp= 0x2b,
    kSetTxPktCfg       = 0x2c, kSetTxPktCfgAck   = 0x2d,
    kGetTxPktCfg       = 0x2e, kGetTxPktCfgResp  = 0x2f,
    kSetRxPktCfg       = 0x30, kSetRxPktCfgAck   = 0x31,
    kGetRxPktCfg       = 0x32, kGetRxPktCfgResp  = 0x33,
    kSetPower          = 0x34, kSetPowerAck      = 0x35,
    kGetPower          = 0x36, kGetPowerResp     = 0x37,
    kBeaconOn          = 0x38, kBeaconOff        = 0x39,
    kDebugOn           = 0x53, kDebugOff         = 0x54,
    kNack              = 0xff,
};

// ── firmware state structs (byte layouts match config_variables.h) ──────────
struct RadioConfig {                   // radio_config_t — 22 B
    Byte f[22];
};
struct PktConfig  { Byte f[8]; };      // radio_pkt_config_t — 8 B
struct PowerCfg   { Byte f[13]; };     // radio_power_settings_t — 13 B

struct SimState {
    std::mutex mu;
    // shared radio_config + per-direction frequency bank (like the firmware)
    RadioConfig radio_config{};
    Byte tx_freq[3], rx_freq[3];
    PktConfig pkt{};
    PowerCfg  pow{};
    Byte  tx_period = 2, tx_timeout = 30, tx_interval_timeout = 60;
    std::uint32_t beacon_mode_timeout = 1200;
    char  callsign[8] = { 'D','R','S','H','T','I','0','1' };
    bool  rtc_set = false;
    std::int64_t rtc_offset_s = 0;          // rtc = wall(sim) + offset
    bool  beacon_mode = false;              // telemetry reserved0
    bool  debug_mode = false;
    steady_clock::time_point boot = steady_clock::now();
    std::uint64_t auto_reboot_at = 0;       // uptime seconds; 0 = none
    // counters
    std::uint32_t uart1_rx = 0, pkts_sent = 0, pkts_good = 0, cs_count = 0;

    std::uint32_t uptime() const {
        return (std::uint32_t)duration_cast<seconds>(steady_clock::now() - boot)
                   .count();
    }
    void reboot() {
        boot = steady_clock::now();
        auto_reboot_at = 0;
        beacon_mode = false;
        // config + rtc survive in the firmware only via flash defaults —
        // a reboot reloads var_init() defaults and clears the RTC.
        init_defaults();
        rtc_set = false;
    }
    void init_defaults() {
        // var_init() values (config_variables.c + board_defaults.h)
        Byte rc[22] = { 0x0E, 0xD2, 0x7D,   // TX FREQ2/1/0 (436.5 MHz band)
                        0x06,               // FSCTRL1
                        0x00,               // CHANNR
                        0x00,               // FSCTRL0
                        0x00,               // DEM_DCFILT_OFF
                        0x00,               // MOD_FORMAT 2-FSK
                        0x00,               // MANCHESTER_EN
                        0x03,               // SYNC_MODE 30/32
                        0x03, 0x03,         // CHANBW_E/M
                        0x08, 0x20,         // DRATE_E/M (7.4 kbps)
                        0x01,               // FEC_EN
                        0x02,               // NUM_PREAMBLE (4 bytes)
                        0x03, 0xB9,         // CHANSPC_E/M
                        0x01, 0x01,         // DEVIATION_E/M
                        0x07,               // RX_TIMEOUT end-of-packet
                        0x00 };             // RX_ATTENUATION 0 dB
        std::memcpy(radio_config.f, rc, 22);
        tx_freq[0] = 0x0E; tx_freq[1] = 0xD2; tx_freq[2] = 0x7D;
        rx_freq[0] = 0x10; rx_freq[1] = 0xAB; rx_freq[2] = 0x9D;
        Byte pc[8] = { 0x91, 0xD3,          // SYNC_WORD0/1
                       0xFF,                // MAX_PKT_LENGTH
                       0x01,                // WHITENING_ENABLE
                       0x00,                // PACKET_FORMAT normal
                       0x00,                // CRC_DISABLED
                       0x01,                // PKT_LEN_CONFIG variable
                       0x00 };              // DEVICE_ADDR
        std::memcpy(pkt.f, pc, 8);
        Byte pw[13] = { 0x00, 0x00,         // MAX_DVGA_GAIN, MAX_LNA_GAIN
                        0x01,               // AGC_LNA_PRIORITY
                        0x01,               // FILTER_LENGTH 16
                        0x00,               // FREND0
                        87, 60, 96, 207, 135, 129, 197, 192 };  // PA7..PA0
        std::memcpy(pow.f, pw, 13);
        tx_period = 2; tx_timeout = 30; tx_interval_timeout = 60;
        beacon_mode_timeout = 1200;
    }
};
static SimState g_state;
static std::uint16_t g_hwid = 0x0001;
static bool g_quiet = false;

// range checks — config_variables.c var_range_verification*
static bool rc_valid(const Byte* f) {
    return f[6] <= 1 && f[7] <= 7 && f[8] <= 1 && f[9] <= 7 && f[10] <= 3 &&
           f[11] <= 3 && f[12] <= 15 && f[14] <= 1 && f[15] <= 7 &&
           f[16] <= 15 && f[18] <= 15 && f[19] <= 15 && f[20] <= 7 &&
           f[21] <= 3;
}
static bool pkt_valid(const Byte* f) {
    return f[3] <= 1 && f[4] <= 3 && f[5] <= 1 && f[6] <= 3;
}
static bool pow_valid(const Byte* f) {
    return f[0] <= 3 && f[1] <= 7 && f[2] <= 1 && f[3] <= 3 && f[4] <= 7;
}

static void put_u32(Bytes& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 0xFF);
}
static std::uint32_t get_u32(const Byte* p) {
    return (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) |
           ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24);
}

static Bytes make_telemetry(SimState& s) {
    Bytes t;
    const std::uint32_t up = s.uptime();
    put_u32(t, up);                             // uptime
    put_u32(t, 0);                              // uart0_rx_count (RF side)
    put_u32(t, s.uart1_rx);                     // uart1_rx_count
    t.push_back(0); t.push_back(0);             // rx_mode, tx_mode
    // 10 ADC channels: plausible raw readings (temp ~25C, VDD/3, int temp)
    const std::int16_t adc[10] = {
        (std::int16_t)(1200 + (int)(up % 7)), 0, 0, 0, 0, 0, 0, 0,
        (std::int16_t)(1105 + (int)(up % 3)),
        (std::int16_t)(1420 + (int)(up % 5)) };
    for (auto v : adc) { t.push_back(v & 0xFF); t.push_back((v >> 8) & 0xFF); }
    t.push_back((Byte)(std::int8_t)(-72 + (int)(up % 9)));   // last_rssi
    t.push_back((Byte)(105 + up % 8));                       // last_lqi
    t.push_back((Byte)(std::int8_t)(2 - (int)(up % 5)));     // last_freqest
    put_u32(t, s.pkts_sent);
    put_u32(t, s.cs_count);
    put_u32(t, s.pkts_good);
    put_u32(t, 0);                              // rejected_checksum
    put_u32(t, 0);                              // rejected_reserved
    put_u32(t, 0);                              // rejected_other
    t.push_back(s.beacon_mode ? 1 : 0);         // reserved0 = beacon flag
    put_u32(t, 0);                              // custom0
    put_u32(t, 0);                              // custom1
    return t;                                   // 70 B
}

// ── the firmware dispatcher (commands.c), one reply frame or empty ──────────
static Bytes handle(const uf::Decoded& c) {
    SimState& s = g_state;
    std::lock_guard<std::mutex> lk(s.mu);
    s.uart1_rx += (std::uint32_t)(uf::kHeaderSize + c.data.size() + 3);
    // lazy scheduled-reboot check
    if (s.auto_reboot_at && s.uptime() >= s.auto_reboot_at) s.reboot();

    Byte  op   = kNack;
    Bytes data;
    switch (c.command) {
        case kAck:  op = kAck;  break;
        case kNack: op = kNack; break;

        case kReboot:
            if (c.data.size() < 4) { op = kAck; s.reboot(); }
            else {
                std::uint32_t postpone = get_u32(c.data.data());
                if (postpone > 604800) op = kNack;      // AUTO_REBOOT_MAX
                else { op = kAck; s.auto_reboot_at = s.uptime() + postpone; }
            }
            break;

        case kGetTime:
            if (!s.rtc_set) { op = kNack; break; }
            op = kSetTime;                              // firmware replies 0x14
            {
                auto wall = (std::int64_t)duration_cast<seconds>(
                    steady_clock::now().time_since_epoch()).count();
                put_u32(data, (std::uint32_t)(wall + s.rtc_offset_s));
                put_u32(data, 0);                       // nanoseconds
            }
            break;

        case kSetTime:
            if (c.data.size() >= 8) {
                auto wall = (std::int64_t)duration_cast<seconds>(
                    steady_clock::now().time_since_epoch()).count();
                s.rtc_offset_s = (std::int64_t)get_u32(c.data.data()) - wall;
                s.rtc_set = true;
            }
            op = kAck;
            break;

        case kGetTelem:
            op = kTelem;
            data = make_telemetry(s);
            break;

        case kSetCallsign:
            op = kAck;
            std::memset(s.callsign, 0, sizeof(s.callsign));
            std::memcpy(s.callsign, c.data.data(),
                        std::min(c.data.size(), sizeof(s.callsign)));
            break;

        case kGetCallsign:
            op = kCallsign;
            data.assign(s.callsign, s.callsign + sizeof(s.callsign));
            break;

        case kSendTelem:                                // beacon trigger:
            s.beacon_mode = true;                       // NO UART reply
            s.pkts_sent++;
            return {};

        case kSetBeaconTimers:
            op = kAck;
            if (c.data.size() >= 7) {
                s.tx_period = c.data[0];
                s.tx_timeout = c.data[1];
                s.tx_interval_timeout = c.data[2];
                s.beacon_mode_timeout = get_u32(c.data.data() + 3);
            }
            break;

        case kGetBeaconTimers:
            op = kBeaconTimersAck;
            data = { s.tx_period, s.tx_timeout, s.tx_interval_timeout };
            put_u32(data, s.beacon_mode_timeout);
            break;

        case kSetTxRadioCfg:
        case kSetRxRadioCfg:
            if (c.data.size() >= 22 && rc_valid(c.data.data())) {
                op = c.command == kSetTxRadioCfg ? kSetTxRadioCfgAck
                                                 : kSetRxRadioCfgAck;
                std::memcpy(s.radio_config.f, c.data.data(), 22);
                Byte* bank = c.command == kSetTxRadioCfg ? s.tx_freq : s.rx_freq;
                bank[0] = c.data[0]; bank[1] = c.data[1]; bank[2] = c.data[2];
            } else op = kNack;
            break;

        case kGetTxRadioCfg:
        case kGetRxRadioCfg: {
            op = c.command == kGetTxRadioCfg ? kGetTxRadioCfgResp
                                             : kGetRxRadioCfgResp;
            const Byte* bank = c.command == kGetTxRadioCfg ? s.tx_freq
                                                           : s.rx_freq;
            s.radio_config.f[0] = bank[0];
            s.radio_config.f[1] = bank[1];
            s.radio_config.f[2] = bank[2];
            data.assign(s.radio_config.f, s.radio_config.f + 22);
            break;
        }

        case kSetTxPktCfg:
        case kSetRxPktCfg:
            if (c.data.size() >= 8 && pkt_valid(c.data.data())) {
                op = c.command == kSetTxPktCfg ? kSetTxPktCfgAck
                                               : kSetRxPktCfgAck;
                std::memcpy(s.pkt.f, c.data.data(), 8);
            } else op = kNack;
            break;

        case kGetTxPktCfg:
        case kGetRxPktCfg:
            op = c.command == kGetTxPktCfg ? kGetTxPktCfgResp
                                           : kGetRxPktCfgResp;
            data.assign(s.pkt.f, s.pkt.f + 8);
            break;

        case kSetPower:
            if (c.data.size() >= 13 && pow_valid(c.data.data())) {
                op = kSetPowerAck;
                std::memcpy(s.pow.f, c.data.data(), 13);
            } else op = kNack;
            break;

        case kGetPower:
            op = kGetPowerResp;
            data.assign(s.pow.f, s.pow.f + 13);
            break;

        case kBeaconOn:
            if (!s.beacon_mode) { op = kAck; s.beacon_mode = true; }
            else op = kNack;
            break;

        case kBeaconOff:
            if (s.beacon_mode) { op = kAck; s.beacon_mode = false; }
            else op = kNack;
            break;

        case kDebugOn:  op = kAck; s.debug_mode = true;  break;
        case kDebugOff: op = kAck; s.debug_mode = false; break;

        default: op = kNack; break;
    }
    // reply carries the radio's own hwid + echoed seqnum (commands.c)
    return uf::encode(op, data, g_hwid, c.seqnum);
}

static void serve(int cfd) {
    Bytes acc;
    while (!g_stop) {
        auto rx = transport::fd_recv(cfd, 4096, 300);
        if (rx.empty()) continue;
        acc.insert(acc.end(), rx.begin(), rx.end());
        // extract complete ESP frames
        for (;;) {
            std::size_t i = 0;
            while (i + 1 < acc.size() &&
                   !(acc[i] == uf::kStart0 && acc[i+1] == uf::kStart1)) ++i;
            if (i > 0) acc.erase(acc.begin(), acc.begin() + i);
            if (acc.size() < 3) break;
            const std::size_t len = acc[2];
            if (acc.size() < 3 + len) break;
            Bytes frame(acc.begin(), acc.begin() + 3 + len);
            acc.erase(acc.begin(), acc.begin() + 3 + len);

            auto c = uf::decode(frame);
            if (!c.framed_ok) continue;
            // firmware hwid/system filter — silently ignore others
            if (c.system != uf::kSystemRadio ||
                (c.hwid != g_hwid && c.hwid != uf::kHwidLocal)) {
                if (!g_quiet)
                    std::cout << color::hint("[uhf] ignored frame for hwid ")
                              << c.hwid << "\n";
                continue;
            }
            if (!g_quiet)
                std::printf("[uhf] cmd 0x%02x seq %u len %zu\n",
                            c.command, c.seqnum, c.data.size());
            Bytes reply = handle(c);
            if (!reply.empty()) transport::fd_send(cfd, reply);
        }
    }
}

int main(int argc, char** argv) {
    xdos::net::Startup _net_init;
    int port = 10060;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--no-color") color::set_use_color(false);
        else if (a == "--quiet")    g_quiet = true;
        else if (a == "--port" && i+1 < argc) port = std::atoi(argv[++i]);
        else if (a == "--hwid" && i+1 < argc)
            g_hwid = (std::uint16_t)std::strtoul(argv[++i], nullptr, 16);
        else if (a == "--help" || a == "-h") {
            std::cout << "xdos_uhf_sim [--port 10060] [--hwid 0001] "
                         "[--quiet] [--no-color]\n";
            return 0;
        }
    }
    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);
    g_state.init_defaults();

    if (!g_quiet) {
        std::cout << color::header(
            "╔══════════════════════════════════════════════════════════╗\n"
            "║        XDOS UHF transceiver sim (OpenLST / Drishti)      ║\n"
            "╚══════════════════════════════════════════════════════════╝") << "\n"
            << "  listening on 127.0.0.1:" << port << "  (OBC UHF downlink)\n"
            << "  hwid 0x" << std::hex << g_hwid << std::dec
            << "  ·  ESP framing 22 69 | len | payload  ·  firmware v6.1.3 model\n"
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
