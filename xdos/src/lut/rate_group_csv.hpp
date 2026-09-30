// SPDX-License-Identifier: Apache-2.0
// Rate-group command CSV — the operator-editable master list (GROUND-SIDE).
//
// One CSV file declares the ENTIRE rate-group command set; it compiles to
// the binary LUT via BinaryLutBuilder.  Columns:
//
//   tier,tc_id,subsystem,tc_name,picks
//
//   tier       VH | H | M | L (case-insensitive).  Row order within a tier
//              is the entry order in the segment — position is the only
//              OBC<->GS link, so reordering rows changes the layout.
//   tc_id      operator label (EP-003, AD-150, GPS-42, XL-010).  For GPS the
//              trailing number IS the log id (GPS-42 -> log 42).
//   subsystem  ADCS | EPS | GPS | XLINK (XLINK-S accepted).
//   tc_name    EPS/XLINK: the TC name in the Database (response derived as
//              <name>_RSP).  ADCS: the TM name or numeric TM id (the poll IS
//              the TM request).  GPS: always LOG.
//   picks      semicolon-separated, mixable; empty cell = full payload.
//                Runtime (seconds); Control mode   exact parameter names
//                3; 9                              1-based parameter numbers
//                bytes 4:2                         raw range OFFSET:SIZE
//
// Cells may be double-quoted (RFC-4180: embedded commas, doubled "").  The
// header row is skipped when the first cell is "tier".  Every problem is
// reported with its CSV row number; parsing continues so one run surfaces
// as many errors as possible.

#pragma once

#include "mib/mib.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace xdos::lut {

struct CsvResult {
    nlohmann::json source;               // BinaryLutBuilder source (tiers -> commands)
    std::vector<std::string> errors;     // each prefixed "row N: "
    std::vector<std::string> warnings;
    bool ok() const { return errors.empty(); }
};

CsvResult parse_rate_group_csv_text(const std::string& text, const mib::Mib& mib);
CsvResult parse_rate_group_csv_file(const std::string& path, const mib::Mib& mib);

} // namespace xdos::lut
