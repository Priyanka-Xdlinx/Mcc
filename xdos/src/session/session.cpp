// SPDX-License-Identifier: Apache-2.0
#include "session/session.hpp"
#include "codec/codec.hpp"
#include "framing/eps_framing.hpp"
#include "framing/adcs_framing.hpp"
#include "framing/gps_framing.hpp"
#include "framing/payload_framing.hpp"
#include "framing/uhf_framing.hpp"
#include "framing/sensor_framing.hpp"
#include "subsystems/xlink_codec.hpp"
#include "common/crc.hpp"

#include <algorithm>
#include <chrono>

namespace xdos::session {

bool Session::connect(Subsystem s) {
    auto it = eps_.find(idx(s));
    if (it == eps_.end() || it->second.port == 0) return false;
    auto& c = conns_[idx(s)];
    if (c.connected()) return true;          // shared link: already up
    return c.connect(it->second.host, it->second.port);
}

void Session::disconnect_all() {
    for (auto& kv : conns_) kv.second.close();
}

bool Session::connected(Subsystem s) const {
    auto it = conns_.find(idx(s));
    return it != conns_.end() && it->second.connected();
}

// ─── frame helpers ──────────────────────────────────────────────────────────
static Bytes wrap_native(Subsystem s, int wire_id, const Bytes& payload,
                         int dest_addr, bool adcs_bare, bool id_only) {
    switch (s) {
        case Subsystem::EPS:
            return framing::eps::encode(static_cast<Byte>(dest_addr),
                                        static_cast<Byte>(wire_id), payload);
        case Subsystem::ADCS:
            // The flight OBC builds the CubeSpace frame itself — delimiters
            // and 0x1F escaping both — so hand it the raw body.  Sims are
            // spoken to directly and get the complete frame (adcs_bare_tc()).
            //
            // A GET goes further: the OBC derives the whole request from the
            // message id, which the CCSDS layer already carries in tc_id, so
            // the data field is left empty and tc_len goes out as 0.  Sending
            // the id again here would only duplicate what tc_id says.
            if (!adcs_bare)
                return framing::adcs::encode(static_cast<Byte>(wire_id), payload);
            if (id_only) return {};
            return framing::adcs::raw_body(static_cast<Byte>(wire_id), payload);
        case Subsystem::GPS:
            return framing::gps::encode(static_cast<std::uint16_t>(wire_id), payload);
        case Subsystem::Payload:
            // Payload Server: 3-byte [Type|CommandID|Length] header, then data.
            // Ground always originates Telecommand (Type 0).
            return framing::payload::encode(
                       framing::payload::MsgType::Telecommand,
                       static_cast<std::uint16_t>(wire_id), payload);
        case Subsystem::UHF:
            // OpenLST ESP frame; wire_id is the command opcode.  HWID_LOCAL
            // is accepted by every radio; seqnum 0 (the reply echoes it).
            return framing::uhf::encode(static_cast<Byte>(wire_id), payload);
        case Subsystem::SENSOR:
            // Sensor controller: the ground builds the whole frame — SOF,
            // DEST-ID, CMD-ID, length and EOF — and the OBC carries it as
            // an opaque TC data field.  dest_addr is the node address from
            // the MIB (50); kDestId covers a MIB that omits the node.
            return framing::sensor::encode(
                       static_cast<Byte>(dest_addr ? dest_addr
                                                   : framing::sensor::kDestId),
                       static_cast<Byte>(wire_id), payload);
        default:
            break;   // XLINK bypasses this path (send_xlink_tc)
    }
    return {};
}

struct Unwrapped { int msg_id = -1; Bytes payload; bool framed_ok = false; bool crc_ok = true; };

// Recover the native subsystem frame from a TM-DATA field that was zero-padded
// to 1280 bytes.  Framing is self-describing, so trim the padding back to the
// true frame boundary per subsystem:
//   EPS      wrapped line frame (terminal 0x01), or the bare message array the
//            flight OBC produces — see the EPS block below
//   ADCS     trailer 0x1F 0xFF (the 0xFF is the last real byte)
//   GPS      NovAtel OEM7 binary: sync AA 44 12, header-len at [3],
//            payload-len (u16 LE) at [8], + 4-byte CRC32  → length is exact
//   Payload  3-byte header carries the exact data length
// If the structure isn't recognised, return unchanged.
static Bytes strip_tm_padding(Subsystem s, const Bytes& tm_data,
                              const mib::Mib* mib) {
    if (s == Subsystem::EPS) {
        // Wrapped line frame: the terminal 0x01 is the real boundary, and the
        // Base64 body in between can never contain a 0x01 itself.
        if (!tm_data.empty() && tm_data[0] == framing::eps::kStartFrame) {
            for (std::size_t i = tm_data.size(); i-- > 0; )
                if (tm_data[i] == framing::eps::kEndFrame)
                    return Bytes(tm_data.begin(), tm_data.begin() + i + 1);
            return tm_data;
        }
        // Bare message array (dest|src|msg_id|data|crc8).  Nothing in it
        // declares its own length, and a backwards scan for 0x01 would cut
        // through the data, so ask the MIB: byte 1 is the replying node's
        // address and byte 2 is the message id.
        if (tm_data.size() >= 4) {
            auto crc_ok_at = [&](std::size_t total) {
                return total >= 4 && total <= tm_data.size() &&
                       crc::crc8(tm_data.data(), total - 1) == tm_data[total - 1];
            };
            // The MIB's declared length is the fast path, but only the CRC-8
            // can confirm it.  Firmware that returns more data bytes than the
            // spec sheet documents is real — Get_PDU_CH_State answers with 4
            // where the sheet says 2 — and trusting the sheet blindly would
            // cut the array short and report a good reply as corrupt.
            if (mib) {
                auto node = mib->node_by_address(Subsystem::EPS, tm_data[1]);
                std::optional<std::string_view> nv;
                if (node) nv = std::string_view(*node);
                const auto* m = mib->by_node_wire(Subsystem::EPS, Direction::TM,
                                                  nv, tm_data[2]);
                if (m && m->length_bytes && !m->is_variable_length) {
                    std::size_t total =
                        3 + static_cast<std::size_t>(*m->length_bytes) + 1;
                    if (crc_ok_at(total))
                        return Bytes(tm_data.begin(), tm_data.begin() + total);
                }
            }
            // No usable MIB length, or the device disagreed with it: let the
            // CRC locate the real boundary — the shortest prefix whose last
            // byte checks out over everything before it.
            const std::size_t max_len = std::min<std::size_t>(tm_data.size(), 260);
            for (std::size_t total = 4; total <= max_len; ++total)
                if (crc_ok_at(total))
                    return Bytes(tm_data.begin(), tm_data.begin() + total);
        }
        return tm_data;
    }
    if (s == Subsystem::Payload) {
        // The 3-byte header declares the data length exactly, so the frame
        // boundary is known without scanning for a marker.
        if (tm_data.size() >= framing::payload::kHeaderLen) {
            std::size_t total = framing::payload::frame_size(tm_data.data());
            if (total <= tm_data.size())
                return Bytes(tm_data.begin(), tm_data.begin() + total);
        }
        return tm_data;
    }
    if (s == Subsystem::GPS) {
        if (tm_data.size() >= 10 &&
            tm_data[0] == 0xAA && tm_data[1] == 0x44 && tm_data[2] == 0x12) {
            std::size_t hdr_len = tm_data[3];
            std::size_t pay_len = static_cast<std::size_t>(tm_data[8]) |
                                  (static_cast<std::size_t>(tm_data[9]) << 8);
            std::size_t total = hdr_len + pay_len + 4;   // + CRC32
            if (total <= tm_data.size())
                return Bytes(tm_data.begin(), tm_data.begin() + total);
        }
        return tm_data;
    }
    if (s == Subsystem::UHF) {
        // ESP frame: 0x22 0x69 | len | payload — the length byte is exact.
        if (tm_data.size() >= 3 &&
            tm_data[0] == framing::uhf::kStart0 &&
            tm_data[1] == framing::uhf::kStart1) {
            std::size_t total = 3 + tm_data[2];
            if (total <= tm_data.size())
                return Bytes(tm_data.begin(), tm_data.begin() + total);
        }
        return tm_data;
    }

    if (s == Subsystem::SENSOR) {
        // A5 AA | dest | cmd | len | payload | BA — the length byte gives the
        // frame size exactly, so the zero padding trims without a scan.
        if (tm_data.size() >= 5 &&
            tm_data[0] == framing::sensor::kStart0 &&
            tm_data[1] == framing::sensor::kStart1) {
            std::size_t total = framing::sensor::frame_size(tm_data[4]);
            if (total <= tm_data.size())
                return Bytes(tm_data.begin(), tm_data.begin() + total);
        }
        return tm_data;
    }

    if (s == Subsystem::ADCS) {
        // Two shapes reach us here.  A complete CubeSpace frame (the ADCS sim,
        // and an OBC that passes the frame through) ends at 1F FF — and since
        // the padding is zeros, the last 0xFF in the field is that trailer.
        if (tm_data.size() >= 2 &&
            tm_data[0] == framing::adcs::kEsc &&
            tm_data[1] == framing::adcs::kSom) {
            for (std::size_t i = tm_data.size(); i-- > 0; ) {
                if (tm_data[i] != framing::adcs::kEom) continue;
                if (i == 0 || tm_data[i-1] != framing::adcs::kEsc) continue;
                return Bytes(tm_data.begin(), tm_data.begin() + i + 1);
            }
            return tm_data;
        }
        // Otherwise the flight OBC has already stripped the framing and the
        // field is a bare msg_id || data.  Nothing in it declares its length,
        // but every ADCS TM in the MIB has a fixed declared size, so byte 0
        // (the TM id — never 0x1F, which is why the sniff above is safe) is
        // enough to find the end.
        if (!tm_data.empty() && mib) {
            const auto* m = mib->by_node_wire(Subsystem::ADCS, Direction::TM,
                                              std::nullopt, tm_data[0]);
            if (m && m->length_bytes && !m->is_variable_length) {
                std::size_t total = 1 + static_cast<std::size_t>(*m->length_bytes);
                if (total <= tm_data.size())
                    return Bytes(tm_data.begin(), tm_data.begin() + total);
            }
        }
        return tm_data;
    }
    // XLINK: fixed-layout frame (20-byte header + fixed data per message); no
    // self-delimiter, and decode_frame reads by fixed offset so trailing
    // zero-padding is ignored.  Pass the field through unchanged.
    return tm_data;
}

static Unwrapped unwrap_native(Subsystem s, const Bytes& frame) {
    Unwrapped u;
    switch (s) {
        case Subsystem::EPS: {
            auto d = framing::eps::decode(frame);
            u.framed_ok = d.framed_ok; u.crc_ok = d.crc_ok;
            u.msg_id = d.msg_id; u.payload = std::move(d.data);
            return u;
        }
        case Subsystem::ADCS: {
            // A complete frame still starts with 1F 7F.  Anything else is a
            // bare body the OBC already de-framed for us — no TM id is 0x1F,
            // so the two can never be confused.
            auto d = (frame.size() >= 2 && frame[0] == framing::adcs::kEsc &&
                      frame[1] == framing::adcs::kSom)
                   ? framing::adcs::decode(frame)
                   : framing::adcs::split_raw_body(frame);
            u.framed_ok = d.framed_ok;
            u.msg_id = d.msg_id; u.payload = std::move(d.data);
            return u;
        }
        case Subsystem::GPS: {
            auto d = framing::gps::decode(frame);
            u.framed_ok = d.framed_ok; u.crc_ok = d.crc_ok;
            u.msg_id = d.msg_id; u.payload = std::move(d.data);
            return u;
        }
        case Subsystem::Payload: {
            auto d = framing::payload::decode(frame);
            u.framed_ok = d.header_ok && d.length_ok;
            u.msg_id    = d.command_id;
            u.payload   = std::move(d.data);
            return u;
        }
        case Subsystem::UHF: {
            // Reply opcode identifies the TM (ACK/NACK/TELEM/…); the ESP
            // layer has no CRC, so framed_ok is the only integrity signal.
            auto d = framing::uhf::decode(frame);
            u.framed_ok = d.framed_ok;
            u.msg_id    = d.command;
            u.payload   = std::move(d.data);
            return u;
        }
        case Subsystem::SENSOR: {
            // The reply echoes the command id.  No checksum exists at this
            // layer, so a good SOF/length/EOF is all the integrity there is.
            auto d = framing::sensor::decode(frame);
            u.framed_ok = d.framed_ok;
            u.msg_id    = d.cmd_id;
            u.payload   = std::move(d.data);
            return u;
        }
        default:
            break;   // XLINK bypasses this path
    }
    return u;
}

// Read one complete response from the link.
//
// A 1432-byte TM downlink frame is not guaranteed to arrive in a single TCP
// segment — the flight OBC writes it in several — and CcsdsTmCodec::decode
// rejects anything short of the full frame, so a lone recv() can turn a good
// downlink into "decode failed".  Collect segments until the frame is whole.
// Native replies are self-delimiting and small enough to arrive in one chunk,
// so they return on the first read exactly as before.
static Bytes recv_response(transport::TcpClient& c,
                           const ccsds::CcsdsTmCodec* tm, int timeout_ms) {
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::milliseconds(timeout_ms);

    // Under 4 bytes the SOF is not yet visible, so the buffer could be either a
    // very short native reply or the first sliver of a TM frame.  Give it a
    // bounded grace to grow instead of waiting out the whole timeout.
    constexpr auto kShortGrace = std::chrono::milliseconds(200);
    std::optional<clock::time_point> short_until;

    Bytes rx;
    auto partial_tm = [&] {
        return tm && tm->looks_like_tm(rx) &&
               rx.size() < ccsds::CcsdsTmCodec::kFrameLen;
    };

    while (clock::now() < deadline) {
        auto chunk = c.recv(8192, 100);
        if (chunk.empty()) {
            if (rx.empty())   continue;   // nothing has arrived yet
            if (partial_tm()) continue;   // mid-frame gap — wait for the rest
            if (rx.size() < 4 && short_until && clock::now() < *short_until)
                continue;                 // unclassifiable — let it grow
            break;                        // short reply, already complete
        }
        rx.insert(rx.end(), chunk.begin(), chunk.end());
        if (rx.size() < 4) {
            if (!short_until) short_until = clock::now() + kShortGrace;
            continue;
        }
        if (!(tm && tm->looks_like_tm(rx))) break;    // native reply — done
        if (rx.size() >= ccsds::CcsdsTmCodec::kFrameLen) break;
    }
    return rx;
}

// ─── exchange ──────────────────────────────────────────────────────────────
ExchangeResult Session::send_tc(Subsystem s, const std::string& name,
                                const nlohmann::json& values,
                                std::optional<std::string> node,
                                int timeout_ms) {
    ExchangeResult r{};
    r.sent_values = values;

    auto* msg = mib_.find(s, Direction::TC, name);
    if (!msg) { r.error = "TC '" + name + "' not found in MIB"; return r; }

    // node resolution: if not provided, use the message's own node
    std::optional<std::string_view> nv;
    if (node)            nv = std::string_view(*node);
    else if (msg->node)  nv = std::string_view(*msg->node);

    int dest_addr = mib_.node_address(s, nv);
    if (s == Subsystem::EPS && dest_addr == 0)
        dest_addr = framing::eps::kBroadcastAddr;

    // 1. encode payload
    try {
        r.tx_payload = codec::encode(*msg, values);
    } catch (const std::exception& e) {
        r.error = std::string("encode: ") + e.what(); return r;
    }

    return transact(s, msg->wire_id, dest_addr, nv, timeout_ms, r);
}

// ─── caller-built data field (Payload variable-length commands) ────────────
ExchangeResult Session::send_tc_data(Subsystem s, const std::string& name,
                                     const Bytes& data, int timeout_ms) {
    ExchangeResult r{};
    r.sent_values = nlohmann::json::object();

    auto* msg = mib_.find(s, Direction::TC, name);
    if (!msg) { r.error = "TC '" + name + "' not found in MIB"; return r; }

    int dest_addr = mib_.node_address(s, std::nullopt);
    r.tx_payload  = data;
    return transact(s, msg->wire_id, dest_addr, std::nullopt, timeout_ms, r);
}

// ─── TM request (CubeSpace-style: send a TC frame carrying the TM's msg_id, ─
//     no payload, and the receiver replies with the TM frame) ───────────────
ExchangeResult Session::send_tm_request(Subsystem s, const std::string& tm_name,
                                        std::optional<std::string> node,
                                        int timeout_ms) {
    ExchangeResult r{};
    r.sent_values = nlohmann::json::object();

    auto* tm = mib_.find(s, Direction::TM, tm_name);
    if (!tm) { r.error = "TM '" + tm_name + "' not found in MIB"; return r; }

    std::optional<std::string_view> nv;
    if (node)           nv = std::string_view(*node);
    else if (tm->node)  nv = std::string_view(*tm->node);

    int dest_addr = mib_.node_address(s, nv);
    if (s == Subsystem::EPS && dest_addr == 0)
        dest_addr = framing::eps::kBroadcastAddr;

    // Empty payload — the msg_id alone says "please send me <tm>", and the
    // receiver answers with that TM frame.  Direct to an ADCS the msg_id
    // rides in the CubeSpace frame; through the flight OBC it rides in the
    // CCSDS tc_id and the data field goes out empty (tc_len = 0).
    r.tx_payload = Bytes{};

    return transact(s, tm->wire_id, dest_addr, nv, timeout_ms, r,
                    /*id_only=*/true);
}

// ─── send_raw — payload-server-style: no MIB, no native framing ────────────
//     The operator typed raw bytes.  If CCSDS is on, wrap; else send as-is.
//     Used by Subsystem::Payload (where there's no native framing layer).
ExchangeResult Session::send_raw(Subsystem s, std::uint16_t tc_id,
                                 const Bytes& raw_payload, int timeout_ms) {
    ExchangeResult r{};
    r.sent_values = nlohmann::json::object();
    r.tx_payload  = raw_payload;
    r.tx_native   = raw_payload;            // for Payload, "native" = the raw frame

    // CCSDS wrap if enabled
    if (ccsds_enabled()) {
        r.tx_frame = ccsds_->encode(s, ccsds::CcsdsDirection::TC,
                                    tc_id, raw_payload);
        r.ccsds_used = true;
        r.ccsds_tx   = ccsds_->decode(r.tx_frame);
    } else {
        r.tx_frame = raw_payload;
    }

    // Send
    auto cit = conns_.find(idx(s));
    if (cit == conns_.end() || !cit->second.connected()) {
        r.error = std::string("not connected to ") + to_string(s); return r;
    }
    auto t0 = std::chrono::steady_clock::now();
    if (!cit->second.send(r.tx_frame)) {
        r.error = "send failed"; return r;
    }

    // Receive
    r.rx_frame = recv_response(cit->second, tm_, timeout_ms);
    r.rtt = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0);

    if (r.rx_frame.empty()) { r.ok = true; return r; }

    // CCSDS unwrap if applicable (TM frame FCA1 first, then legacy 1ACF)
    if (ccsds_enabled() && tm_ && tm_->looks_like_tm(r.rx_frame)) {
        auto d = tm_->decode(r.rx_frame);
        if (!d) { r.error = "TM RX decode failed"; return r; }
        if (!d->crc_ok) { r.error = "TM RX bad CRC"; return r; }
        r.tm_frame  = d;
        r.rx_native = strip_tm_padding(s, d->tm_data, &mib_);
    } else if (ccsds_enabled() && ccsds_->looks_like_ccsds(r.rx_frame)) {
        auto d = ccsds_->decode(r.rx_frame);
        if (!d) { r.error = "CCSDS RX decode failed"; return r; }
        if (!d->crc_ok) { r.error = "CCSDS RX bad CRC"; return r; }
        r.ccsds_rx  = d;
        r.rx_native = d->tc_data;
    } else {
        r.rx_native = r.rx_frame;
    }

    r.rx_payload = r.rx_native;   // for Payload, the inner bytes are the ACK
    r.ok = true;
    return r;
}


ExchangeResult& Session::transact(Subsystem s, int wire_id, int dest_addr,
                                  std::optional<std::string_view> nv,
                                  int timeout_ms, ExchangeResult& r,
                                  bool id_only) {
    // 2. apply native framing
    r.tx_native = wrap_native(s, wire_id, r.tx_payload, dest_addr,
                              adcs_bare_tc_ && ccsds_enabled(), id_only);

    // 2a. optional CCSDS wrap (native frame becomes tc_data; tc_id = wire-id)
    if (ccsds_enabled()) {
        r.tx_frame = ccsds_->encode(s, ccsds::CcsdsDirection::TC,
                                    static_cast<std::uint16_t>(wire_id),
                                    r.tx_native);
        r.ccsds_used = true;
        r.ccsds_tx   = ccsds_->decode(r.tx_frame);
    } else {
        r.tx_frame = r.tx_native;
    }

    // 3. send
    auto cit = conns_.find(idx(s));
    if (cit == conns_.end() || !cit->second.connected()) {
        r.error = std::string("not connected to ") + to_string(s); return r;
    }
    auto t0 = std::chrono::steady_clock::now();
    if (!cit->second.send(r.tx_frame)) {
        r.error = "send failed"; return r;
    }

    // 4. receive
    r.rx_frame = recv_response(cit->second, tm_, timeout_ms);
    r.rtt = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0);

    if (r.rx_frame.empty()) { r.ok = true; return r; }

    // 4a. optional CCSDS unwrap — recover the native subsystem frame.
    //     TM downlink frames (SOF FCA1) carry the native TM in TM DATA;
    //     legacy symmetric frames (SOF 1ACF) carry it in tc_data.
    if (ccsds_enabled() && tm_ && tm_->looks_like_tm(r.rx_frame)) {
        auto d = tm_->decode(r.rx_frame);
        if (!d) { r.error = "TM RX decode failed"; return r; }
        if (!d->crc_ok) { r.error = "TM RX bad CRC"; return r; }
        r.tm_frame  = d;
        r.rx_native = strip_tm_padding(s, d->tm_data, &mib_);
    } else if (ccsds_enabled() && ccsds_->looks_like_ccsds(r.rx_frame)) {
        auto d = ccsds_->decode(r.rx_frame);
        if (!d) { r.error = "CCSDS RX decode failed"; return r; }
        if (!d->crc_ok) { r.error = "CCSDS RX bad CRC"; return r; }
        r.ccsds_rx  = d;
        r.rx_native = d->tc_data;     // OBC put the native TM frame here
    } else {
        r.rx_native = r.rx_frame;
    }

    // 5. decode response
    auto u = unwrap_native(s, r.rx_native);
    if (!u.framed_ok) { r.error = "response: framing error"; return r; }
    if (!u.crc_ok)    { r.error = "response: bad CRC";       return r; }
    r.response_msg_id = u.msg_id;
    r.rx_payload      = u.payload;

    auto* rtm = mib_.by_node_wire(s, Direction::TM, nv, u.msg_id);

    // A sensor-controller NACK reuses the command id of the thing it rejects
    // and is told apart only by its payload length — so decoding it against
    // the success layout would report a rejection as AVN_CMD_SUCCESS.  When
    // the reply is the wrong size for the TM but the right size for the
    // EVENT at that id, decode it as the NACK it is.
    if (s == Subsystem::SENSOR && rtm && rtm->length_bytes &&
        r.rx_payload.size() != static_cast<std::size_t>(*rtm->length_bytes)) {
        const auto* nak = mib_.by_node_wire(s, Direction::Event, nv, u.msg_id);
        if (nak && nak->length_bytes &&
            r.rx_payload.size() == static_cast<std::size_t>(*nak->length_bytes))
            rtm = nak;
    }

    r.response_msg = rtm;
    if (rtm) {
        try {
            r.decoded_values = codec::decode(*rtm, u.payload);
        } catch (const std::exception& e) {
            r.error = std::string("decode: ") + e.what();
        }
    }
    r.ok = true;
    return r;
}

// ─── XLink Service Server dedicated path ────────────────────────────────────
// This bypasses the generic codec/framing pipeline because XLink's wire format
// is monolithic (16-byte product_key + 4-byte service_event + data), all with
// U32-word LE bit-packing that the generic byte-offset codec would mangle.

ExchangeResult Session::send_xlink_tc(const std::string& tc_name,
                                      const nlohmann::json& values,
                                      bool is_get,
                                      int /*timeout_ms*/) {
    ExchangeResult r;
    auto* tc = mib_.find(Subsystem::XLINK, Direction::TC, tc_name);
    if (!tc) { r.error = "XLink TC '" + tc_name + "' not found in MIB"; return r; }

    // 1. encode
    auto key = xlink_key_;
    std::uint32_t event = static_cast<std::uint32_t>(tc->msg_id);
    std::uint32_t flag  = is_get ? xdos::xlink::codec::FLAG_GET
                                 : xdos::xlink::codec::FLAG_SET;
    auto frame = xdos::xlink::codec::encode_frame(key, event, flag, *tc, values);
    r.tx_native = frame;

    // 2. optional CCSDS wrap — parity with ADCS/EPS/GPS/Payload.  The native
    //    XLink frame (product key + service event + data) rides inside the TC
    //    frame's data field; the OBC unwraps it and forwards the native bytes
    //    to the transceiver, so device semantics (NAK on wrong key, etc.) are
    //    unchanged.  When framing is off, the raw XLink frame goes as before.
    Bytes on_wire = frame;
    if (ccsds_enabled()) {
        on_wire = ccsds_->encode(Subsystem::XLINK, ccsds::CcsdsDirection::TC,
                                 static_cast<int>(tc->msg_id), frame);
    }
    r.tx_frame = on_wire;

    // 3. send
    if (!connected(Subsystem::XLINK)) { r.error = "XLink not connected"; return r; }
    auto& client = conns_.at(idx(Subsystem::XLINK));
    if (!client.send(on_wire)) { r.error = "XLink send failed"; return r; }

    // 4. receive.  The reply may be a small native XLink frame (direct) or a
    //    full 1432-byte CCSDS TM frame (through the OBC).  A CCSDS frame can
    //    span several TCP segments on the wire, so read until we have a
    //    complete frame or the link goes quiet — a single recv() can return a
    //    partial frame and make decode fail ("no response").
    Bytes rx;
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
        while (std::chrono::steady_clock::now() < deadline) {
            auto chunk = client.recv(8192, 300);
            if (!chunk.empty()) {
                rx.insert(rx.end(), chunk.begin(), chunk.end());
                // Enough for a full TM frame?  Stop.  Otherwise grab a short
                // grace window for trailing segments, then stop when quiet.
                if (tm_ && tm_->looks_like_tm(rx) &&
                    rx.size() >= ccsds::CcsdsTmCodec::kFrameLen) break;
                auto tail = std::chrono::steady_clock::now() + std::chrono::milliseconds(40);
                bool got_more = false;
                while (std::chrono::steady_clock::now() < tail) {
                    auto more = client.recv(8192, 10);
                    if (more.empty()) continue;
                    rx.insert(rx.end(), more.begin(), more.end());
                    got_more = true;
                    if (tm_ && tm_->looks_like_tm(rx) &&
                        rx.size() >= ccsds::CcsdsTmCodec::kFrameLen) break;
                }
                if (!got_more) break;   // native reply already complete
                if (tm_ && tm_->looks_like_tm(rx) &&
                    rx.size() >= ccsds::CcsdsTmCodec::kFrameLen) break;
            }
        }
    }
    if (rx.empty()) { r.error = "XLink no response"; return r; }
    r.rx_frame = rx;

    // 5. optional CCSDS unwrap — TM downlink frame (FCA1) first, then legacy
    //    symmetric frame (1ACF); otherwise the response is already native.
    Bytes native_rx = rx;
    if (ccsds_enabled() && tm_ && tm_->looks_like_tm(rx)) {
        auto d = tm_->decode(rx);
        if (!d)          { r.error = "XLink TM RX decode failed"; return r; }
        if (!d->crc_ok)  { r.error = "XLink TM RX bad CRC";       return r; }
        r.tm_frame = d;
        native_rx  = strip_tm_padding(Subsystem::XLINK, d->tm_data, &mib_);
    } else if (ccsds_enabled() && ccsds_->looks_like_ccsds(rx)) {
        auto d = ccsds_->decode(rx);
        if (!d)          { r.error = "XLink RX decode failed"; return r; }
        if (!d->crc_ok)  { r.error = "XLink RX bad CRC";       return r; }
        r.ccsds_rx = d;
        native_rx  = d->tc_data;
    }
    // 6. decode the native XLink response
    auto* tm = mib_.find(Subsystem::XLINK, Direction::TM, tc_name + "_RSP");

    // TM-downlink path: the 1280-byte TM DATA field is zero-padded and the
    // XLink frame has no self-delimiter, so trim to the true native length —
    // 20-byte header + Database RSP size (U32-word-aligned).  Header-only
    // ACK/NAK replies (no _RSP message) trim to the bare 20-byte header.
    // Direct native replies are already exact-length and pass unchanged.
    if (r.tm_frame) {
        std::size_t want = 20;
        if (tm && tm->length_bytes && !tm->is_variable_length)
            want += (static_cast<std::size_t>(*tm->length_bytes) + 3)
                    & ~std::size_t(3);
        if (want <= native_rx.size()) native_rx.resize(want);
    }
    r.rx_native = native_rx;
    auto d = xdos::xlink::codec::decode_frame(native_rx, tm);
    if (!d.framed_ok) { r.error = "XLink response framing error"; return r; }

    r.response_msg_id  = static_cast<int>(d.event_base);
    r.rx_payload       = Bytes(native_rx.begin() + 20, native_rx.end());
    r.decoded_values   = std::move(d.values);
    // Surface ACK/NAK to the caller.  For XLink, NAK means the device rejected
    // the command (wrong key, bad params, or busy) — annotate the error.
    if (d.nak) r.error = "XLink NAK";
    r.ok = d.framed_ok && !d.nak;
    return r;
}

} // namespace xdos::session
