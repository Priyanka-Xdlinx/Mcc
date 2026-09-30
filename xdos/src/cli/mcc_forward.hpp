// SPDX-License-Identifier: Apache-2.0
// MCC subframe forwarder (GS side).
//
// Forwards every received rate-group subframe's PAYLOAD to the MCC software
// over TCP, tagged with where it sits in the downlink:
//
//   header = master frame number + subframe number, then the payload bytes
//
// Wire format — big-endian, matching the "XLUT" uplink convention already
// used between the GS and the OBC:
//
//   off  size  field
//   0    4     magic "XMCC"
//   4    1     format version (= 1)
//   5    1     reserved (= 0)
//   6    4     master frame number  (u32) — the OBC's SFC / 16
//   10   2     subframe number      (u16) — 1..16 within that master frame
//   12   2     timestamp: year      (u16)
//   14   1     timestamp: month     1..12
//   15   1     timestamp: day       1..31
//   16   1     timestamp: hour      0..23
//   17   1     timestamp: minute    0..59
//   18   1     timestamp: second    0..59
//   19   2     timestamp: millisec  (u16) 0..999
//   21   1     reserved (= 0)
//   22   4     payload length       (u32) — bytes that follow (1280)
//   26   N     payload              — the subframe's TM DATA field
//
// Total 26 + 1280 = 1306 bytes per subframe at the standard subframe size.
//
// The timestamp is the subframe's own CCS Time Code, copied field for field
// out of the TM header — NOT a ground receive time and NOT converted to an
// epoch.  Passing it through verbatim keeps it free of timezone and leap
// handling, so MCC sees exactly the time the spacecraft stamped.
//
// The magic and length are not decoration: TCP is a byte stream with no
// message boundaries, so without them the MCC side cannot frame the feed or
// resynchronise after a partial read.  The payload is the subframe's data
// field ONLY — no CCSDS TM header, no CRC — i.e. exactly the 1280 bytes the
// binary LUT partitions as VH[0:400] H[400:800] M[800:1100] L[1100:1275].
//
// Both roles are supported because either side may own the socket:
//   listen()  — GS accepts; MCC connects in (MCC is the TCP client)
//   connect() — GS dials out; MCC is listening (MCC is the TCP server)

#ifndef XDOS_CLI_MCC_FORWARD_HPP
#define XDOS_CLI_MCC_FORWARD_HPP

#include "common/types.hpp"
#include "transport/tcp_transport.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace xdos::cli {

// Header laid out above; exposed so tests and the MCC side agree on sizes.
constexpr char          kMccMagic[4]    = { 'X', 'M', 'C', 'C' };
constexpr std::uint8_t  kMccVersion     = 1;
constexpr std::size_t   kMccHeaderSize  = 26;

// The subframe's CCS Time Code, passed through as-is from the TM header.
struct SubframeTime {
    std::uint16_t year   = 0;
    std::uint8_t  month  = 0, day = 0, hour = 0, minute = 0, second = 0;
    std::uint16_t ms     = 0;
};

class MccForwarder {
public:
    ~MccForwarder() { stop(); }

    // Accept MCC connections on host:port.  Every connected client receives
    // every subframe; a client that stops reading is dropped on send failure.
    bool listen(const std::string& host, int port, std::string* err = nullptr);

    // Dial out to an MCC that is listening.  Reconnects on the next subframe
    // after a failure, so the feed recovers by itself.
    bool connect(const std::string& host, int port, std::string* err = nullptr);

    // Frame and send one subframe.  slot_1based is 1..16, as displayed.
    // Never throws and never blocks the caller on a dead peer.
    void send(std::uint32_t mf_id, int slot_1based, const SubframeTime& ts,
              const Bytes& payload);

    void stop();

    bool          active()   const { return mode_ != Mode::Off; }
    bool          linked()   const;           // at least one live peer
    int           peers()    const;
    std::uint64_t sent()     const { return sent_.load(); }
    std::uint64_t dropped()  const { return dropped_.load(); }
    std::string   endpoint() const { return endpoint_; }

private:
    enum class Mode { Off, Listen, Connect };

    Bytes frame(std::uint32_t mf_id, int slot_1based, const SubframeTime& ts,
                const Bytes& payload) const;

    Mode                 mode_ = Mode::Off;
    std::string          endpoint_;

    // Listen mode
    transport::TcpServer srv_;
    mutable std::mutex   clients_mu_;
    std::vector<int>     clients_;
    std::atomic<bool>    running_ { false };

    // Connect mode
    std::string          host_;
    int                  port_ = 0;
    mutable std::mutex   cli_mu_;
    transport::TcpClient cli_;

    std::atomic<std::uint64_t> sent_    { 0 };
    std::atomic<std::uint64_t> dropped_ { 0 };
};

}  // namespace xdos::cli
#endif
