// SPDX-License-Identifier: Apache-2.0
// XDOS Bus Simulator — plausible value generation.
#pragma once

#include "mib/mib.hpp"
#include <nlohmann/json.hpp>

namespace xdos::sim {

// Generate a plausible value map for a TM message using its MIB definition.
// Values drift slowly with `t` (seconds since sim start) so dashboards
// look alive across repeated requests.
nlohmann::json plausible_values(const mib::Message& tm, double t,
                                int array_records = 3);

} // namespace xdos::sim
