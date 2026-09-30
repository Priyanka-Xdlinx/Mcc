// SPDX-License-Identifier: Apache-2.0
// XDOS OBC — CCSDS TM frame validator implementation.
#include "ccsds_tm_validator.hpp"
#include "common/color.hpp"

#include <cstdio>
#include <string>

namespace xdos::obc {

using ccsds::DecodedTmFrame;

namespace {
    using S = ValidationField::Status;

    template <typename T>
    std::string hex(T v, int width) {
        char b[32];
        std::snprintf(b, sizeof(b), "0x%0*X", width, static_cast<unsigned>(v));
        return std::string(b);
    }
    std::string dec(int v) { return std::to_string(v); }

    ValidationField mk(const std::string& name, const std::string& got,
                       const std::string& expected, S status,
                       const std::string& note = "") {
        return ValidationField{name, got, expected, status, note};
    }

    const char* status_tag(ValidationField::Status s) {
        switch (s) {
            case ValidationField::Status::OK:   return "OK  ";
            case ValidationField::Status::WARN: return "WARN";
            case ValidationField::Status::FAIL: return "FAIL";
            case ValidationField::Status::INFO: return "    ";
        }
        return "    ";
    }
    std::string colorize(const std::string& s, ValidationField::Status st) {
        switch (st) {
            case ValidationField::Status::OK:   return color::ok(s);
            case ValidationField::Status::WARN: return color::warn(s);
            case ValidationField::Status::FAIL: return color::err(s);
            case ValidationField::Status::INFO: return s;
        }
        return s;
    }
}  // namespace

ValidationReport validate_tm_frame(const ccsds::CcsdsTmCodec& codec,
                                   const Bytes& wire) {
    ValidationReport r;
    r.frame_size = wire.size();

    auto d_opt = codec.decode(wire);
    if (!d_opt) {
        r.fields.push_back(mk("Frame decode", "FAILED",
            "SOF FCA1 + 1432 bytes", S::FAIL,
            "Wrong SOF, short frame (<1432), or decode error."));
        ++r.fail_count;
        return r;
    }
    r.decoded_ok = true;
    const DecodedTmFrame& d = *d_opt;
    r.crc_ok = d.crc_ok;

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
        bool ok = codec.looks_like_tm(wire);
        add(mk("SOF", got, "0xFCA1", ok ? S::OK : S::FAIL,
               ok ? "" : "Start-of-frame marker does not match TM profile."));
    }

    // ─── Frame length ──────────────────────────────────────────────────────
    {
        bool ok = (static_cast<int>(wire.size()) == codec.frame_length());
        add(mk("Frame length", dec(static_cast<int>(wire.size())) + " B",
               dec(codec.frame_length()) + " B", ok ? S::OK : S::FAIL,
               ok ? "" : "TM frames are fixed length."));
    }

    // ─── TM Primary Header ─────────────────────────────────────────────────
    add(mk("TF Version", dec(d.tf_version_number), "0",
           d.tf_version_number == 0 ? S::OK : S::FAIL));
    {
        bool ok = (d.spacecraft_id == codec.spacecraft_id());
        add(mk("Spacecraft ID", hex(d.spacecraft_id, 3),
               hex(codec.spacecraft_id(), 3), ok ? S::OK : S::WARN,
               ok ? "" : "SCID differs from profile."));
    }
    add(mk("Radio Module ID", dec(d.radio_module_id), "", S::INFO,
           "RM-ID (VCID slot); interim value."));
    add(mk("Master frame count", dec(d.master_frame_count), "", S::INFO));
    add(mk("Sub frame count", dec(d.sub_frame_count), "", S::INFO,
           "Per-APID counter."));

    // ─── Data Field Status ─────────────────────────────────────────────────
    add(mk("Sec-hdr flag (TF)", dec(d.secondary_header_flag ? 1 : 0), "1",
           d.secondary_header_flag ? S::OK : S::FAIL));
    add(mk("Synch flag", dec(d.synch_flag), "0",
           d.synch_flag == 0 ? S::OK : S::WARN,
           d.synch_flag == 0 ? "" : "Non-zero synch flag: octet stream undefined."));
    add(mk("Packet order flag", dec(d.packet_order_flag), "0",
           d.packet_order_flag == 0 ? S::OK : S::WARN));
    add(mk("Segment length id", dec(d.segment_length_id), "3",
           d.segment_length_id == 3 ? S::OK : S::WARN,
           d.segment_length_id == 3 ? "" : "Expected 3 (no segmentation)."));
    add(mk("First header ptr", dec(d.first_header_pointer), "0",
           d.first_header_pointer == 0 ? S::OK : S::WARN,
           d.first_header_pointer == 0 ? "" : "Non-zero: packet does not start at byte 0."));

    // ─── SPP Primary Header ────────────────────────────────────────────────
    add(mk("Packet version", dec(d.packet_version), "0",
           d.packet_version == 0 ? S::OK : S::FAIL));
    add(mk("Packet type", dec(d.packet_type) + " (TM)", "0 (TM)",
           d.packet_type == 0 ? S::OK : S::FAIL,
           d.packet_type == 0 ? "" : "TM frames must carry packet type 0."));
    add(mk("Sec-hdr flag (SPP)", dec(d.spp_secondary_flag ? 1 : 0), "1",
           d.spp_secondary_flag ? S::OK : S::FAIL));
    {
        auto sub = codec.apid_to_subsystem(d.apid);
        std::string got = hex(d.apid, 3) +
            (sub ? " (" + std::string(to_string(*sub)) + ")" : " (unknown)");
        add(mk("APID", got, "known subsystem", sub ? S::OK : S::WARN,
               sub ? "" : "APID does not resolve to a known subsystem."));
    }
    add(mk("Sequence flag", dec(d.sequence_flag), "3",
           d.sequence_flag == 3 ? S::OK : S::WARN,
           d.sequence_flag == 3 ? "" : "Expected 3 (unsegmented)."));
    add(mk("Packet seq count", dec(d.packet_sequence_cnt), "", S::INFO));
    {
        // Fixed geometry: sec-hdr(11) + TM DATA(1280) + echo(123) − 1 = 1413.
        const int expected = 11 + static_cast<int>(ccsds::CcsdsTmCodec::kTmDataLen)
                           + static_cast<int>(ccsds::CcsdsTmCodec::kEchoLen) - 1;
        bool ok = (d.packet_data_length == expected);
        add(mk("Packet data length", dec(d.packet_data_length), dec(expected),
               ok ? S::OK : S::FAIL,
               ok ? "" : "Declared length disagrees with fixed frame geometry."));
    }

    // ─── SPP Secondary Header ──────────────────────────────────────────────
    add(mk("Ground station ID", dec(d.ground_station_id), "", S::INFO));
    {
        char tb[64];
        std::snprintf(tb, sizeof(tb), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                      d.time_code.year, d.time_code.month, d.time_code.day,
                      d.time_code.hour, d.time_code.minute, d.time_code.second,
                      d.time_code.milliseconds);
        bool sane = d.time_code.month >= 1 && d.time_code.month <= 12 &&
                    d.time_code.day >= 1 && d.time_code.day <= 31 &&
                    d.time_code.hour < 24 && d.time_code.minute < 60 &&
                    d.time_code.second < 60 && d.time_code.milliseconds < 1000;
        add(mk("Time code (UTC)", tb, "valid CCS", sane ? S::OK : S::WARN,
               sane ? "" : "Time field out of range."));
    }
    add(mk("RSV", hex(d.rsv, 4), "0x0000",
           d.rsv == 0 ? S::OK : S::WARN,
           d.rsv == 0 ? "" : "Reserved field should be zero."));

    // ─── User data field sizes ─────────────────────────────────────────────
    add(mk("TM DATA size", dec(static_cast<int>(d.tm_data.size())) + " B",
           dec(static_cast<int>(ccsds::CcsdsTmCodec::kTmDataLen)) + " B",
           d.tm_data.size() == ccsds::CcsdsTmCodec::kTmDataLen ? S::OK : S::FAIL));
    add(mk("GS TC echo size", dec(static_cast<int>(d.gs_tc_cmd.size())) + " B",
           dec(static_cast<int>(ccsds::CcsdsTmCodec::kEchoLen)) + " B",
           d.gs_tc_cmd.size() == ccsds::CcsdsTmCodec::kEchoLen ? S::OK : S::FAIL));

    // ─── CRC ───────────────────────────────────────────────────────────────
    add(mk("CRC-16", hex(d.crc_received, 4),
           hex(d.crc_computed, 4), d.crc_ok ? S::OK : S::FAIL,
           d.crc_ok ? "" : "CRC mismatch — frame corrupt or mis-encoded."));

    return r;
}

void print_tm_validation_report(std::ostream& os, const ValidationReport& r) {
    os << "\n";
    os << "  ┌─ TM frame validation  ("
       << r.frame_size << " bytes) ──────────────────────────\n";

    if (!r.decoded_ok) {
        os << "  │  " << color::err("Frame failed fundamental decode — cannot parse fields") << "\n";
        os << "  └────────────────────────────────────────────────────────────\n";
        return;
    }

    for (const auto& f : r.fields) {
        std::string tagged = colorize("[" + std::string(status_tag(f.status)) + "]", f.status);
        char line[256];
        std::snprintf(line, sizeof(line), "  │ %s  %-20s : %-32s",
                      tagged.c_str(), f.name.c_str(), f.got.c_str());
        os << line;
        if (!f.expected.empty()) os << "exp " << f.expected;
        os << "\n";
        if (!f.note.empty() && f.status != ValidationField::Status::INFO)
            os << "  │              \u2937 " << f.note << "\n";
    }

    os << "  ├──── Summary ────\n";
    os << "  │   OK   : " << r.ok_count   << "\n";
    os << "  │   WARN : " << r.warn_count << "\n";
    os << "  │   FAIL : " << r.fail_count << "\n";
    if (r.all_ok())
        os << "  │   " << color::ok("VERDICT: TM frame is valid against profile") << "\n";
    else
        os << "  │   " << color::err("VERDICT: TM frame has violations — see FAILs above") << "\n";
    os << "  └────────────────────────────────────────────────────────────\n";
}

}  // namespace xdos::obc
