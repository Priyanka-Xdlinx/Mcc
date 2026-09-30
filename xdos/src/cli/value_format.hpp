// SPDX-License-Identifier: Apache-2.0
// Shared operator-facing value formatting (units, scale, enum symbols).
// Used by the telecommanding CLI (xdos_gs) and the Master Frame telemetry
// terminal (xdos_tm_view).
#pragma once
#include "mib/mib.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace xdos::cli {
std::string format_value(const nlohmann::json& v, const mib::Parameter& p);
std::pair<std::string, std::string> flag_labels_for(const mib::Parameter& p);

// Derived engineering values for UHF (CC1110, 27 MHz XOSC) replies.
// For TX/RX_RADIO_CONFIG: carrier frequency, data rate, channel bandwidth,
// deviation and channel spacing computed from the raw registers with the
// CC1110 datasheet formulas.  For TX/RX_RADIO_PKT_CONFIG: the combined sync
// word.  Returns "" for other messages.  Lines are prefixed with `indent`.
std::string uhf_derived_text(const std::string& tm_name,
                             const nlohmann::json& vals,
                             const std::string& indent);
} // namespace xdos::cli
