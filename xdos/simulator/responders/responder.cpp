// SPDX-License-Identifier: Apache-2.0
#include "responders/responder.hpp"
#include "plausible.hpp"
#include "handlers/engine_responder.hpp"

#include "codec/codec.hpp"
#include "framing/eps_framing.hpp"
#include "framing/adcs_framing.hpp"
#include "framing/gps_framing.hpp"
#include "framing/uhf_framing.hpp"
#include "common/hex.hpp"

#include <cstring>
#include <iostream>
#include <optional>
#include <random>

namespace xdos::sim {

using nlohmann::json;

Responder::Responder(const mib::Mib& mib, Subsystem s)
    : mib_(mib), sub_(s), t0_(std::chrono::steady_clock::now()) {}

Responder::~Responder() = default;

void Responder::attach_engine_responder(std::shared_ptr<EngineResponder> er) {
    er_ = std::move(er);
}

// ─── framing in/out ──────────────────────────────────────────────────────────
namespace {

struct Unwrapped { int msg_id = -1; Bytes payload; std::optional<std::string> node; bool ok = false; };

Unwrapped unwrap(const mib::Mib& mib, Subsystem s, const Bytes& f) {
    Unwrapped u;
    switch (s) {
        case Subsystem::EPS: {
            auto d = framing::eps::decode(f);
            u.ok = d.framed_ok; u.msg_id = d.msg_id; u.payload = std::move(d.data);
            u.node = mib.node_by_address(Subsystem::EPS, d.dest_addr);
            return u;
        }
        case Subsystem::ADCS: {
            auto d = framing::adcs::decode(f);
            u.ok = d.framed_ok; u.msg_id = d.msg_id; u.payload = std::move(d.data);
            return u;
        }
        case Subsystem::GPS: {
            auto d = framing::gps::decode(f);
            u.ok = d.framed_ok; u.msg_id = d.msg_id; u.payload = std::move(d.data);
            return u;
        }
        case Subsystem::UHF: {
            auto d = framing::uhf::decode(f);
            u.ok = d.framed_ok; u.msg_id = d.command; u.payload = std::move(d.data);
            return u;
        }
        case Subsystem::Payload:
        default:
            // Payload/XLINK sims don't use the shared responder — own binaries.
            return u;
    }
    return u;
}

}  // namespace (anonymous)

Bytes wrap(Subsystem s, int wire_id, const Bytes& payload,
           const mib::Mib& mib, std::optional<std::string_view> node) {
    int addr = mib.node_address(s, node);
    switch (s) {
        case Subsystem::EPS:
            // EPS TM: built by sim responding TO the OBC.
            //   dest = OBC1 (201) — TM is destined for the OBC.
            //   src  = the EPS card (PDU/BMS/MPPT) that is replying.
            // This matches NL EPS Communication Protocol Msg Structure where
            // dest=destination, src=origin.  The on-orbit firmware will do the
            // same on real hardware.
            return framing::eps::encode(framing::eps::kObc1Addr,
                                        static_cast<Byte>(wire_id), payload,
                                        static_cast<Byte>(addr));
        case Subsystem::ADCS:
            return framing::adcs::encode(static_cast<Byte>(wire_id), payload);
        case Subsystem::GPS:
            return framing::gps::encode(static_cast<std::uint16_t>(wire_id), payload);
        case Subsystem::UHF:
            return framing::uhf::encode(static_cast<Byte>(wire_id), payload);
        case Subsystem::Payload:
        default:
            return {};
    }
    return {};
}

namespace {

// encode with trailing array records if the TM is variable-length
Bytes encode_with_arrays(const mib::Message& tm, const json& values) {
    Bytes fixed = codec::encode(tm, values);
    std::vector<const mib::Parameter*> arrays;
    for (auto const& p : tm.parameters) if (p.is_array) arrays.push_back(&p);
    if (arrays.empty()) return fixed;

    int n = 0;
    for (auto const& p : tm.parameters) {
        if (p.is_array) continue;
        auto nm = p.name;
        if (nm.find("num") != std::string::npos ||
            nm.find("count") != std::string::npos) {
            auto it = values.find(p.name);
            if (it != values.end() && it->is_number_integer())
                n = it->get<int>();
            break;
        }
    }
    int rec_len = 0;
    for (auto const* p : arrays) rec_len += p->length;

    std::mt19937 rng{0xCAFE};
    std::uniform_real_distribution<double> uf(1.0, 40.0);
    std::uniform_int_distribution<int>     ui(0, 30);

    Bytes rec(rec_len * n, 0);
    int pos = 0;
    for (int r = 0; r < n; ++r) {
        for (auto const* p : arrays) {
            if (p->data_type == "CHAR" || p->data_type == "STRING") {
                std::memcpy(rec.data() + pos, "SIM", std::min(3, p->length));
            } else if (p->data_type == "FLOAT" && p->length == 4) {
                float f = static_cast<float>(uf(rng));
                std::memcpy(rec.data() + pos, &f, 4);
            } else if (p->data_type == "DOUBLE" && p->length == 8) {
                double d = uf(rng);
                std::memcpy(rec.data() + pos, &d, 8);
            } else {
                int v = ui(rng);
                for (int i = 0; i < p->length; ++i)
                    rec[pos + i] = static_cast<Byte>((v >> (8 * i)) & 0xFF);
            }
            pos += p->length;
        }
    }
    fixed.insert(fixed.end(), rec.begin(), rec.end());
    return fixed;
}

} // namespace

// ─── response resolution ────────────────────────────────────────────────────
const mib::Message* Responder::response_for(const mib::Message* tc,
                                            const Bytes& payload,
                                            std::optional<std::string_view> node) {
    if (!tc) return nullptr;

    // GPS LOG -> respond with the requested log
    if (sub_ == Subsystem::GPS && tc->name == "LOG") {
        try {
            auto v = codec::decode(*tc, payload);
            int want = v.value("message_id", 0);
            for (auto const* tm : mib_.messages(Subsystem::GPS, Direction::TM))
                if (tm->msg_id == want) return tm;
        } catch (...) {}
        return nullptr;
    }

    // EPS / ADCS: matching TM by (node, msg_id)
    for (auto const* tm : mib_.messages(sub_, Direction::TM)) {
        if (tm->msg_id == tc->msg_id && tm->node == tc->node) return tm;
    }
    for (auto const* tm : mib_.messages(sub_, Direction::TM))
        if (tm->msg_id == tc->msg_id) return tm;

    // fallback: any TM (a generic ack-like response)
    auto tms = mib_.messages(sub_, Direction::TM);
    return tms.empty() ? nullptr : tms.front();
}

// ─── handle a frame ─────────────────────────────────────────────────────────
Bytes Responder::handle(const Bytes& in_frame, const std::string& peer,
                        bool verbose) {
    auto u = unwrap(mib_, sub_, in_frame);
    if (!u.ok) {
        if (verbose)
            std::cout << "  [" << to_string(sub_) << "] " << peer
                      << "  bad frame (" << in_frame.size() << "B)\n";
        return {};
    }

    std::optional<std::string_view> nv;
    if (u.node) nv = std::string_view(*u.node);

    const mib::Message* tc = mib_.by_node_wire(sub_, Direction::TC, nv, u.msg_id);

    // ADCS TM-request: a frame whose msg_id is a TM (>=128 in CubeSpace's
    // convention) is the ground asking for that telemetry; reply with it.
    const mib::Message* tm_direct = nullptr;
    if (!tc && sub_ == Subsystem::ADCS) {
        tm_direct = mib_.by_node_wire(sub_, Direction::TM, nv, u.msg_id);
    }

    if (verbose) {
        std::cout << "  [" << to_string(sub_) << "] " << peer
                  << "  RX wire=0x" << std::hex << u.msg_id << std::dec
                  << "  " << u.payload.size() << "B body"
                  << (u.node ? "  node=" + *u.node : "")
                  << "  -> " << (tc ? tc->name
                                : tm_direct ? tm_direct->name + " (TM-request)"
                                : std::string("(unknown)")) << "\n";
    }

    const mib::Message* tm = tm_direct
        ? tm_direct
        : response_for(tc, u.payload, nv);
    if (!tm) return {};

    // If an EngineResponder is attached, first apply any SET-side mutation,
    // then ask the engine for live values.  Without one, fall back to the
    // synthetic plausible-values flow.
    json values;
    if (er_) {
        if (tc) {
            try {
                json decoded = codec::decode(*tc, u.payload);
                er_->apply_tc(*tc, decoded);
            } catch (...) { /* SET parse failed — skip mutation */ }
        }
        values = er_->build_tm_values(*tm);
    } else {
        values = plausible_values(*tm, now_seconds());
    }

    Bytes tm_body;
    try {
        tm_body = tm->is_variable_length
            ? encode_with_arrays(*tm, values)
            : codec::encode(*tm, values);
    } catch (const std::exception& e) {
        if (verbose) std::cout << "      encode TM failed: " << e.what() << "\n";
        return {};
    }

    auto out = wrap(sub_, tm->wire_id, tm_body, mib_, tm->node);
    if (verbose) {
        std::cout << "      TX TM " << tm->name
                  << "  wire=0x" << std::hex << tm->wire_id << std::dec
                  << "  " << tm_body.size() << "B body  " << out.size() << "B framed\n";
    }
    return out;
}

} // namespace xdos::sim
