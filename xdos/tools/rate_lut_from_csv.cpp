// SPDX-License-Identifier: Apache-2.0
// Rate-group CSV -> binary LUT compiler (GROUND-SIDE tool).
//
// The operator maintains ONE CSV listing every rate-group command
// (tier,tc_id,subsystem,tc_name,picks — see src/lut/rate_group_csv.hpp for
// the grammar).  This tool resolves it against the Database, builds the
// native TC bytes with the live encoders, and writes both artifacts:
//
//   config/rate_group_lut.bin          the OBC binary — the ONLY uplink
//   config/rate_group_lut_bin_gs.json  the GS decode twin — never transmitted
//
//   build:  compiled as the `xdos_lut_csv` target
//   usage:  xdos_lut_csv <commands.csv> [--out-bin P] [--out-twin P]
//                        [--mib DIR] [--zero-xlink-key]
//
// The build is deterministic: identical CSV + Database (+ key) give a
// byte-identical file, so the CRC-16 printed at the end is quotable.

#include "lut/binary_lut_builder.hpp"
#include "lut/rate_group_csv.hpp"
#include "mib/mib.hpp"
#include "subsystems/xlink_keys.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

using namespace xdos;
namespace fs = std::filesystem;

static std::string find_dir(std::initializer_list<const char*> cands) {
    for (auto p : cands)
        if (fs::is_directory(p)) return p;
    return "";
}

int main(int argc, char** argv) {
    std::string csv_path, mib_dir, out_bin, out_twin;
    bool zero_key = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      (a == "--mib"      && i+1 < argc) mib_dir  = argv[++i];
        else if (a == "--out-bin"  && i+1 < argc) out_bin  = argv[++i];
        else if (a == "--out-twin" && i+1 < argc) out_twin = argv[++i];
        else if (a == "--zero-xlink-key")         zero_key = true;
        else if (a == "--help" || a == "-h" || a[0] == '-') {
            std::fprintf(stderr,
                "xdos_lut_csv — compile the rate-group command CSV to the binary LUT\n"
                "  usage: xdos_lut_csv <commands.csv> [--out-bin P] [--out-twin P]\n"
                "                      [--mib DIR] [--zero-xlink-key]\n");
            return a == "--help" || a == "-h" ? 0 : 1;
        }
        else csv_path = a;
    }
    if (csv_path.empty()) {
        std::fprintf(stderr, "xdos_lut_csv: no CSV given (try --help)\n");
        return 1;
    }
    if (mib_dir.empty()) mib_dir = find_dir({ "mib", "../mib", "../../mib" });
    if (mib_dir.empty()) {
        std::fprintf(stderr, "xdos_lut_csv: no Database directory found\n");
        return 1;
    }
    std::string cfg_dir = find_dir({ "config", "../config", "../../config" });
    if (cfg_dir.empty()) cfg_dir = ".";
    if (out_bin.empty())
        out_bin = (fs::path(cfg_dir) / "rate_group_lut.bin").string();
    if (out_twin.empty())
        out_twin = (fs::path(cfg_dir) / "rate_group_lut_bin_gs.json").string();

    mib::Mib mib;
    mib.load_dir(mib_dir);

    lut::BinaryLutBuilder bld(mib);

    // XLINK product key: first entry of the vendor catalog if present.
    // The zero fallback is accepted by the simulator only.
    if (!zero_key) {
        bool loaded = false;
        auto keys_dir = fs::path(mib_dir) / "xlink_keys";
        if (fs::is_directory(keys_dir)) {
            for (auto const& e : fs::directory_iterator(keys_dir)) {
                if (e.path().extension() != ".txt") continue;
                auto cat = xlink::KeyCatalog::load(e.path().string());
                if (cat && !cat->entries.empty()) {
                    bld.set_xlink_key(cat->entries.front().key);
                    std::fprintf(stderr,
                        "xdos_lut_csv: XLINK key %s... (SN %s) from %s\n",
                        cat->entries.front().raw_key_text.substr(0, 8).c_str(),
                        cat->serial_number.c_str(),
                        e.path().filename().string().c_str());
                    loaded = true;
                    break;
                }
            }
        }
        if (!loaded)
            std::fprintf(stderr, "xdos_lut_csv: WARNING no key catalog in "
                         "%s/xlink_keys — XLINK entries use the all-zero key "
                         "(sim only)\n", mib_dir.c_str());
    } else {
        std::fprintf(stderr, "xdos_lut_csv: XLINK key forced to zero (sim only)\n");
    }

    auto parsed = lut::parse_rate_group_csv_file(csv_path, mib);
    for (const auto& w : parsed.warnings)
        std::fprintf(stderr, "xdos_lut_csv: warning: %s\n", w.c_str());
    if (!parsed.ok()) {
        for (const auto& e : parsed.errors)
            std::fprintf(stderr, "xdos_lut_csv: %s\n", e.c_str());
        return 1;
    }

    auto r = bld.build(parsed.source);
    for (int t = 0; t < 4; ++t)
        std::fprintf(stderr, "  %-2s  %2zu cmds   %4d / %4d B\n",
                     lut::kTierName[t], r.segs[t].entries.size(),
                     r.segs[t].used, r.segs[t].budget);
    for (const auto& w : r.warnings)
        std::fprintf(stderr, "xdos_lut_csv: warning: %s\n", w.c_str());
    if (!r.ok()) {
        for (const auto& e : r.errors)
            std::fprintf(stderr, "xdos_lut_csv: %s\n", e.c_str());
        return 1;
    }

    std::string werr;
    if (!lut::BinaryLutBuilder::write(r, out_bin, out_twin, &werr)) {
        std::fprintf(stderr, "xdos_lut_csv: %s\n", werr.c_str());
        return 1;
    }
    std::fprintf(stderr,
        "xdos_lut_csv: wrote %s (%zu B, CRC-16 0x%04X) and %s\n",
        out_bin.c_str(), r.file.size(), r.crc, out_twin.c_str());
    return 0;
}
