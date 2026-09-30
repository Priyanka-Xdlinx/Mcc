// SPDX-License-Identifier: Apache-2.0
// XDOS OBC — CCSDS frame validator implementation.
#include "ccsds_validator.hpp"
#include "common/color.hpp"

#include <cstdio>
#include <iomanip>

namespace xdos::obc {

using ccsds::CcsdsDirection;
using ccsds::DecodedFrame;

// Compact helpers for building report fields.
namespace {
    using S = ValidationField::Status;

    template <typename T>
    std::string hex(T v, int width) {
        char b[32];
        std::snprintf(b, sizeof(b), "0x%0*X", width, static_cast<unsigned>(v));
        return std::string(b);
    }

    std::string dec(int v) {
        return std::to_string(v);
    }

    ValidationField mk(const std::string& name, const std::string& got,
                       const std::string& expected, S status,
                       const std::string& note = "") {
        return ValidationField{name, got, expected, status, note};
    }

    std::string bytes_hex(const Bytes& b, std::size_t max = 32) {
        std::string out;
        std::size_t n = std::min(b.size(), max);
        for (std::size_t i = 0; i < n; ++i) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%02X ", b[i]);
            out += buf;
        }
        if (b.size() > max) out += "...";
        if (!out.empty() && out.back() == ' ') out.pop_back();
        return out;
    }
}

ValidationReport validate_ccsds_frame(const ccsds::CcsdsCodec& codec,
                                      const Bytes& wire) {
    ValidationReport r;
    r.frame_size = wire.size();

    // ─── Fundamental decode ────────────────────────────────────────────────
    auto d_opt = codec.decode(wire);
    if (!d_opt) {
        r.fields.push_back(mk("Frame decode",
            "FAILED",
            "SOF + min size + EOF",
            S::FAIL,
            "Either wrong SOF, too short (<30 bytes), or other decode error."));
        ++r.fail_count;
        return r;
    }
    r.decoded_ok = true;
    const DecodedFrame& d = *d_opt;
    r.crc_ok = d.crc_ok;

    // Track running tallies as we go.
    auto add = [&](ValidationField f) {
        switch (f.status) {
            case S::OK:   ++r.ok_count;   break;
            case S::WARN: ++r.warn_count; break;
            case S::FAIL: ++r.fail_count; break;
            case S::INFO: break;
        }
        r.fields.push_back(std::move(f));
    };

    // ─── SOF ───────────────────────────────────────────────────────────────
    {
        std::string got = (wire.size() >= 2)
            ? hex((wire[0] << 8) | wire[1], 4) : std::string("?");
        bool ok = codec.looks_like_ccsds(wire);
        add(mk("SOF", got, "0x1ACF",
               ok ? S::OK : S::FAIL,
               ok ? "" : "Start-of-frame marker does not match profile."));
    }

    // ─── TF Transfer Frame Primary Header ──────────────────────────────────
    add(mk("TF version",          dec(d.tf_version_number),       "0",
           d.tf_version_number == 0 ? S::OK : S::FAIL,
           d.tf_version_number == 0 ? "" : "CCSDS 232 requires version 00."));

    add(mk("Bypass flag",         dec(d.bypass_flag),
           "0 or 1", S::INFO,
           d.bypass_flag ? "Type-B expedited" : "Type-A sequence-controlled"));

    add(mk("Control command flag", dec(d.control_command_flag),
           "0 or 1", S::INFO,
           d.control_command_flag ? "Type-C Control Info"
                                  : "Type-D Frame Data Unit"));

    add(mk("SCID",                hex(d.spacecraft_id, 3),
           hex(codec.spacecraft_id(), 3),
           d.spacecraft_id == codec.spacecraft_id() ? S::OK : S::FAIL,
           d.spacecraft_id == codec.spacecraft_id()
               ? ""
               : "Spacecraft ID does not match this ground station's profile."));

    add(mk("VCID",                dec(d.virtual_channel_id),
           dec(codec.default_vcid()),
           d.virtual_channel_id == codec.default_vcid() ? S::OK : S::WARN,
           d.virtual_channel_id == codec.default_vcid()
               ? ""
               : "Non-default virtual channel — may be intentional."));

    // Frame length cross-check: header says (frame_length + 1) octets,
    // and that should equal (total wire bytes - 2 for SOF).
    {
        int declared = d.frame_length + 1;
        int actual   = static_cast<int>(wire.size()) - 2;   // exclude SOF
        bool ok = declared == actual;
        add(mk("Frame length",
               dec(d.frame_length) + " (declared " + dec(declared)
                   + " octets after SOF)",
               dec(actual) + " (actual)",
               ok ? S::OK : S::FAIL,
               ok ? ""
                  : "Declared length disagrees with actual byte count."));
    }

    add(mk("Frame sequence",      dec(d.frame_sequence_num),
           "0..255", S::INFO,
           "Modulo-256 counter, advances per TC."));

    // ─── Space Packet Primary Header ───────────────────────────────────────
    add(mk("Packet version",      dec(d.packet_version),         "0",
           d.packet_version == 0 ? S::OK : S::FAIL,
           d.packet_version == 0 ? "" : "CCSDS 133 requires version 000."));

    {
        std::string got = (d.direction == CcsdsDirection::TC) ? "1 (TC)" : "0 (TM)";
        // For an inbound frame on the GS→OBC side it should be TC.
        bool ok = (d.direction == CcsdsDirection::TC);
        add(mk("Packet type",     got, "1 (TC for uplink)",
               ok ? S::OK : S::WARN,
               ok ? "" : "Direction bit indicates TM, not TC — unexpected on uplink."));
    }

    add(mk("Sec hdr flag",        dec(d.secondary_header ? 1 : 0), "1",
           d.secondary_header ? S::OK : S::FAIL,
           d.secondary_header ? "" : "Profile requires secondary header present."));

    // APID
    {
        auto sub = codec.apid_to_subsystem(d.apid);
        bool known = sub.has_value();
        std::string got = hex(d.apid, 3);
        std::string exp = "ADCS=0x064, EPS=0x0C8, GPS=0x12C, Payload=0x190";
        std::string note;
        if (known) {
            note = std::string("Resolved to subsystem ")
                 + std::string(to_string(*sub));
        } else {
            note = "APID not in profile apid_map — frame cannot be routed.";
        }
        add(mk("APID", got, exp, known ? S::OK : S::FAIL, note));
    }

    add(mk("Sequence flag",       dec(d.sequence_flag),           "3 (unsegmented)",
           d.sequence_flag == 3 ? S::OK : S::WARN,
           d.sequence_flag == 3 ? ""
                                : "Frame is segmented — multi-packet TC."));

    add(mk("Packet sequence",     dec(d.packet_sequence_cnt),
           "0..16383", S::INFO,
           "Per-APID 14-bit counter."));

    // Packet data length cross-check.
    // The packet data field begins right after the 6-byte Pkt Primary Header.
    // It contains: GS-ID(1) + Time(8) + AncLen(2) + AncData(N1) + TC-ID(2) + TC-Len(2) + TC-Data(N2)
    // Declared in header as (octets - 1).  Computed expected count = total
    // pkt-data-field bytes = (whole frame body) - 5 (TF hdr) - 6 (Pkt hdr) - 2 (CRC).
    {
        int body         = static_cast<int>(wire.size()) - 2;   // exclude SOF
        int expected_dat = body - 5 - 6 - 2;                    // exclude TF hdr, Pkt hdr, CRC
        int declared     = d.packet_data_length + 1;
        bool ok = declared == expected_dat;
        add(mk("Packet data length",
               dec(d.packet_data_length) + " (declared " + dec(declared) + " octets)",
               dec(expected_dat) + " (actual)",
               ok ? S::OK : S::FAIL,
               ok ? "" : "Declared packet data length disagrees with frame size."));
    }

    // ─── Packet Secondary Header ───────────────────────────────────────────
    add(mk("Ground Station ID",   hex(d.ground_station_id, 2),
           hex(codec.ground_station_id(), 2),
           d.ground_station_id == codec.ground_station_id() ? S::OK : S::WARN,
           d.ground_station_id == codec.ground_station_id()
               ? ""
               : "GS-ID differs from this OBC's profile (may be from another GS)."));

    {
        // Time code field — print human-readable date.
        char tbuf[64];
        std::snprintf(tbuf, sizeof(tbuf),
                      "%04u-%02u-%02u %02u:%02u:%02u.%02u0",
                      d.time_code.year, d.time_code.month, d.time_code.day,
                      d.time_code.hour, d.time_code.minute, d.time_code.second,
                      d.time_code.centiseconds);

        // Basic sanity: year reasonable, month 1..12, etc.
        bool sane = (d.time_code.year >= 2020 && d.time_code.year <= 2099) &&
                    (d.time_code.month  >= 1 && d.time_code.month  <= 12) &&
                    (d.time_code.day    >= 1 && d.time_code.day    <= 31) &&
                    (d.time_code.hour          <= 23) &&
                    (d.time_code.minute        <= 59) &&
                    (d.time_code.second        <= 60) &&
                    (d.time_code.centiseconds  <= 99);
        add(mk("Time code",       tbuf, "valid date/time",
               sane ? S::OK : S::WARN,
               sane ? "Centiseconds = ms/10."
                    : "One or more time fields out of range — clock not set?"));
    }

    add(mk("Anc data length",     dec(d.ancillary_data_length),
           "0 (no scheduler)", S::INFO,
           d.ancillary_data_length == 0
               ? "No ancillary data."
               : "Ancillary scheduler data present."));

    // ─── User Data ─────────────────────────────────────────────────────────
    add(mk("TC ID",               hex(d.tc_id, 4),                  "MIB wire-id",
           S::INFO,
           "Telecommand identifier from the subsystem MIB."));

    {
        int declared = d.tc_len;
        int actual   = static_cast<int>(d.tc_data.size());
        bool ok = declared == actual;
        add(mk("TC Len",          dec(declared) + " (declared)",
               dec(actual) + " (actual)",
               ok ? S::OK : S::FAIL,
               ok ? "" : "Declared TC Len disagrees with TC Data byte count."));
    }

    add(mk("TC Data",             bytes_hex(d.tc_data),
           "subsystem payload",
           S::INFO,
           "Inner native frame (current Phase-1 hybrid)."));

    // ─── CRC ───────────────────────────────────────────────────────────────
    add(mk("CRC received",        hex(d.crc_received, 4),
           hex(d.crc_computed, 4),
           d.crc_ok ? S::OK : S::FAIL,
           d.crc_ok ? "CRC-16 CCITT (poly 0x1021, init 0xFFFF) matches."
                    : "CRC mismatch — frame corrupted in transit."));

    return r;
}

// ─── Pretty printer ────────────────────────────────────────────────────────
static const char* status_tag(ValidationField::Status s) {
    switch (s) {
        case ValidationField::Status::OK:   return "OK  ";
        case ValidationField::Status::WARN: return "WARN";
        case ValidationField::Status::FAIL: return "FAIL";
        case ValidationField::Status::INFO: return "    ";
    }
    return "    ";
}

static std::string colorize(const std::string& s, ValidationField::Status st) {
    switch (st) {
        case ValidationField::Status::OK:   return color::ok(s);
        case ValidationField::Status::WARN: return color::warn(s);
        case ValidationField::Status::FAIL: return color::err(s);
        case ValidationField::Status::INFO: return s;
    }
    return s;
}

void print_validation_report(std::ostream& os, const ValidationReport& r) {
    os << "\n";
    os << "  ┌─ CCSDS frame validation  ("
       << r.frame_size << " bytes) ─────────────────────────\n";

    if (!r.decoded_ok) {
        os << "  │  " << color::err("Frame failed fundamental decode — cannot parse fields") << "\n";
        os << "  └────────────────────────────────────────────────────────────\n";
        return;
    }

    for (const auto& f : r.fields) {
        std::string tag    = status_tag(f.status);
        std::string tagged = colorize("[" + tag + "]", f.status);

        char line[256];
        std::snprintf(line, sizeof(line),
                      "  │ %s  %-20s : %-32s",
                      tagged.c_str(), f.name.c_str(), f.got.c_str());
        os << line;
        if (!f.expected.empty()) os << "exp " << f.expected;
        os << "\n";
        if (!f.note.empty() && f.status != ValidationField::Status::INFO) {
            os << "  │              \u2937 " << f.note << "\n";
        }
    }

    os << "  ├──── Summary ────\n";
    os << "  │   OK   : " << r.ok_count   << "\n";
    os << "  │   WARN : " << r.warn_count << "\n";
    os << "  │   FAIL : " << r.fail_count << "\n";
    if (r.all_ok()) {
        os << "  │   " << color::ok("VERDICT: frame is valid against profile") << "\n";
    } else {
        os << "  │   " << color::err("VERDICT: frame has violations — see FAILs above") << "\n";
    }
    os << "  \u2514\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\n";
}

}  // namespace xdos::obc
