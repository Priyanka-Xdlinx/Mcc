// SPDX-License-Identifier: Apache-2.0
// XDOS Ground Station — common type aliases.
#pragma once

#include <array>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xdos {

using Byte  = std::uint8_t;
using Bytes = std::vector<Byte>;

// A view into a buffer of bytes. Used in framing decode paths where we
// don't want to copy.
using ByteView = std::basic_string_view<Byte>;

// Subsystem identifier. We use the documented name everywhere — keeps
// the on-the-wire form decoupled from string literals scattered around.
// SENSOR is appended last on purpose: the enumerator's integer value indexes
// per-subsystem arrays (Bridge stats, Session connection slots), so inserting
// anywhere else would renumber the existing subsystems.
enum class Subsystem { ADCS, EPS, GPS, Payload, XLINK, UHF, SENSOR };

inline const char* to_string(Subsystem s) {
    switch (s) {
        case Subsystem::ADCS:    return "ADCS";
        case Subsystem::EPS:     return "EPS";
        case Subsystem::GPS:     return "GPS";
        case Subsystem::Payload: return "Payload";
        case Subsystem::XLINK:   return "XLINK-S";
        case Subsystem::UHF:     return "UHF";
        case Subsystem::SENSOR:  return "SENSOR";
    }
    return "?";
}

inline std::optional<Subsystem> subsystem_from_string(std::string_view v) {
    if (v == "ADCS")    return Subsystem::ADCS;
    if (v == "EPS")     return Subsystem::EPS;
    if (v == "GPS")     return Subsystem::GPS;
    if (v == "Payload") return Subsystem::Payload;
    if (v == "XLINK-S" || v == "XLINK") return Subsystem::XLINK;
    if (v == "UHF")     return Subsystem::UHF;
    if (v == "SENSOR" || v == "SENSOR_CONTROLLER") return Subsystem::SENSOR;
    return std::nullopt;
}

// Direction of a message in the MIB.
enum class Direction { TC, TM, Event };

inline const char* to_string(Direction d) {
    switch (d) {
        case Direction::TC:    return "TC";
        case Direction::TM:    return "TM";
        case Direction::Event: return "EVENT";
    }
    return "?";
}

inline std::optional<Direction> direction_from_string(std::string_view v) {
    if (v == "TC")    return Direction::TC;
    if (v == "TM")    return Direction::TM;
    if (v == "EVENT") return Direction::Event;
    return std::nullopt;
}

} // namespace xdos
