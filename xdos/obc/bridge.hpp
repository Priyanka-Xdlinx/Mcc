// SPDX-License-Identifier: Apache-2.0
// OBC bridge — routes frames between xdos_gs and subsystem sims.
//
// Wire format on every link: native subsystem framing (no CCSDS yet).
//
//   GS  ──(EPS  frame: 0x00 base64 0x01)──► OBC ──(same)──► EPS  sim 10010
//   GS  ──(ADCS frame: 0x1F 0x7F ...)─────► OBC ──(same)──► ADCS sim 10020
//   GS  ──(GPS  frame: 0xAA 0x44 0x12 ...)─► OBC ──(same)──► GPS  sim 10030
//
// The bridge sniffs the first 1-3 bytes of each frame to identify the
// subsystem.  When CCSDS lands later, this same module gains a CCSDS
// unwrap step and uses the inner-packet APID to route instead.

#ifndef XDOS_OBC_BRIDGE_HPP
#define XDOS_OBC_BRIDGE_HPP

#include "common/types.hpp"
#include "codec/ccsds_codec.hpp"
#include "codec/ccsds_tm_codec.hpp"
#include "transport/tcp_transport.hpp"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace xdos::obc {

struct BridgeStats {
    uint64_t frames_in  = 0;
    uint64_t frames_out = 0;
    uint64_t routed[7]   {0,0,0,0,0,0,0}; // by Subsystem index (ADCS/EPS/GPS/Payload/XLINK-S/UHF/SENSOR)
    uint64_t unrecognised = 0;
    uint64_t ccsds_in     = 0;      // frames that arrived CCSDS-wrapped
    uint64_t ccsds_out    = 0;      // frames sent back CCSDS-wrapped
    uint64_t ccsds_bad    = 0;      // CCSDS frames that failed decode/CRC
};

class Bridge {
public:
    struct Endpoint { std::string ip; int port; };
    using EndpointMap = std::map<Subsystem, Endpoint>;

    explicit Bridge(EndpointMap subsystems);
    ~Bridge();

    // Start listening on the GS-facing port.  Subsystem connections are
    // opened on demand (first frame for that subsystem).
    bool start(const std::string& bind_ip, int port);
    void stop();

    // Attach a CCSDS codec.  When attached, incoming frames are checked for
    // CCSDS SOF; if found, they're unwrapped before forwarding to sim,
    // and sim responses are wrapped back before returning to GS.
    void set_ccsds_codec(ccsds::CcsdsCodec* c) { ccsds_ = c; }

    // Attach the TM downlink codec.  When set AND use_tm_frames(true), CCSDS
    // responses are wrapped in the fixed 1432-byte TM frame (SOF FCA1) instead
    // of the symmetric TC-style frame.  The native subsystem TM bytes go in
    // TM DATA and the originating TC is echoed in the GS TC cmd field.
    void set_tm_codec(ccsds::CcsdsTmCodec* c) { tm_ = c; }
    void use_tm_frames(bool on) { tm_frames_.store(on); }
    bool tm_frames() const { return tm_frames_.load(); }

    // ── Flight power model hooks ────────────────────────────────────────────
    // While disabled (OBC unpowered / booting) every GS frame is dropped —
    // except EGSE umbilical frames, which are answered by the handler below
    // so the ground station can always see the OBC's power state.
    void set_enabled(bool e) { enabled_.store(e); }
    bool enabled() const     { return enabled_.load(); }

    using EgseHandler = std::function<Bytes(const Bytes&)>;
    void set_egse_handler(EgseHandler h) { egse_handler_ = std::move(h); }

    // Frame observer — called on every frame from GS, BEFORE any CCSDS
    // unwrap.  Used by the validator (--validate flag) to print structured
    // reports.  Set to nullptr to disable.
    using FrameObserver = std::function<void(const Bytes& wire)>;
    void set_frame_observer(FrameObserver obs) { observer_ = std::move(obs); }

    // TM emit observer — called with each TM downlink frame the OBC produces,
    // BEFORE it is sent to the GS.  Used by --validate / --validate-tm.
    using TmObserver = std::function<void(const Bytes& wire, Subsystem sub)>;
    void set_tm_observer(TmObserver obs) { tm_observer_ = std::move(obs); }

    BridgeStats stats() const;

    // Identify which subsystem a frame is destined for by sniffing prefix.
    static std::optional<Subsystem> sniff_subsystem(const Bytes& frame);

    // Log entries — last N frame events for the "watch traffic" CLI.
    struct LogEntry {
        std::string when;        // hh:mm:ss
        std::string direction;   // "GS→ADCS", "ADCS→GS", etc.
        size_t      size;
        std::string preview;     // first 24 hex bytes, lowercase
    };
    std::vector<LogEntry> recent_log(size_t n = 30) const;

private:
    void handle_gs_client(int fd, const std::string& peer);
    bool ensure_connected(Subsystem s);
    void log_frame(const std::string& dir, const Bytes& frame);

    EndpointMap                       endpoints_;
    transport::TcpServer              srv_;
    std::map<Subsystem, std::unique_ptr<transport::TcpClient>> downlinks_;
    mutable std::mutex                m_;
    BridgeStats                       stats_;
    std::vector<LogEntry>             log_;
    static constexpr size_t           kLogCap = 200;
    std::atomic<bool>                 running_ { false };
    ccsds::CcsdsCodec*                ccsds_ = nullptr;
    ccsds::CcsdsTmCodec*              tm_    = nullptr;
    std::atomic<bool>                 tm_frames_ { false };
    std::mutex                        echo_mu_;
    std::map<Subsystem, Bytes>        last_tc_;   // per-subsystem TC echo
    FrameObserver                     observer_;
    TmObserver                        tm_observer_;
    std::atomic<bool>                 enabled_ { true };
    EgseHandler                       egse_handler_;
};

} // namespace xdos::obc

#endif
