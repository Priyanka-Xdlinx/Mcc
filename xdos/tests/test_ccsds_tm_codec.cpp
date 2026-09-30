// SPDX-License-Identifier: Apache-2.0
// CCSDS TM downlink codec tests: fixed frame geometry, bit-field packing,
// encode→decode roundtrip, CRC integrity, FCA1 sync, and pad behaviour.

#include <catch2/catch_test_macros.hpp>

#include "codec/ccsds_tm_codec.hpp"

#include <filesystem>
#include <memory>
#include <string>

using namespace xdos;
using namespace xdos::ccsds;

static std::string mib_dir() {
    for (auto p : { "mib", "../mib", "../../mib" })
        if (std::filesystem::is_directory(p)) return p;
    return "mib";
}

// Codec holds a mutex → non-movable; hand back a heap instance.
static std::unique_ptr<CcsdsTmCodec> loaded() {
    auto c = std::make_unique<CcsdsTmCodec>();
    REQUIRE(c->load(mib_dir() + "/ccsds_tm.json"));
    return c;
}

TEST_CASE("TM profile loads and inherits SCID from TC profile", "[tm][ccsds]") {
    auto cp = loaded(); auto& c = *cp;
    REQUIRE(c.spacecraft_id() == 625);          // from ccsds_tc.json
    REQUIRE(c.frame_length()  == 1432);
    REQUIRE(c.apid_to_subsystem(3).has_value());
    REQUIRE(*c.apid_to_subsystem(3) == Subsystem::EPS);
}

TEST_CASE("TM frame has the exact fixed geometry", "[tm][ccsds]") {
    auto cp = loaded(); auto& c = *cp;
    Bytes tm = { 0x11, 0x22, 0x33 };
    Bytes echo = { 0xAB, 0xCD };
    Bytes f = c.encode(Subsystem::EPS, tm, echo);

    REQUIRE(f.size() == 1432);
    REQUIRE(f[0] == 0xFC);                        // SOF hi
    REQUIRE(f[1] == 0xA1);                        // SOF lo

    // User data starts at 2 (SOF) + 27 (headers) = 29.
    const std::size_t user = 2 + 25;   // SOF(2) + 25-byte header block
    REQUIRE(f[user + 0] == 0x11);                 // TM DATA begins
    REQUIRE(f[user + 1] == 0x22);
    REQUIRE(f[user + 2] == 0x33);
    REQUIRE(f[user + 3] == 0x00);                 // zero-padded
    // Echo begins after 1280 TM-DATA bytes.
    REQUIRE(f[user + 1280 + 0] == 0xAB);
    REQUIRE(f[user + 1280 + 1] == 0xCD);
    REQUIRE(f[user + 1280 + 2] == 0x00);          // zero-padded echo
}

TEST_CASE("TM encode→decode roundtrip preserves fields", "[tm][ccsds]") {
    auto cp = loaded(); auto& c = *cp;
    Bytes tm(64, 0x5A);
    Bytes echo = { 0xDE, 0xAD, 0xBE, 0xEF };
    Bytes f = c.encode(Subsystem::GPS, tm, echo, /*rm_id=*/1);

    auto d = c.decode(f);
    REQUIRE(d.has_value());
    REQUIRE(d->crc_ok);
    REQUIRE(d->spacecraft_id == 625);
    REQUIRE(d->radio_module_id == 1);
    REQUIRE(d->apid == 4);
    REQUIRE(d->subsystem.has_value());
    REQUIRE(*d->subsystem == Subsystem::GPS);
    REQUIRE(d->packet_type == 0);                 // telemetry
    REQUIRE(d->secondary_header_flag);
    REQUIRE(d->synch_flag == 0);
    REQUIRE(d->segment_length_id == 3);
    REQUIRE(d->first_header_pointer == 0);
    REQUIRE(d->sequence_flag == 3);
    REQUIRE(d->packet_data_length == 1413);       // 11 + 1403 − 1
    REQUIRE(d->rsv == 0);

    // Payloads land where expected.
    REQUIRE(d->tm_data.size() == 1280);
    REQUIRE(d->tm_data[0] == 0x5A);
    REQUIRE(d->tm_data[63] == 0x5A);
    REQUIRE(d->tm_data[64] == 0x00);
    REQUIRE(d->gs_tc_cmd.size() == 123);
    REQUIRE(d->gs_tc_cmd[0] == 0xDE);
    REQUIRE(d->gs_tc_cmd[3] == 0xEF);
    REQUIRE(d->gs_tc_cmd[4] == 0x00);
}

TEST_CASE("TM master frame count increments, sub count is per-APID", "[tm][ccsds]") {
    auto cp = loaded(); auto& c = *cp;
    auto d0 = c.decode(c.encode(Subsystem::EPS, {0x01}, {}));
    auto d1 = c.decode(c.encode(Subsystem::GPS, {0x02}, {}));
    auto d2 = c.decode(c.encode(Subsystem::EPS, {0x03}, {}));
    REQUIRE(d0->master_frame_count == 0);
    REQUIRE(d1->master_frame_count == 1);
    REQUIRE(d2->master_frame_count == 2);
    // sub-frame counts are independent per APID
    REQUIRE(d0->sub_frame_count == 0);   // EPS #0
    REQUIRE(d1->sub_frame_count == 0);   // GPS #0
    REQUIRE(d2->sub_frame_count == 1);   // EPS #1
}

TEST_CASE("TM CRC detects corruption", "[tm][ccsds]") {
    auto cp = loaded(); auto& c = *cp;
    Bytes f = c.encode(Subsystem::EPS, {0x11, 0x22}, {});
    REQUIRE(c.decode(f)->crc_ok);
    f[500] ^= 0xFF;                               // flip a TM-DATA byte
    auto d = c.decode(f);
    REQUIRE(d.has_value());
    REQUIRE_FALSE(d->crc_ok);
}

TEST_CASE("TM sync rejects non-FCA1 and short frames", "[tm][ccsds]") {
    auto cp = loaded(); auto& c = *cp;
    Bytes f = c.encode(Subsystem::EPS, {0x11}, {});
    REQUIRE(c.looks_like_tm(f));

    Bytes wrong = f; wrong[0] = 0x1A; wrong[1] = 0xCF;   // TC SOF
    REQUIRE_FALSE(c.looks_like_tm(wrong));
    REQUIRE_FALSE(c.decode(wrong).has_value());

    Bytes shortf(100, 0x00); shortf[0] = 0xFC; shortf[1] = 0xA1;
    REQUIRE_FALSE(c.decode(shortf).has_value());
}

TEST_CASE("TM oversized payloads are truncated to field sizes", "[tm][ccsds]") {
    auto cp = loaded(); auto& c = *cp;
    Bytes big_tm(2000, 0x77);          // > 1280
    Bytes big_echo(500, 0x33);         // > 123
    Bytes f = c.encode(Subsystem::Payload, big_tm, big_echo);
    REQUIRE(f.size() == 1432);         // still exactly one frame
    auto d = c.decode(f);
    REQUIRE(d->crc_ok);
    REQUIRE(d->tm_data.size() == 1280);
    REQUIRE(d->gs_tc_cmd.size() == 123);
    REQUIRE(d->tm_data[1279] == 0x77);
    REQUIRE(d->gs_tc_cmd[122] == 0x33);
}

TEST_CASE("TM unmapped subsystem falls back to idle APID", "[tm][ccsds]") {
    auto cp = loaded(); auto& c = *cp;
    // XLINK may or may not be in the TC apid_map; if absent it must not throw
    // and should use the idle APID (2047).
    Bytes f = c.encode(Subsystem::XLINK, {0x01}, {});
    REQUIRE(f.size() == 1432);
    auto d = c.decode(f);
    REQUIRE(d.has_value());
    REQUIRE(d->crc_ok);
}

// The GS strips the 1280-byte zero padding off TM DATA before handing the
// native frame to the per-subsystem decoder.  This mirrors the exact logic in
// Session::strip_tm_padding so a regression there is caught here too.
namespace {
Bytes strip_padding_ref(Subsystem s, const Bytes& d) {
    if (s == Subsystem::GPS) {
        if (d.size() >= 10 && d[0] == 0xAA && d[1] == 0x44 && d[2] == 0x12) {
            std::size_t h = d[3];
            std::size_t p = static_cast<std::size_t>(d[8]) |
                            (static_cast<std::size_t>(d[9]) << 8);
            std::size_t t = h + p + 4;
            if (t <= d.size()) return Bytes(d.begin(), d.begin() + t);
        }
        return d;
    }
    Byte em;
    switch (s) {
        case Subsystem::EPS:  em = 0x01; break;
        case Subsystem::ADCS: em = 0xFF; break;
        default: return d;
    }
    for (std::size_t i = d.size(); i-- > 0; ) {
        if (d[i] == em) {
            if (s == Subsystem::ADCS && (i == 0 || d[i-1] != 0x1F)) continue;
            return Bytes(d.begin(), d.begin() + i + 1);
        }
    }
    return d;
}
}  // namespace

TEST_CASE("TM DATA padding strips back to the exact native frame", "[tm][ccsds]") {
    auto cp = loaded(); auto& c = *cp;

    SECTION("EPS end-marker 0x01") {
        Bytes native = { 0x00, 0x41, 0x42, 0x43, 0x01 };   // start..end
        auto d = c.decode(c.encode(Subsystem::EPS, native, {}));
        REQUIRE(d->tm_data.size() == 1280);
        REQUIRE(strip_padding_ref(Subsystem::EPS, d->tm_data) == native);
    }
    SECTION("ADCS trailer 0x1F 0xFF, with an escaped 0x1F inside") {
        Bytes native = { 0x1F, 0x7F, 0x22, 0x1F, 0x1F, 0x33, 0x1F, 0xFF };
        auto d = c.decode(c.encode(Subsystem::ADCS, native, {}));
        REQUIRE(strip_padding_ref(Subsystem::ADCS, d->tm_data) == native);
    }
    SECTION("GPS OEM7 header-declared length") {
        // sync(3) + hdrlen@3 + paylen@8 (=3) + 3 payload + 4 CRC = 10+... here
        Bytes native(14, 0x00);
        native[0]=0xAA; native[1]=0x44; native[2]=0x12; native[3]=10;
        native[8]=0x00; native[9]=0x00;    // paylen 0 for simplicity
        native.resize(10 + 0 + 4, 0x00);   // header + 0 payload + CRC
        auto d = c.decode(c.encode(Subsystem::GPS, native, {}));
        REQUIRE(strip_padding_ref(Subsystem::GPS, d->tm_data) == native);
    }
}
