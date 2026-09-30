// SPDX-License-Identifier: Apache-2.0
// XDOS OBC — CCSDS TM frame validator.
//
// The TM counterpart to ccsds_validator.hpp.  Given a raw 1432-byte TM
// downlink frame (SOF FCA1) and the TM codec, produce a per-field report:
//
//   - Decode the frame with the TM codec.
//   - Cross-check every header value against profile expectations
//     (TFVN, SCID, sec-hdr flags, synch, segment id, packet type=TM,
//     APID known, sequence flag, declared packet_data_length).
//   - Re-verify the CRC-16 independently.
//
// Same ValidationField / report shape as the TC validator so the two read
// identically in --validate output.  Reuses ValidationField/Status from
// ccsds_validator.hpp to avoid a parallel type.
#pragma once

#include "ccsds_validator.hpp"          // ValidationField, ValidationReport
#include "codec/ccsds_tm_codec.hpp"
#include "common/types.hpp"

#include <ostream>

namespace xdos::obc {

// Run TM validation.  Does not throw — returns decoded_ok=false on a
// fundamental decode failure (wrong SOF, too short, etc.).
ValidationReport validate_tm_frame(const ccsds::CcsdsTmCodec& codec,
                                   const Bytes& wire);

// Pretty-print a TM validation report (ANSI colour when enabled).  Header line
// is labelled "TM FRAME VALIDATION" to distinguish it from the TC report.
void print_tm_validation_report(std::ostream& os, const ValidationReport& r);

}  // namespace xdos::obc
