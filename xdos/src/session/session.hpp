// SPDX-License-Identifier: Apache-2.0
// XDOS Ground Station — bus-per-subsystem session.
//
// Level-1 model: the ground station talks directly to each subsystem's bus.
// No OBC, no CCSDS — one TCP connection per subsystem, each speaking that
// subsystem's native framing:
//
//   ADCS bus  TCP port 10020   CubeSpace UART framing (0x1F 0x7F ... 0x1F 0xFF)
//   EPS  bus  TCP port 10010   RS485 frame -> Base64 -> 0x00 ... 0x01
//   GPS  bus  TCP port 10030   OEM7 binary (AA 44 12 ... CRC32)
//
// The ground station looks up the message in the MIB, encodes the payload
// via the codec, applies the right native framing, sends, and decodes the
// response.
#pragma once

#include "common/types.hpp"
#include "codec/ccsds_codec.hpp"
#include "codec/ccsds_tm_codec.hpp"
#include "mib/mib.hpp"
#include "transport/tcp_transport.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <map>
#include <string>

namespace xdos::session {

struct BusEndpoint { std::string host = "127.0.0.1"; int port = 0; };

struct ExchangeResult {
    bool        ok        = false;
    std::string error;
    Bytes       tx_frame;             // bytes that went on the wire (CCSDS-wrapped if enabled)
    Bytes       rx_frame;             // bytes received on the wire
    Bytes       tx_native;            // native subsystem frame (pre-CCSDS, if applicable)
    Bytes       rx_native;            // native subsystem frame (post-CCSDS unwrap, if applicable)
    std::optional<ccsds::DecodedTmFrame> tm_frame;   // set when RX was a TM downlink frame (SOF FCA1)
    Bytes       tx_payload;           // pre-framing payload that we encoded
    Bytes       rx_payload;           // payload from the response (post de-framing)
    int         response_msg_id = -1; // msg_id of the response (if any)
    // The MIB message the response was decoded against.  Usually the TM at
    // response_msg_id, but a sensor-controller NACK resolves to the EVENT at
    // that same id — the CLI needs to know which, to label the fields.
    const mib::Message* response_msg = nullptr;
    nlohmann::json sent_values;
    nlohmann::json decoded_values;
    std::chrono::milliseconds rtt{0};

    // CCSDS framing info (only valid if ccsds_used == true)
    bool                                ccsds_used = false;
    std::optional<ccsds::DecodedFrame>  ccsds_tx;   // outgoing CCSDS header info
    std::optional<ccsds::DecodedFrame>  ccsds_rx;   // incoming CCSDS header info
};

class Session {
public:
    explicit Session(const mib::Mib& m) : mib_(m) {}

    const mib::Mib& mib() const { return mib_; }

    // Single-link (via-OBC) mode: all subsystem traffic multiplexes over one
    // TCP connection to the OBC; subsystem identity travels as the APID.
    void set_shared_link(bool on) { shared_link_ = on; }
    bool shared_link() const      { return shared_link_; }

    void configure(Subsystem s, BusEndpoint ep) { eps_[idx(s)] = ep; }
    bool connect(Subsystem s);
    void disconnect_all();
    bool connected(Subsystem s) const;

    // Send a TC by name and wait for the response telemetry (if any).
    // `values` is a json object of param-name -> value.
    // For EPS, `node` selects the board ("PDU"/"BMS"/"MPPT").  For ADCS/GPS
    // pass std::nullopt.
    ExchangeResult send_tc(Subsystem s,
                           const std::string& tc_name,
                           const nlohmann::json& values,
                           std::optional<std::string> node = std::nullopt,
                           int timeout_ms = 15000);

    // Request a telemetry value (CubeSpace-style: empty-payload TC frame
    // whose msg_id is the TM's id; receiver replies with that TM).  Used by
    // ADCS where each TM is operationally a GET TC.  For GPS, wrap as LOG
    // instead — that's a different wire idiom.
    ExchangeResult send_tm_request(Subsystem s,
                                   const std::string& tm_name,
                                   std::optional<std::string> node = std::nullopt,
                                   int timeout_ms = 15000);

    // Send a raw byte payload to a subsystem (used by Payload Server where
    // the operator types hex directly — no MIB framing).  If CCSDS is on,
    // the raw bytes go into tc_data; tc_id is provided by the caller.
    // Used for Subsystem::Payload — CCSDS-only path.
    ExchangeResult send_raw(Subsystem s,
                            std::uint16_t tc_id,
                            const Bytes& raw_payload,
                            int timeout_ms = 15000);

    // Send a TC whose data field the caller has already built.  Needed for
    // Payload commands with variable-length trailing fields (operation name,
    // arguments, capture parameters) that the fixed-offset MIB codec cannot
    // express.  Native framing and the optional CCSDS wrap are applied as
    // usual, and the response is decoded against the MIB the normal way.
    ExchangeResult send_tc_data(Subsystem s,
                                const std::string& name,
                                const Bytes& data,
                                int timeout_ms = 15000);

    // XLink Service Server dedicated send path.  Encodes with 16-byte product
    // key + 4-byte service_event (GET or SET flag), packs the data block using
    // U32-word LE bit-packing, sends over the XLink TCP connection, decodes the
    // response.  `is_get` selects between 0x10000000 (GET) and 0x20000000 (SET)
    // flags on service_event.  Product key must have been set via set_xlink_key.
    ExchangeResult send_xlink_tc(const std::string& tc_name,
                                 const nlohmann::json& values,
                                 bool is_get,
                                 int timeout_ms = 15000);

    // Set the XLink 16-byte product key.  Any subsequent send_xlink_tc will
    // use this key.  In simulator mode the key is not authenticated but is
    // still echoed back in responses.
    void set_xlink_key(const std::array<std::uint8_t, 16>& key) { xlink_key_ = key; }
    const std::array<std::uint8_t, 16>& xlink_key() const { return xlink_key_; }

    // ── CCSDS framing toggle ───────────────────────────────────────────────
    // Attach a CCSDS codec.  When attached and enabled, every TC is wrapped
    // in a CCSDS TC frame before sending and every TM is unwrapped on receive.
    // Set to nullptr to detach (raw native framing only).
    void set_ccsds_codec(ccsds::CcsdsCodec* c) { ccsds_ = c; }
    void set_tm_codec(ccsds::CcsdsTmCodec* c) { tm_ = c; }
    void set_ccsds_enabled(bool on)            { ccsds_on_ = on; }
    bool ccsds_enabled()                 const { return ccsds_on_ && ccsds_ != nullptr; }
    ccsds::CcsdsCodec* ccsds_codec()           { return ccsds_; }

    // ── ADCS bare-body telecommands ────────────────────────────────────────
    // The flight OBC adds the CubeSpace delimiters itself: it wraps the ADCS
    // TC data field in 1F 7F … 1F FF before putting it on the ADCS UART.  So
    // against real hardware the ground must send the bare escaped body
    // (msg_id || data) — sending a full frame would give the ADCS a doubled
    // SOM/EOM.  Off by default, which keeps every simulator path (the ADCS
    // sim direct, and the OBC sim, neither of which adds delimiters) sending
    // complete frames exactly as before.  Affects TX only: telemetry still
    // comes back fully framed and is decoded unchanged.
    void set_adcs_bare_tc(bool on)             { adcs_bare_tc_ = on; }
    bool adcs_bare_tc()                  const { return adcs_bare_tc_; }

private:
    // id_only: this is a GET, so the message id alone identifies the request.
    // In ADCS bare mode that means an empty native body — see wrap_native.
    ExchangeResult& transact(Subsystem s, int wire_id, int dest_addr,
                             std::optional<std::string_view> nv,
                             int timeout_ms, ExchangeResult& r,
                             bool id_only = false);

    // ── shared-link (via-OBC) mode ─────────────────────────────────────────
    // One physical link: GS ↔ OBC.  Subsystems are APIDs inside the frames,
    // not separate buses.  When shared_link_ is set, every subsystem maps to
    // the single OBC connection (slot kObcIdx); per-subsystem sockets exist
    // only in --no-obc simulator mode.
    static constexpr int kObcIdx = -1;
    int idx(Subsystem s) const {
        return shared_link_ ? kObcIdx : static_cast<int>(s);
    }
    bool shared_link_ = false;

    const mib::Mib& mib_;
    std::map<int, BusEndpoint>  eps_;
    std::map<int, transport::TcpClient> conns_;

    // CCSDS codec (optional). Owned by the caller; Session only borrows.
    ccsds::CcsdsCodec*   ccsds_ = nullptr;
    ccsds::CcsdsTmCodec* tm_    = nullptr;
    bool               ccsds_on_ = false;

    // Send ADCS TCs as a bare escaped body (no 1F 7F / 1F FF) — real OBC only.
    bool           adcs_bare_tc_ = false;

    // XLink Service Server 16-byte product key. Zero-initialised by default;
    // the sim accepts anything, real hardware requires the correct per-device key.
    std::array<std::uint8_t, 16> xlink_key_ {};
};

} // namespace xdos::session
