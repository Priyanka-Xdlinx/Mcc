// SPDX-License-Identifier: Apache-2.0
// XDOS Ground Station — Mission Information Base.
//
// Loads one or more per-subsystem JSON files (produced by tools/sqlite_to_json.py)
// into immutable in-memory structures and provides O(1) lookups by name,
// (subsystem, direction, wire_id), and (subsystem, direction, node, wire_id)
// — the last being needed for EPS where the wire id repeats across boards.
#pragma once

#include "common/types.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace xdos::mib {

struct EnumValue {
    std::int64_t value;
    std::string  name;
    std::string  description;
};

struct EnumType {
    std::string            name;
    std::string            doc_reference;
    std::vector<EnumValue> values;
    const EnumValue*       by_value(std::int64_t v) const;
    const EnumValue*       by_name(std::string_view n) const;
};

struct Parameter {
    int                seq        = 0;
    std::optional<int> offset;                  // in the parent message's offset_unit
    int                length     = 0;          // ditto
    std::string        name;
    std::string        data_type;               // UINT/INT/FLOAT/DOUBLE/BOOL/ENUM/CHAR/HEX/RESERVED
    const EnumType*    enum_type  = nullptr;    // resolved on load; may be null
    bool               big_endian  = false;   // "endian": "big" in the JSON —
                                              // byte-offset messages only
    std::optional<std::string> unit;
    std::optional<double>      scale;
    std::string        description;
    bool               is_array    = false;
    std::optional<int> array_count;
};

struct Node {
    std::string         name;
    std::optional<int>  address;                // RS485 device address for EPS
    std::string         description;
};

// EPS power-channel descriptor — which subsystem sits on each output channel.
// Populated from the "channels" array in mib/eps.json (sourced from the
// NL Spacehawk Power Mapping Sheet).
struct ChannelInfo {
    int                        id           = 0;        // 1..32
    std::string                subsystem;               // "OBC-A", "ADCS", "Unused", ...
    std::optional<std::string> voltage;                 // e.g. "Batt+", "12V"
    std::optional<double>      avg_power_w;
    std::optional<double>      peak_power_w;
    std::optional<std::string> input_range;
    std::optional<std::string> owner;                   // team/SME owner
    std::optional<std::string> description;
    bool wired() const { return subsystem != "Unused" && subsystem != "RESERVED"; }
};

struct Message {
    Subsystem            subsystem;
    std::optional<std::string> node;            // EPS only — PDU/BMS/MPPT
    Direction            direction;
    int                  msg_id     = 0;        // numeric id in the subsystem's scheme
    int                  wire_id    = 0;        // byte as seen on the wire
    std::string          param_id;              // spec-level globally-unique id, e.g. "EP-019"
    std::string          name;
    std::string          description;
    std::optional<int>   length_bytes;
    bool                 is_variable_length = false;
    bool                 is_config          = false;
    std::optional<std::string> request_via;     // "LOG" for GPS, else empty
    std::string          offset_unit;           // "bits" | "bytes"
    std::string          doc_reference;
    std::vector<Parameter> parameters;
};

struct SubsystemDef {
    std::string             name;
    std::string             protocol;
    std::string             description;
    std::vector<Node>       nodes;
    std::vector<std::unique_ptr<EnumType>> enums;
    std::vector<Message>    messages;
    std::vector<ChannelInfo> channels;        // EPS power channels (empty for ADCS/GPS/Payload)
};

class Mib {
public:
    // Load a single subsystem JSON file.  May be called multiple times.
    void load(const std::filesystem::path& subsystem_json);

    // Load every *.json in a directory (alphabetical order).
    void load_dir(const std::filesystem::path& dir);

    // ── lookups ───────────────────────────────────────────────────────────
    const Message* find(Subsystem s, Direction d, std::string_view name) const;
    const Message* by_wire(Subsystem s, Direction d, int wire_id) const;
    const Message* by_node_wire(Subsystem s, Direction d,
                                std::optional<std::string_view> node,
                                int wire_id) const;

    int node_address(Subsystem s, std::optional<std::string_view> node) const;
    std::optional<std::string> node_by_address(Subsystem s, int addr) const;

    // EPS channel map (from "channels" array of mib/eps.json).
    // Empty for subsystems with no channel concept.
    const std::vector<ChannelInfo>& channels(Subsystem s) const;
    const ChannelInfo* channel(Subsystem s, int channel_id) const;

    // Find a message by its spec-level param_id (e.g. "EP-019").
    // Accepts loose input: "EP-019", "EP-19", "EP19", "ep-019", "19".
    // For EPS, returns the TC of that pair by default (or the TM if dir set).
    // Returns nullptr if no match or input is ambiguous.
    const Message* by_param_id(Subsystem s, const std::string& input,
                               Direction dir = Direction::TC) const;

    // ── listings ──────────────────────────────────────────────────────────
    const std::vector<Message>& messages(Subsystem s) const;
    std::vector<const Message*> messages(Subsystem s, Direction d) const;

    const SubsystemDef* subsystem(Subsystem s) const;
    std::vector<Subsystem> loaded_subsystems() const;

    struct Counts {
        std::size_t tc = 0, tm = 0, event = 0, params = 0;
    };
    Counts counts() const;

private:
    std::unordered_map<int, SubsystemDef> subs_;   // keyed by int(Subsystem)

    // helpers
    void index(SubsystemDef& def);

    // indexes (key = packed)
    std::unordered_map<std::string, const Message*> by_name_;       // "SUB:DIR:NAME"
    std::unordered_map<std::string, const Message*> by_wire_;       // "SUB:DIR:WIRE"
    std::unordered_map<std::string, const Message*> by_node_wire_;  // "SUB:DIR:NODE:WIRE"
};

} // namespace xdos::mib
