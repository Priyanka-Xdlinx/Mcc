// SPDX-License-Identifier: Apache-2.0
// XDOS Bus Simulator — per-subsystem responder.
#pragma once

#include "common/types.hpp"
#include "mib/mib.hpp"

#include <chrono>
#include <memory>

namespace xdos::sim {

class EngineResponder;       // forward-declare; defined in handlers/

class Responder {
public:
    Responder(const mib::Mib& mib, Subsystem s);
    ~Responder();

    // Optionally attach an EngineResponder so SETs mutate live engine state
    // and TM values get pulled from the engine instead of synthesized.
    // If unset, the responder falls back to the old plausible-values flow.
    void attach_engine_responder(std::shared_ptr<EngineResponder> er);

    // Handle a single native bus frame received on this subsystem's port.
    // Returns the framed response bytes (may be empty if the request had
    // no response).  `peer_label` is used only for logging.
    Bytes handle(const Bytes& in_frame, const std::string& peer_label,
                 bool verbose);

private:
    const mib::Mib&                  mib_;
    Subsystem                        sub_;
    std::shared_ptr<EngineResponder> er_;
    std::chrono::steady_clock::time_point t0_;

    double now_seconds() const {
        return std::chrono::duration<double>(
            std::chrono::steady_clock::now() - t0_).count();
    }

    const mib::Message* response_for(const mib::Message* tc,
                                     const Bytes& payload,
                                     std::optional<std::string_view> node);
};

// Build a native bus frame for a TM reply.  Exposed for tests so we can
// validate dest/src addressing.  See impl in responder.cpp.
Bytes wrap(Subsystem s, int wire_id, const Bytes& payload,
           const mib::Mib& mib, std::optional<std::string_view> node);

} // namespace xdos::sim
