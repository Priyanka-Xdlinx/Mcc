// SPDX-License-Identifier: Apache-2.0
// XDOS Ground Station — subsystem plugin interface.
//
// -- Motivation ---------------------------------------------------------------
// Adding a new subsystem to the GS used to mean:
//   1. Add an enum value to Subsystem
//   2. Add case branches in ~12 places across session/cli/simulator
//   3. Add a codec module under src/framing/
//   4. Add per-subsystem sim, engine, and responder classes
// The 12 case branches were the pain point — subsystem-specific logic scattered
// across the core framework.
//
// This interface consolidates all the subsystem-specific behavior into ONE
// class per subsystem.  The framework loads a registry of SubsystemPlugin
// implementations at startup and dispatches through them.  Adding a new
// subsystem now looks like:
//   1. Drop mib/<name>.json in the MIB directory
//   2. Implement SubsystemPlugin (typically ~150-300 LOC)
//   3. Register it in subsystems/registry.cpp (one line)
//   4. Optionally add a <name>_sim_main.cpp for offline testing
// No changes to session/cli/codec — they read from the registry.
//
// -- Migration strategy -------------------------------------------------------
// Existing subsystems (EPS, ADCS, GPS, Payload) keep working via the legacy
// switch-based dispatch during migration.  The Session/CLI check the plugin
// registry FIRST, falling back to legacy behavior for the four grandfathered
// subsystems.  XLINK-S is the first "plugin-native" subsystem.  Migrating the
// legacy four is a follow-up: each becomes ~200 LOC of plugin code, and the
// switch statements delete cleanly.

#pragma once

#include "common/types.hpp"
#include "mib/mib.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

namespace xdos::subsystems {

// A single TCP endpoint the plugin needs to reach.
struct BusEndpoint {
    std::string host = "127.0.0.1";
    int         port = 0;
    std::string label;              // "TCP", "UDP-RCV", "control", etc.
};

// The result of encoding a TC or decoding a TM.
struct EncodedFrame {
    std::vector<std::uint8_t> bytes;   // exactly what goes on the wire
    std::string  diagnostic;           // human-readable summary for the log
    int          msg_id_used = -1;     // resolved msg_id (for round-trip matching)
};

struct DecodedFrame {
    bool         framed_ok = false;    // header parsed
    bool         crc_ok    = true;     // checksum (if any) verified
    bool         ack       = true;     // SET/ACT: was the request acknowledged?
    int          msg_id    = -1;       // subsystem's msg_id
    nlohmann::json values  = nlohmann::json::object();  // decoded field → value
    std::string  diagnostic;           // human-readable summary for the log
};

// Per-connection context — plugins may need to carry auth tokens or session
// state.  Framework treats this as an opaque handle; only the plugin dereferences
// it.  Owned by Session; lifetime is the TCP connection.
struct FrameContext {
    virtual ~FrameContext() = default;
};

// Called by the CLI when displaying a decoded TM parameter, giving the plugin
// a chance to rewrite the name (e.g., "CH4 Current Readings" → "CH4 (ADCS)
// Current Readings" for EPS, or add a bit-flag breakdown for XLink).  Return
// std::nullopt to accept the default rendering.
struct DisplayHint {
    std::optional<std::string> display_name;    // override the parameter name
    std::optional<std::string> value_suffix;    // additional text after value
    std::vector<std::string>   secondary_lines; // extra lines beneath the row
};

// The interface every subsystem implements.
class SubsystemPlugin {
public:
    virtual ~SubsystemPlugin() = default;

    // ── Identity ───────────────────────────────────────────────────────────
    // Registry key.  Uppercase.  Must be unique across all plugins.
    // Examples: "EPS", "ADCS", "GPS", "PAYLOAD", "XLINK".
    virtual std::string name() const = 0;

    // Filename inside the MIB directory.  Loaded once at startup.
    // Examples: "eps.json", "xlink.json".
    virtual std::string mib_filename() const = 0;

    // Human-friendly description shown in the CLI subsystem picker.
    virtual std::string description() const = 0;

    // ── Connectivity ───────────────────────────────────────────────────────
    // The default endpoint(s) the plugin talks to.  Most subsystems have one
    // TCP endpoint; XLink also has an optional UDP RCV data stream.
    virtual std::vector<BusEndpoint> default_endpoints() const = 0;

    // If true, frames are routed via the OBC bridge (SET framing wrapping,
    // CCSDS envelope, etc.).  If false, the plugin talks directly to its
    // endpoint over raw TCP.
    // EPS/ADCS/GPS/Payload: true.  XLINK: false (direct Ethernet).
    virtual bool via_obc_bridge() const = 0;

    // Create a per-connection state object if the plugin needs one.
    // Return nullptr for stateless plugins.  Called each time Session opens
    // a new connection to this subsystem.
    virtual std::unique_ptr<FrameContext> make_context() const {
        return nullptr;
    }

    // ── Framing ────────────────────────────────────────────────────────────
    // Encode a TC message into the byte sequence that goes on the wire.
    // `msg` is the MIB Message; `values` is the JSON with field → value.
    virtual EncodedFrame encode_tc(const mib::Message& msg,
                                   const nlohmann::json& values,
                                   FrameContext* ctx) const = 0;

    // Decode a received TM frame back to structured values.
    // `mib` is provided so the plugin can look up the response message by
    // its wire-level ID.
    virtual DecodedFrame decode_tm(const std::vector<std::uint8_t>& frame,
                                   const mib::Mib& mib,
                                   FrameContext* ctx) const = 0;

    // ── CLI presentation ───────────────────────────────────────────────────
    // Override the display of one decoded parameter.  Return {} to accept
    // the default (name = MIB name, value = format_value output).
    virtual DisplayHint display_hint(const mib::Parameter& /*p*/,
                                     const nlohmann::json& /*value*/,
                                     const mib::Mib& /*mib*/) const {
        return {};
    }

    // Should the CLI show the [PDU1]-style node tag in TC menus?
    virtual bool show_node_tags() const { return true; }

    // Prefix used for spec-level IDs (e.g., "EP" for EP-019, "XL" for XL-004).
    // If empty, the CLI shows raw msg_id.
    virtual std::string param_id_prefix() const { return ""; }
};

// ── Registry ───────────────────────────────────────────────────────────────
// Global registry populated at startup.  Owned by the process.
class Registry {
public:
    static Registry& instance();

    // Register a plugin — called once per subsystem at program startup.
    void add(std::unique_ptr<SubsystemPlugin> p);

    // Look up by name (case-insensitive).  Returns nullptr if not registered.
    SubsystemPlugin* find(std::string_view name) const;

    // All registered plugins in registration order.
    const std::vector<std::unique_ptr<SubsystemPlugin>>& all() const {
        return plugins_;
    }

private:
    Registry() = default;
    std::vector<std::unique_ptr<SubsystemPlugin>> plugins_;
};

// Convenience: register the built-in plugins (called once from each main()
// that needs them — the GS, the OBC, and any sim binary).
void register_builtins();

} // namespace xdos::subsystems
