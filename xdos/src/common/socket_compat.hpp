// SPDX-License-Identifier: Apache-2.0
// XDOS Ground Station — cross-platform socket layer.
//
// One header that papers over the differences between POSIX sockets (Linux,
// macOS, BSD) and Winsock (Windows).  Use this everywhere instead of pulling
// in <sys/socket.h> or <winsock2.h> directly.
//
//   socket_t              — POSIX `int` or Windows `SOCKET` (an unsigned)
//   xd_invalid_socket()   — −1 / INVALID_SOCKET sentinel
//   xd_close_socket(fd)   — close(fd) / closesocket(fd)
//   xd_socket_error()     — errno / WSAGetLastError()
//   xd_set_nonblocking()  — fcntl O_NONBLOCK / ioctlsocket FIONBIO
//   net::Startup          — RAII; calls WSAStartup/WSACleanup on Windows
//
// Call `xdos::net::Startup _net_init;` near the top of every main() that uses
// TCP — that's the minimum needed for Winsock to initialise.  On POSIX it
// compiles to nothing.

#pragma once

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX                          // keep std::min/std::max working
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <io.h>                             // _isatty, _fileno
  #include <cstdio>
#else
  #include <arpa/inet.h>
  #include <fcntl.h>
  #include <netdb.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <sys/select.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
  #include <cerrno>
#endif

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace xdos::net {

#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
using socklen_t_compat = int;
#else
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
using socklen_t_compat = socklen_t;
#endif

inline bool valid(socket_t s) {
#ifdef _WIN32
    return s != INVALID_SOCKET;
#else
    return s >= 0;
#endif
}

inline int close_socket(socket_t s) {
#ifdef _WIN32
    return ::closesocket(s);
#else
    return ::close(s);
#endif
}

inline int last_error() {
#ifdef _WIN32
    return ::WSAGetLastError();
#else
    return errno;
#endif
}

inline std::string last_error_str() {
#ifdef _WIN32
    char buf[256] = {0};
    int err = ::WSAGetLastError();
    ::FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                     nullptr, err, 0, buf, sizeof(buf), nullptr);
    // Strip trailing CRLF
    for (int i = static_cast<int>(sizeof(buf)) - 1; i >= 0; --i) {
        if (buf[i] == '\r' || buf[i] == '\n' || buf[i] == 0) buf[i] = 0;
        else break;
    }
    return std::string(buf);
#else
    return std::string(std::strerror(errno));
#endif
}

inline bool would_block(int err) {
#ifdef _WIN32
    // Winsock returns WSAETIMEDOUT (not WSAEWOULDBLOCK) when SO_RCVTIMEO
    // expires on a blocking recv.  Treat both as "idle, try again".
    return err == WSAEWOULDBLOCK || err == WSAETIMEDOUT;
#else
    return err == EAGAIN || err == EWOULDBLOCK;
#endif
}

inline bool was_interrupted(int err) {
#ifdef _WIN32
    return err == WSAEINTR;
#else
    return err == EINTR;
#endif
}

inline bool in_progress(int err) {
#ifdef _WIN32
    return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS;
#else
    return err == EINPROGRESS;
#endif
}

inline bool set_nonblocking(socket_t s, bool nb) {
#ifdef _WIN32
    u_long m = nb ? 1 : 0;
    return ::ioctlsocket(s, FIONBIO, &m) == 0;
#else
    int flags = ::fcntl(s, F_GETFL, 0);
    if (flags < 0) return false;
    if (nb) flags |=  O_NONBLOCK;
    else    flags &= ~O_NONBLOCK;
    return ::fcntl(s, F_SETFL, flags) == 0;
#endif
}

inline bool isatty_stdin() {
#ifdef _WIN32
    return ::_isatty(::_fileno(stdin)) != 0;
#else
    return ::isatty(::fileno(stdin)) != 0;
#endif
}

// RAII helper — instantiate at the top of every main() that uses TCP.
// On Windows, also fixes two long-standing console quirks:
//   1. Sets the console code page to UTF-8 so box-drawing characters and
//      arrows in our banners render correctly (default is CP1252 / OEM).
//   2. Enables virtual-terminal processing so ANSI color escapes work in
//      both new Windows Terminal and legacy ConHost.
// On POSIX it's a no-op.
class Startup {
public:
    Startup() {
#ifdef _WIN32
        WSADATA d;
        int rc = ::WSAStartup(MAKEWORD(2, 2), &d);
        if (rc != 0) throw std::runtime_error("WSAStartup failed: code " + std::to_string(rc));
        ok_ = true;

        // Console code page → UTF-8 so multi-byte chars in banners render right.
        ::SetConsoleOutputCP(CP_UTF8);
        ::SetConsoleCP(CP_UTF8);

        // Enable virtual-terminal processing for ANSI colors / cursor control.
        HANDLE hOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut != INVALID_HANDLE_VALUE) {
            DWORD mode = 0;
            if (::GetConsoleMode(hOut, &mode)) {
                ::SetConsoleMode(hOut, mode | 0x0004 /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */);
            }
        }
#endif
    }
    ~Startup() {
#ifdef _WIN32
        if (ok_) ::WSACleanup();
#endif
    }
    Startup(const Startup&) = delete;
    Startup& operator=(const Startup&) = delete;
private:
#ifdef _WIN32
    bool ok_ = false;
#endif
};

} // namespace xdos::net

// Some POSIX flag names that Winsock either lacks or names differently.
// We default to 0 on Windows since the platform handles SIGPIPE automatically.
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#ifdef _WIN32
// POSIX shutdown(how) constants vs Winsock SD_*
#define SHUT_RD   SD_RECEIVE
#define SHUT_WR   SD_SEND
#define SHUT_RDWR SD_BOTH
#endif
