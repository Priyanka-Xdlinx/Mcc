// SPDX-License-Identifier: Apache-2.0
#include "cli/mcc_forward.hpp"

#include <algorithm>
#include <cstring>

namespace xdos::cli {

static void put_u32_be(Bytes& b, std::uint32_t v) {
    b.push_back((Byte)((v >> 24) & 0xFF));
    b.push_back((Byte)((v >> 16) & 0xFF));
    b.push_back((Byte)((v >>  8) & 0xFF));
    b.push_back((Byte)( v        & 0xFF));
}
static void put_u16_be(Bytes& b, std::uint16_t v) {
    b.push_back((Byte)((v >> 8) & 0xFF));
    b.push_back((Byte)( v       & 0xFF));
}

Bytes MccForwarder::frame(std::uint32_t mf_id, int slot_1based,
                          const SubframeTime& ts, const Bytes& payload) const {
    Bytes out;
    out.reserve(kMccHeaderSize + payload.size());
    out.insert(out.end(), kMccMagic, kMccMagic + 4);   // 0
    out.push_back(kMccVersion);                        // 4
    out.push_back(0);                                  // 5  reserved
    put_u32_be(out, mf_id);                            // 6
    put_u16_be(out, (std::uint16_t)slot_1based);       // 10
    put_u16_be(out, ts.year);                          // 12
    out.push_back(ts.month);                           // 14
    out.push_back(ts.day);                             // 15
    out.push_back(ts.hour);                            // 16
    out.push_back(ts.minute);                          // 17
    out.push_back(ts.second);                          // 18
    put_u16_be(out, ts.ms);                            // 19
    out.push_back(0);                                  // 21 reserved
    put_u32_be(out, (std::uint32_t)payload.size());    // 22
    out.insert(out.end(), payload.begin(), payload.end());   // 26
    return out;
}

bool MccForwarder::listen(const std::string& host, int port, std::string* err) {
    if (mode_ != Mode::Off) { if (err) *err = "forwarder already started"; return false; }
    running_.store(true);
    // Each accepted MCC client just sits in the list; it is a one-way feed,
    // so the handler only needs to notice the peer going away.
    const bool ok = srv_.start(host, port, [this](int cfd, const std::string&) {
        {
            std::lock_guard<std::mutex> g(clients_mu_);
            // Defensive: never list the same descriptor twice.
            if (std::find(clients_.begin(), clients_.end(), cfd) == clients_.end())
                clients_.push_back(cfd);
        }
        while (running_.load()) {
            // Empty result means real EOF/error — the peer has gone.
            if (transport::fd_recv_blocking(cfd, 16, 500).empty()) break;
        }
        // MUST drop the descriptor here.  TcpServer closes cfd as soon as this
        // handler returns, and the OS is free to hand the same fd NUMBER to the
        // next client that connects.  Leaving it listed would make that later
        // client appear twice and receive every subframe twice — which is
        // exactly what a readiness probe connecting and disconnecting caused.
        std::lock_guard<std::mutex> g(clients_mu_);
        clients_.erase(std::remove(clients_.begin(), clients_.end(), cfd),
                       clients_.end());
    });
    if (!ok) {
        running_.store(false);
        if (err) *err = "cannot listen on " + host + ":" + std::to_string(port);
        return false;
    }
    mode_     = Mode::Listen;
    endpoint_ = host + ":" + std::to_string(port) + " (listening)";
    return true;
}

bool MccForwarder::connect(const std::string& host, int port, std::string* err) {
    if (mode_ != Mode::Off) { if (err) *err = "forwarder already started"; return false; }
    host_ = host; port_ = port;
    mode_ = Mode::Connect;
    endpoint_ = host + ":" + std::to_string(port) + " (dial-out)";
    std::lock_guard<std::mutex> g(cli_mu_);
    if (!cli_.connect(host, port, 2000) && err)
        *err = "not connected yet — will retry on each subframe";
    return true;    // not fatal: send() retries
}

void MccForwarder::send(std::uint32_t mf_id, int slot_1based,
                        const SubframeTime& ts, const Bytes& payload) {
    if (mode_ == Mode::Off || payload.empty()) return;
    const Bytes msg = frame(mf_id, slot_1based, ts, payload);

    if (mode_ == Mode::Listen) {
        std::lock_guard<std::mutex> g(clients_mu_);
        bool any = false;
        for (auto it = clients_.begin(); it != clients_.end();) {
            if (transport::fd_send(*it, msg)) { any = true; ++it; }
            else it = clients_.erase(it);     // peer gone — stop feeding it
        }
        any ? (void)++sent_ : (void)++dropped_;
        return;
    }

    std::lock_guard<std::mutex> g(cli_mu_);
    if (!cli_.connected() && !cli_.connect(host_, port_, 500)) { ++dropped_; return; }
    if (cli_.send(msg)) { ++sent_; return; }
    cli_.close();                              // force a reconnect next time
    ++dropped_;
}

void MccForwarder::stop() {
    if (mode_ == Mode::Off) return;
    running_.store(false);
    if (mode_ == Mode::Listen) srv_.stop();
    else { std::lock_guard<std::mutex> g(cli_mu_); cli_.close(); }
    mode_ = Mode::Off;
}

bool MccForwarder::linked() const {
    if (mode_ == Mode::Listen) return peers() > 0;
    if (mode_ == Mode::Connect) {
        std::lock_guard<std::mutex> g(cli_mu_);
        return cli_.connected();
    }
    return false;
}

int MccForwarder::peers() const {
    if (mode_ != Mode::Listen) return linked() ? 1 : 0;
    std::lock_guard<std::mutex> g(clients_mu_);
    return (int)clients_.size();
}

}  // namespace xdos::cli
