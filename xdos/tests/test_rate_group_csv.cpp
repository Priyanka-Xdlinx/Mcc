// SPDX-License-Identifier: Apache-2.0
// Rate-group command CSV parser — grammar, resolution, and row-level errors.
#include <catch2/catch_test_macros.hpp>

#include "lut/binary_lut_builder.hpp"
#include "lut/rate_group_csv.hpp"
#include "mib/mib.hpp"

#include <filesystem>

using namespace xdos;
using namespace xdos::lut;
using nlohmann::json;

static const mib::Mib& test_mib() {
    static mib::Mib m; static bool loaded = false;
    if (!loaded) {
        for (auto p : { "mib", "../mib", "../../mib" })
            if (std::filesystem::is_directory(p)) { m.load_dir(p); break; }
        loaded = true;
    }
    return m;
}

TEST_CASE("CSV: header row, blank lines, per-subsystem resolution", "[csv]") {
    const std::string text =
        "tier,tc_id,subsystem,tc_name,picks\n"
        "\n"
        "VH,AD-150,ADCS,Control and estimation mode,\n"
        "VH,EP-003,EPS,Get_PDU_CH_State,\n"
        "H,GPS-42,GPS,LOG,\n"
        "L,XL-008,XLINK,HOUSE_KEEPING,\n";
    auto r = parse_rate_group_csv_text(text, test_mib());
    for (auto& e : r.errors) INFO(e);
    REQUIRE(r.ok());
    REQUIRE(r.source["tiers"]["VH"].size() == 2);
    REQUIRE(r.source["tiers"]["H"].size() == 1);
    REQUIRE(r.source["tiers"]["L"].size() == 1);
    // ADCS: tc == rsp == the TM name
    REQUIRE(r.source["tiers"]["VH"][0]["tc"] == "Control and estimation mode");
    REQUIRE(r.source["tiers"]["VH"][0]["rsp"] == "Control and estimation mode");
    // EPS: rsp derived
    REQUIRE(r.source["tiers"]["VH"][1]["rsp"] == "Get_PDU_CH_State_RSP");
    // GPS: log id from the tc_id, response resolved from the Database
    REQUIRE(r.source["tiers"]["H"][0]["gps_log_id"] == 42);
    REQUIRE(r.source["tiers"]["H"][0]["rsp"] == "BESTPOS");
    // full-payload picks are empty arrays
    REQUIRE(r.source["tiers"]["H"][0]["picks"].empty());
    // and the whole thing builds clean
    BinaryLutBuilder b(test_mib());
    auto br = b.build(r.source);
    for (auto& e : br.errors) INFO(e);
    REQUIRE(br.ok());
}

TEST_CASE("CSV: ADCS numeric TM id resolves like the composer", "[csv]") {
    auto r = parse_rate_group_csv_text("VH,,ADCS,150,\n", test_mib());
    for (auto& e : r.errors) INFO(e);
    REQUIRE(r.ok());
    REQUIRE(r.source["tiers"]["VH"][0]["rsp"] == "Control and estimation mode");
    REQUIRE(r.source["tiers"]["VH"][0]["id"] == "AD-150");   // synthesized
}

TEST_CASE("CSV: quoting — embedded commas, doubled quotes, semicolon picks", "[csv]") {
    const std::string text =
        "tier,tc_id,subsystem,tc_name,picks\n"
        "H,EP-016,EPS,Get_PDU_Voltage_Readings,\"Batt Voltage Reading; bytes 0:4\"\n";
    auto r = parse_rate_group_csv_text(text, test_mib());
    for (auto& e : r.errors) INFO(e);
    REQUIRE(r.ok());
    const auto& picks = r.source["tiers"]["H"][0]["picks"];
    REQUIRE(picks.size() == 2);                    // one bytes + one params pick
    bool have_bytes = false, have_param = false;
    for (const auto& p : picks) {
        if (p.contains("bytes"))  { have_bytes = true;
                                    REQUIRE(p["bytes"][0] == 0);
                                    REQUIRE(p["bytes"][1] == 4); }
        if (p.contains("params")) { have_param = true;
                                    REQUIRE(p["params"][0] == "Batt Voltage Reading"); }
    }
    REQUIRE(have_bytes);
    REQUIRE(have_param);

    // a doubled quote inside a quoted cell survives
    auto r2 = parse_rate_group_csv_text(
        "VH,\"EP\"\"3\",EPS,Get_PDU_CH_State,\n", test_mib());
    REQUIRE(r2.ok());
    REQUIRE(r2.source["tiers"]["VH"][0]["id"] == "EP\"3");
}

TEST_CASE("CSV: numeric pick tokens are 1-based parameter numbers", "[csv]") {
    // Get_PDU_Voltage_Readings_RSP param #5 = Batt Voltage Reading @16
    auto r = parse_rate_group_csv_text(
        "H,EP-016,EPS,Get_PDU_Voltage_Readings,5\n", test_mib());
    for (auto& e : r.errors) INFO(e);
    REQUIRE(r.ok());
    const auto& picks = r.source["tiers"]["H"][0]["picks"];
    REQUIRE(picks.size() == 1);
    REQUIRE(picks[0]["params"][0] == "Batt Voltage Reading");
}

TEST_CASE("CSV: row-numbered errors, parsing continues past them", "[csv]") {
    const std::string text =
        "tier,tc_id,subsystem,tc_name,picks\n"          // row 1
        "XX,EP-003,EPS,Get_PDU_CH_State,\n"             // row 2: bad tier
        "VH,EP-003,MARS,Get_PDU_CH_State,\n"            // row 3: bad subsystem
        "VH,EP-999,EPS,No_Such_Command,\n"              // row 4: unknown TC
        "H,GPS-42,GPS,LOG,\n"                           // row 5: fine
        "H,GPS-9999,GPS,LOG,\n"                         // row 6: unknown log id
        "M,EP-016,EPS,Get_PDU_Voltage_Readings,No Such Param\n"  // row 7
        "M,EP-016,EPS,Get_PDU_Voltage_Readings,bytes 18:99\n";   // row 8: range
    auto r = parse_rate_group_csv_text(text, test_mib());
    REQUIRE_FALSE(r.ok());
    REQUIRE(r.errors.size() == 6);
    auto has = [&](const std::string& pfx, const std::string& sub) {
        for (const auto& e : r.errors)
            if (e.rfind(pfx, 0) == 0 && e.find(sub) != std::string::npos)
                return true;
        return false;
    };
    REQUIRE(has("row 2:", "unknown tier"));
    REQUIRE(has("row 3:", "unknown subsystem"));
    REQUIRE(has("row 4:", "not in the Database"));
    REQUIRE(has("row 6:", "not in the Database"));
    REQUIRE(has("row 7:", "not in"));
    REQUIRE(has("row 8:", "outside"));
    // the good row still parsed
    REQUIRE(r.source["tiers"]["H"].size() == 1);
    // duplicate-id warning path
    auto r2 = parse_rate_group_csv_text(
        "VH,EP-003,EPS,Get_PDU_CH_State,\nH,EP-003,EPS,Get_PDU_CH_State,\n",
        test_mib());
    REQUIRE(r2.ok());
    REQUIRE(r2.warnings.size() == 1);
}

TEST_CASE("CSV: the shipped example compiles to a valid LUT", "[csv]") {
    std::string path;
    for (auto p : { "config/rate_group_commands.csv",
                    "../config/rate_group_commands.csv",
                    "../../config/rate_group_commands.csv" })
        if (std::filesystem::exists(p)) { path = p; break; }
    REQUIRE_FALSE(path.empty());
    auto r = parse_rate_group_csv_file(path, test_mib());
    for (auto& e : r.errors) INFO(e);
    REQUIRE(r.ok());
    BinaryLutBuilder b(test_mib());
    auto br = b.build(r.source);
    for (auto& e : br.errors) INFO(e);
    REQUIRE(br.ok());
    // every tier populated, all budgets closed
    for (int t = 0; t < 4; ++t) {
        REQUIRE_FALSE(br.segs[t].entries.empty());
        REQUIRE(br.segs[t].used <= br.segs[t].budget);
    }
}
