// SPDX-License-Identifier: Apache-2.0
// xdos_payload_sim — Payload Server simulator.
//
// Listens on 127.0.0.1:10040 and speaks the Payload Server protocol from the
// "Command and Data Format Specification":
//
//   [3-bit Message Type | 10-bit Command ID | 11-bit Length] [Data]
//
// That frame is normally carried as CCSDS TC_Data under APID 5.  The sim
// accepts both forms: if a frame starts with the CCSDS SOF (1A CF) it is
// unwrapped (and its CRC-16 checked) first; otherwise the bytes are treated
// as a bare payload frame.
//
// The payload server returns no telemetry.  A command that completes is
// acknowledged with a single character in the data field of a frame that is
// otherwise identical to the command (same Command ID), wrapped the same way
// the request arrived.
//
// Reads are length-prefixed off the TCP stream, so coalesced or split
// segments are reassembled correctly.
//
//   $ ./xdos_payload_sim [--port 10040] [--quiet] [--no-color]

#include "codec/ccsds_codec.hpp"
#include "common/color.hpp"
#include "common/types.hpp"
#include "framing/payload_framing.hpp"
#include "transport/tcp_transport.hpp"
#include "common/socket_compat.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>

using namespace xdos;
using namespace std::chrono;
namespace pf = xdos::framing::payload;

static volatile std::sig_atomic_t g_stop = 0;
static void on_sigint(int) { g_stop = 1; }

static const Byte kCcsdsSof0 = 0x1A, kCcsdsSof1 = 0xCF;

static std::string hex_bytes(const Byte* b, std::size_t n) {
    std::string out;
    for (std::size_t i = 0; i < n; ++i) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%02X ", b[i]);
        out += buf;
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

static std::string ascii_of(const Byte* b, std::size_t n) {
    std::string out;
    out.reserve(n + 2);
    out.push_back('"');
    for (std::size_t i = 0; i < n; ++i) {
        Byte c = b[i];
        if (c >= 0x20 && c < 0x7F) out.push_back(static_cast<char>(c));
        else {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\x%02X", c);
            out += buf;
        }
    }
    out.push_back('"');
    return out;
}

static std::string ccsds_profile_path() {
    for (const auto& p : { "mib/ccsds_tc.json", "../mib/ccsds_tc.json",
                           "../../mib/ccsds_tc.json" })
        if (std::filesystem::exists(p)) return p;
    return "";
}

// ─── framing off the TCP stream ─────────────────────────────────────────────
// Returns the total wire size of the frame at the head of `buf`, 0 if more
// bytes are needed to tell.
static std::size_t head_frame_size(const Bytes& buf) {
    if (buf.size() >= 2 && buf[0] == kCcsdsSof0 && buf[1] == kCcsdsSof1) {
        // CCSDS: frame_length field (10 bits) = body size - 1; body starts at [2].
        if (buf.size() < 6) return 0;
        const std::size_t body_sz = (((buf[4] & 0x03u) << 8) | buf[5]) + 1u;
        return 2 + body_sz;
    }
    if (buf.size() < pf::kHeaderLen) return 0;
    return pf::frame_size(buf.data());
}

// ─── decode + report ────────────────────────────────────────────────────────
// Returns the single acknowledgement character to reply with.
static Byte handle_frame(const pf::Decoded& d, std::mutex& mu) {
    std::lock_guard<std::mutex> g(mu);

    char tbuf[32];
    auto t = system_clock::to_time_t(system_clock::now());
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &t);
#else
    gmtime_r(&t, &utc);
#endif
    std::strftime(tbuf, sizeof(tbuf), "%H:%M:%S", &utc);

    std::cout << "\n" << color::header("=== [Payload sim] frame received ===")
              << "  " << color::hint(tbuf) << "\n";
    std::cout << "  Message Type : " << color::ok(pf::type_name(d.type))
              << color::hint("  (" + std::to_string(static_cast<int>(d.type)) + ")") << "\n";
    std::cout << "  Command ID   : " << color::ok(std::to_string(d.command_id))
              << color::hint(std::string("  ") + pf::command_name(d.command_id)) << "\n";
    std::cout << "  Length       : " << d.length;
    if (!d.length_ok)
        std::cout << color::err("  ! declared " + std::to_string(d.length)
                                + " but " + std::to_string(d.data.size()) + " present");
    std::cout << "\n";

    if (d.data.empty()) {
        std::cout << "  Data         : " << color::hint("(none)") << "\n";
    } else {
        std::cout << "  Data         : " << hex_bytes(d.data.data(), d.data.size())
                  << color::hint("  (" + std::to_string(d.data.size()) + " B)") << "\n";
    }

    Byte ack = pf::kAckChar;
    switch (d.command_id) {
        case pf::kCmdContainerOn:
        case pf::kCmdContainerOff: {
            if (d.data.empty()) {
                std::cout << color::err("    ! Payload ID missing — expected 1 data byte\n");
                ack = pf::kNakChar;
                break;
            }
            const Byte pid = d.data[0];
            std::cout << "    └─ Payload ID: " << color::ok(std::to_string(pid))
                      << color::hint(std::string(" (") + pf::payload_name(pid) + ")") << "\n";
            std::cout << "    └─ Action   : " << color::ok(
                std::string(d.command_id == pf::kCmdContainerOn ? "container ON " : "container OFF ")
                + pf::payload_name(pid)) << "\n";
            break;
        }
        case pf::kCmdPayloadSeq: {
            if (d.data.size() < 3) {
                std::cout << color::err("    ! sequence header truncated — need 3 bytes\n");
                ack = pf::kNakChar;
                break;
            }
            const Byte pid      = d.data[0];
            const std::size_t nlen = d.data[1];
            const std::size_t alen = d.data[2];
            std::cout << "    └─ Payload ID: " << color::ok(std::to_string(pid))
                      << color::hint(std::string(" (") + pf::payload_name(pid) + ")") << "\n";
            std::cout << "    └─ Name len  : " << nlen << "\n";
            std::cout << "    └─ Args len  : " << alen << "\n";
            if (3 + nlen + alen != d.data.size()) {
                std::cout << color::err("    ! lengths do not add up to the data field\n");
                ack = pf::kNakChar;
                break;
            }
            std::cout << "    └─ Operation : "
                      << color::ok(ascii_of(d.data.data() + 3, nlen)) << "\n";
            std::cout << "    └─ Arguments : "
                      << (alen ? color::ok(ascii_of(d.data.data() + 3 + nlen, alen))
                               : color::hint("(none)")) << "\n";
            break;
        }
        case pf::kCmdCaptureMsi:
        case pf::kCmdCaptureScanway: {
            const Byte pid = (d.command_id == pf::kCmdCaptureMsi) ? pf::kPayloadMsi
                                                                 : pf::kPayloadScanway;
            std::cout << "    └─ Target    : " << color::ok(pf::payload_name(pid))
                      << color::hint("  (implied by Command ID)") << "\n";
            std::cout << "    └─ Parameters: "
                      << (d.data.empty() ? color::hint("(none)")
                                         : color::ok(ascii_of(d.data.data(), d.data.size())))
                      << "\n";
            break;
        }
        default:
            std::cout << color::warn("    ! unrecognised Command ID\n");
            ack = pf::kNakChar;
            break;
    }

    char abuf[64];
    std::snprintf(abuf, sizeof(abuf), "  → ACK '%c' (0x%02X) for cmd %u\n",
                  static_cast<char>(ack), ack, d.command_id);
    std::cout << (ack == pf::kAckChar ? color::ok(abuf) : color::err(abuf));
    std::cout.flush();
    return ack;
}

int main(int argc, char** argv) {
    xdos::net::Startup _net_init;
    int  port = 10040;
    bool quiet = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--no-color")  color::set_use_color(false);
        else if (a == "--quiet") quiet = true;
        else if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
    }

    std::signal(SIGINT,  on_sigint);
    std::signal(SIGTERM, on_sigint);

    ccsds::CcsdsCodec codec;
    bool ccsds_ok = false;
    if (auto p = ccsds_profile_path(); !p.empty()) ccsds_ok = codec.load(p);

    std::cout << color::header("=== XDOS Payload Simulator ===") << "\n"
              << "  bus    : Payload Server TCP\n"
              << "  listen : " << color::addr("127.0.0.1:" + std::to_string(port)) << "\n"
              << "  frame  : [3b Type | 10b Command ID | 11b Length] + Data\n"
              << "  reply  : single-character ACK (no telemetry)\n"
              << "  ccsds  : " << (ccsds_ok ? color::ok("profile loaded (APID 5 unwrap + CRC check)")
                                            : color::warn("profile not found — raw frames only")) << "\n"
              << "  Ctrl-C to stop.\n\n";

    std::mutex stdout_mu;
    transport::TcpServer srv;
    bool ok = srv.start("127.0.0.1", port,
        [&stdout_mu, &codec, ccsds_ok, quiet](int fd, const std::string& peer) {
            if (!quiet) {
                std::lock_guard<std::mutex> g(stdout_mu);
                std::cout << color::hint("[Payload] client connected ") << peer << "\n";
            }
            Bytes buf;
            while (true) {
                auto chunk = transport::fd_recv_blocking(fd, 8192);
                if (chunk.empty()) break;
                buf.insert(buf.end(), chunk.begin(), chunk.end());

                // Drain every complete frame sitting in the buffer.
                while (true) {
                    std::size_t need = head_frame_size(buf);
                    if (need == 0 || buf.size() < need) break;

                    Bytes frame(buf.begin(), buf.begin() + need);
                    buf.erase(buf.begin(), buf.begin() + need);

                    bool was_ccsds = (frame[0] == kCcsdsSof0 && frame[1] == kCcsdsSof1);
                    Bytes inner = frame;
                    std::uint16_t tc_id = 0;

                    if (was_ccsds) {
                        if (!ccsds_ok) {
                            std::lock_guard<std::mutex> g(stdout_mu);
                            std::cout << color::err(
                                "[Payload] CCSDS frame received but no profile loaded\n");
                            continue;
                        }
                        auto dec = codec.decode(frame);
                        if (!dec) {
                            std::lock_guard<std::mutex> g(stdout_mu);
                            std::cout << color::err("[Payload] CCSDS decode failed\n");
                            continue;
                        }
                        if (!dec->crc_ok) {
                            std::lock_guard<std::mutex> g(stdout_mu);
                            std::cout << color::err("[Payload] CCSDS CRC-16 mismatch — dropped\n");
                            continue;
                        }
                        inner = dec->tc_data;
                        tc_id = dec->tc_id;
                    }

                    auto d = pf::decode(inner);
                    if (!d.header_ok) {
                        std::lock_guard<std::mutex> g(stdout_mu);
                        std::cout << color::err("[Payload] frame shorter than the 3-byte header\n");
                        continue;
                    }
                    if (was_ccsds && tc_id != d.command_id) {
                        std::lock_guard<std::mutex> g(stdout_mu);
                        std::cout << color::warn(
                            "[Payload] CCSDS TC_ID " + std::to_string(tc_id)
                            + " != payload Command ID " + std::to_string(d.command_id) + "\n");
                    }

                    Byte ack = handle_frame(d, stdout_mu);
                    Bytes reply = pf::encode(pf::MsgType::Telemetry, d.command_id,
                                             Bytes{ ack });
                    if (was_ccsds)
                        reply = codec.encode(Subsystem::Payload, ccsds::CcsdsDirection::TM,
                                             d.command_id, reply);
                    transport::fd_send(fd, reply);
                }
            }
            if (!quiet) {
                std::lock_guard<std::mutex> g(stdout_mu);
                std::cout << color::hint("[Payload] client disconnected ") << peer << "\n";
            }
        });
    if (!ok) {
        std::cerr << color::err("Payload sim: failed to bind port "
                                + std::to_string(port)) << "\n";
        return 1;
    }

    while (!g_stop) std::this_thread::sleep_for(milliseconds(200));
    std::cout << "\nPayload sim shutting down...\n";
    srv.stop();
    return 0;
}
