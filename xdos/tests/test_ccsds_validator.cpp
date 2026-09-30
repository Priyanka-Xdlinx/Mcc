// SPDX-License-Identifier: Apache-2.0
// Validator tests.  Run isolated: ./build/xdos_tests "[validator]"
#include "codec/ccsds_codec.hpp"
#include "ccsds_validator.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <sstream>

using namespace xdos;
using namespace xdos::ccsds;
using namespace xdos::obc;

static std::string profile_path() {
    for (const auto& p : { "mib/ccsds_tc.json",
                           "../mib/ccsds_tc.json",
                           "../../mib/ccsds_tc.json" }) {
        if (std::filesystem::exists(p)) return p;
    }
    throw std::runtime_error("test: cannot find mib/ccsds_tc.json");
}

static const ValidationField* find_field(const ValidationReport& r,
                                         const std::string& name) {
    for (const auto& f : r.fields) if (f.name == name) return &f;
    return nullptr;
}

TEST_CASE("Validator passes a clean ADCS TC frame", "[validator]") {
    CcsdsCodec codec; codec.load(profile_path());

    Bytes native = {0x1F, 0x7F, 0x80, 0x1F, 0xFF};
    Bytes wire = codec.encode(Subsystem::ADCS, CcsdsDirection::TC, 0x0080, native);

    auto r = validate_ccsds_frame(codec, wire);
    REQUIRE(r.decoded_ok);
    REQUIRE(r.all_ok());
    REQUIRE(r.fail_count == 0);

    auto* sof = find_field(r, "SOF");
    REQUIRE(sof != nullptr);
    REQUIRE(sof->status == ValidationField::Status::OK);

    auto* scid = find_field(r, "SCID");
    REQUIRE(scid != nullptr);
    REQUIRE(scid->got == "0x271");

    auto* crc = find_field(r, "CRC received");
    REQUIRE(crc != nullptr);
    REQUIRE(crc->status == ValidationField::Status::OK);
}

TEST_CASE("Validator catches CRC corruption", "[validator]") {
    CcsdsCodec codec; codec.load(profile_path());
    Bytes native = {0x1F, 0x7F, 0x80, 0x1F, 0xFF};
    Bytes wire = codec.encode(Subsystem::ADCS, CcsdsDirection::TC, 0x0080, native);

    // Flip a bit in the user_data area
    wire[20] ^= 0x01;
    auto r = validate_ccsds_frame(codec, wire);
    REQUIRE(r.decoded_ok);
    REQUIRE_FALSE(r.all_ok());
    auto* crc = find_field(r, "CRC received");
    REQUIRE(crc != nullptr);
    REQUIRE(crc->status == ValidationField::Status::FAIL);
}

TEST_CASE("Validator catches wrong SOF (returns decode failure)", "[validator]") {
    CcsdsCodec codec; codec.load(profile_path());

    Bytes wire(40, Byte{0});
    wire[0] = 0xDE; wire[1] = 0xAD;            // wrong SOF
    auto r = validate_ccsds_frame(codec, wire);
    REQUIRE_FALSE(r.decoded_ok);
    REQUIRE(r.fail_count >= 1);
}

TEST_CASE("Validator catches length mismatch", "[validator]") {
    CcsdsCodec codec; codec.load(profile_path());
    Bytes native = {0x1F, 0x7F, 0x80, 0x1F, 0xFF};
    Bytes wire = codec.encode(Subsystem::ADCS, CcsdsDirection::TC, 0x0080, native);

    // Truncate by 2 bytes — drop CRC bytes.  Decode may return data,
    // but length cross-check should fail.
    Bytes shortw(wire.begin(), wire.end() - 2);
    auto r = validate_ccsds_frame(codec, shortw);
    // The decoded length field will say more than the actual byte count.
    // The validator should flag this.
    if (r.decoded_ok) {
        auto* fl = find_field(r, "Frame length");
        REQUIRE(fl != nullptr);
        REQUIRE(fl->status == ValidationField::Status::FAIL);
    }
}

TEST_CASE("Validator gives readable text output", "[validator]") {
    CcsdsCodec codec; codec.load(profile_path());
    Bytes native = {0x1F, 0x7F, 0x80, 0x1F, 0xFF};
    Bytes wire = codec.encode(Subsystem::ADCS, CcsdsDirection::TC, 0x0080, native);

    auto r = validate_ccsds_frame(codec, wire);
    std::ostringstream os;
    print_validation_report(os, r);
    std::string text = os.str();
    REQUIRE(text.find("CCSDS frame validation") != std::string::npos);
    REQUIRE(text.find("VERDICT") != std::string::npos);
    REQUIRE(text.find("SCID")   != std::string::npos);
    REQUIRE(text.find("CRC")    != std::string::npos);
}
