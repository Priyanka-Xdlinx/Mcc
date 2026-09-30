// SPDX-License-Identifier: Apache-2.0
// XDOS Ground Station — POSIX TCP transport.
//
// Two roles in one header for the level-1 design:
//   TcpClient — what the ground station uses to connect to a bus simulator
//   TcpServer — what the bus simulator uses, one per subsystem port
//
// Bytes go on the wire as-is.  Subsystem-native framing (EPS / ADCS / GPS)
// is applied by the caller before send / after receive.
#pragma once

#include "common/types.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace xdos::transport {

class TcpClient {
public:
    TcpClient() = default;
    ~TcpClient() { close(); }
    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    bool connect(const std::string& host, int port, int timeout_ms = 2000);
    bool send(const Bytes& bytes);

    // Blocking read of up to `max` bytes, returns when at least one byte
    // is available or timeout / connection-loss.
    Bytes recv(std::size_t max = 4096, int timeout_ms = 1000);

    void close();
    bool connected() const { return fd_ >= 0; }

private:
    int        fd_ = -1;
    std::mutex tx_mu_;
};

class TcpServer {
public:
    using ConnHandler = std::function<void(int /*fd*/, const std::string& /*peer*/)>;

    TcpServer() = default;
    ~TcpServer() { stop(); }
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    bool start(const std::string& host, int port, ConnHandler on_conn);
    void stop();

private:
    int                       fd_       = -1;
    std::atomic<bool>         running_  {false};
    std::thread               accept_t_;
    ConnHandler               on_conn_;
};

// Helpers usable from inside ConnHandler — operate on a raw fd.
bool fd_send(int fd, const Bytes& bytes);
Bytes fd_recv(int fd, std::size_t max = 4096, int timeout_ms = 1000);

// Like fd_recv but treats timeouts as "keep waiting" rather than "give up".
// Returns empty only on real EOF (peer closed) or socket error — which is
// the only meaningful signal for a server's per-connection handler loop.
// Wakes briefly every `poll_ms` so a shutdown flag check can interrupt.
Bytes fd_recv_blocking(int fd, std::size_t max = 4096, int poll_ms = 500);

} // namespace xdos::transport
