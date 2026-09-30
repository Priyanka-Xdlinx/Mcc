// SPDX-License-Identifier: Apache-2.0
// XDOS OBC — CCSDS frame validator.
//
// Given a raw CCSDS-wrapped frame (as bytes off the wire) and the codec
// that knows the profile, produce a per-field validation report:
//
//   - Decode the frame using the codec.
//   - Cross-check every header value against profile expectations
//     (SCID, APID known, packet type, sec hdr flag, etc).
//   - Cross-check declared lengths against actual byte counts.
//   - Re-verify the CRC independently.
//
// The report can be printed to stdout (human-readable) and/or programmatically
// inspected (each field has a name, value, expected, status).
#pragma once

#include "codec/ccsds_codec.hpp"
#include "common/types.hpp"

#include <ostream>
#include <string>
#include <vector>

namespace xdos::obc {

struct ValidationField {
    std::string name;            // "SCID", "Packet Type", etc.
    std::string got;             // "0x271", "1 (TC)"
    std::string expected;        // "0x271" or "" if no expectation
    enum class Status { OK, WARN, FAIL, INFO };
    Status      status = Status::INFO;
    std::string note;            // optional explanation when not OK
};

struct ValidationReport {
    bool                          decoded_ok = false;
    bool                          crc_ok     = false;
    std::size_t                   frame_size = 0;
    std::vector<ValidationField>  fields;

    // Counts for quick summary
    int ok_count   = 0;
    int warn_count = 0;
    int fail_count = 0;

    bool all_ok() const { return decoded_ok && fail_count == 0; }
};

// Run validation.  Does not throw — returns report.decoded_ok=false on
// fundamental decode failure (SOF wrong, too short, etc.).
ValidationReport validate_ccsds_frame(const ccsds::CcsdsCodec& codec,
                                      const Bytes& wire);

// Pretty-print the report to a stream (uses ANSI colour if color::use_color()
// is true).
void print_validation_report(std::ostream& os, const ValidationReport& r);

}  // namespace xdos::obc
