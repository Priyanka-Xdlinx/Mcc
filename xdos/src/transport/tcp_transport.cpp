// SPDX-License-Identifier: Apache-2.0
// XDOS Ground Station — cross-platform TCP transport.
//
// Builds unchanged on Linux (POSIX sockets) and Windows (Winsock 2).
#include "transport/tcp_transport.hpp"
#include "common/socket_compat.hpp"

#include <cstring>
#include <iostream>

namespace xdos::transport {

namespace net = xdos::net;

// ─── helpers ────────────────────────────────────────────────────────────────
static bool set_recv_timeout(int fd, int timeout_ms) {
#ifdef _WIN32
    DWORD tv = static_cast<DWORD>(timeout_ms);
    return ::setsockopt(static_cast<net::socket_t>(fd), SOL_SOCKET, SO_RCVTIMEO,
                        reinterpret_cast<const char*>(&tv), sizeof(tv)) == 0;
#else
    timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
#endif
}

bool fd_send(int fd, const Bytes& bytes) {
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        auto n = ::send(static_cast<net::socket_t>(fd),
                        reinterpret_cast<const char*>(bytes.data() + sent),
                        static_cast<int>(bytes.size() - sent),
                        MSG_NOSIGNAL);
        if (n <= 0) {
            int err = net::last_error();
            if (n < 0 && (net::was_interrupted(err) || net::would_block(err))) continue;
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

Bytes fd_recv(int fd, std::size_t max, int timeout_ms) {
    Bytes buf(max);
    set_recv_timeout(fd, timeout_ms);
    auto n = ::recv(static_cast<net::socket_t>(fd),
                    reinterpret_cast<char*>(buf.data()),
                    static_cast<int>(buf.size()), 0);
    if (n <= 0) return {};
    buf.resize(static_cast<std::size_t>(n));
    return buf;
}

Bytes fd_recv_blocking(int fd, std::size_t max, int poll_ms) {
    Bytes buf(max);
    set_recv_timeout(fd, poll_ms);
    while (true) {
        auto n = ::recv(static_cast<net::socket_t>(fd),
                        reinterpret_cast<char*>(buf.data()),
                        static_cast<int>(buf.size()), 0);
        if (n > 0) {
            buf.resize(static_cast<std::size_t>(n));
            return buf;
        }
        if (n == 0) return {};                           // peer closed
        int err = net::last_error();
        if (net::would_block(err))    continue;          // idle timeout
        if (net::was_interrupted(err)) continue;         // signal
        return {};                                       // real socket error
    }
}

// ─── TcpClient ─────────────────────────────────────────────────────────────
bool TcpClient::connect(const std::string& host, int port, int timeout_ms) {
    close();
    auto s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (!net::valid(s)) return false;
    fd_ = static_cast<int>(s);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<std::uint16_t>(port));
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        if (::getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) {
            close(); return false;
        }
        addr.sin_addr = reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr;
        ::freeaddrinfo(res);
    }

    net::set_nonblocking(s, true);
    int rc = ::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc < 0) {
        int err = net::last_error();
        if (!net::in_progress(err)) { close(); return false; }

        fd_set wf; FD_ZERO(&wf); FD_SET(s, &wf);
        timeval tv{ timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
        rc = ::select(static_cast<int>(s) + 1, nullptr, &wf, nullptr, &tv);
        if (rc <= 0) { close(); return false; }

        int sockerr = 0;
        net::socklen_t_compat elen = sizeof(sockerr);
        ::getsockopt(s, SOL_SOCKET, SO_ERROR,
                     reinterpret_cast<char*>(&sockerr), &elen);
        if (sockerr) { close(); return false; }
    }
    net::set_nonblocking(s, false);
    int one = 1;
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
                 reinterpret_cast<const char*>(&one), sizeof(one));
    return true;
}

bool TcpClient::send(const Bytes& bytes) {
    if (fd_ < 0) return false;
    std::lock_guard<std::mutex> lk(tx_mu_);
    if (!fd_send(fd_, bytes)) { close(); return false; }
    return true;
}

Bytes TcpClient::recv(std::size_t max, int timeout_ms) {
    if (fd_ < 0) return {};
    return fd_recv(fd_, max, timeout_ms);
}

void TcpClient::close() {
    if (fd_ >= 0) {
        auto s = static_cast<net::socket_t>(fd_);
        ::shutdown(s, SHUT_RDWR);
        net::close_socket(s);
        fd_ = -1;
    }
}

// ─── TcpServer ─────────────────────────────────────────────────────────────
bool TcpServer::start(const std::string& host, int port, ConnHandler on_conn) {
    stop();
    auto s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (!net::valid(s)) return false;
    fd_ = static_cast<int>(s);

    int one = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&one), sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<std::uint16_t>(port));
    if (host == "0.0.0.0" || host.empty())
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    else
        ::inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind " << port << " failed: "
                  << net::last_error_str() << "\n";
        net::close_socket(s); fd_ = -1; return false;
    }
    if (::listen(s, 4) < 0) {
        net::close_socket(s); fd_ = -1; return false;
    }

    on_conn_  = std::move(on_conn);
    running_  = true;
    accept_t_ = std::thread([this]{
        auto srv = static_cast<net::socket_t>(fd_);
        while (running_) {
            sockaddr_in pa{}; net::socklen_t_compat pal = sizeof(pa);
            timeval tv{1, 0};
            fd_set rf; FD_ZERO(&rf); FD_SET(srv, &rf);
            int rc = ::select(static_cast<int>(srv) + 1,
                              &rf, nullptr, nullptr, &tv);
            if (rc <= 0) continue;
            auto cfd = ::accept(srv, reinterpret_cast<sockaddr*>(&pa), &pal);
            if (!net::valid(cfd)) continue;
            char ip[64];
            ::inet_ntop(AF_INET, &pa.sin_addr, ip, sizeof(ip));
            std::string peer = std::string(ip) + ":" +
                               std::to_string(ntohs(pa.sin_port));
            int int_cfd = static_cast<int>(cfd);
            std::thread([this, int_cfd, peer]{
                auto c = static_cast<net::socket_t>(int_cfd);
                try { on_conn_(int_cfd, peer); } catch (...) {}
                ::shutdown(c, SHUT_RDWR);
                net::close_socket(c);
            }).detach();
        }
    });
    return true;
}

void TcpServer::stop() {
    running_ = false;
    if (fd_ >= 0) {
        auto s = static_cast<net::socket_t>(fd_);
        ::shutdown(s, SHUT_RDWR);
        net::close_socket(s);
        fd_ = -1;
    }
    if (accept_t_.joinable()) accept_t_.join();
}

} // namespace xdos::transport
