// SPDX-License-Identifier: Apache-2.0
// Engine-driven responder.
//
// Replaces the old "plausible random values" simulator with one that pulls
// live values from the per-subsystem sim engines.  When a TM name maps to a
// known live field, we return that field; everything else falls back to the
// plausible_values() generator so MIB coverage stays complete.

#ifndef XDOS_SIM_HANDLERS_ENGINE_RESPONDER_HPP
#define XDOS_SIM_HANDLERS_ENGINE_RESPONDER_HPP

#include "mib/mib.hpp"
#include "engines/eps_engine.hpp"
#include "engines/adcs_engine.hpp"
#include "engines/gps_engine.hpp"

#include <nlohmann/json.hpp>

namespace xdos::sim {

// One per process; subsystem-specific.  Owns no MIB, takes a const&.
class EngineResponder {
public:
    EngineResponder(const mib::Mib& mib, Subsystem sub);

    // Inject engines.  Set only the one for `sub`; pass nullptr for the rest.
    void set_eps_engine (EpsEngine*  e) { eps_  = e; }
    void set_adcs_engine(AdcsEngine* e) { adcs_ = e; }
    void set_gps_engine (GpsEngine*  e) { gps_  = e; }

    // Compute the values to put in a TM frame for `tm`.
    // Pulls live state from the engine when known, otherwise falls back to
    // plausible synthetic values from the existing generator.  Also performs
    // any SET-side state mutation triggered by the incoming TC.
    nlohmann::json build_tm_values(const mib::Message& tm) const;

    // Apply a SET TC to the engine state (mutate).  Called *before*
    // build_tm_values so the response reflects the SET we just absorbed.
    void apply_tc(const mib::Message& tc,
                  const nlohmann::json& values) const;

private:
    const mib::Mib&  mib_;
    Subsystem        sub_;
    EpsEngine*       eps_  = nullptr;
    AdcsEngine*      adcs_ = nullptr;
    GpsEngine*       gps_  = nullptr;
};

} // namespace xdos::sim

#endif
