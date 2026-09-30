// SPDX-License-Identifier: Apache-2.0
// Loader for config/flight.json — power-sequence timings, thresholds, shed
// policy and EGSE endpoints.  All subsystem↔power bindings are by EPS PDU
// CHANNEL ID (the O-numbers of the NL Spacehawk Master_Sheet); display
// labels come from mib/eps.json "channels".  Falls back to compiled
// defaults when the file is absent so binaries stay runnable anywhere.

#ifndef XDOS_COMMON_FLIGHT_CONFIG_HPP
#define XDOS_COMMON_FLIGHT_CONFIG_HPP

#include <nlohmann/json.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace xdos {

struct FlightConfig {
    struct Entry { std::string name; std::vector<int> channels; };

    int obc_boot_s = 10, stagger_s = 2, restore_period_s = 10;

    std::vector<int>   obc_channels { 1, 2 };
    std::vector<Entry> power_menu {
        { "ADCS", {4} }, { "GPS", {3} }, { "SBAND", {5} }, { "PAYLOAD", {10} },
        { "UHF", {11} } };
    std::vector<std::string> default_on_sequence
        { "ADCS", "SBAND", "GPS", "UHF" };

    float thr_full = 12.0f, thr_normal = 11.1f, thr_safe = 9.9f, thr_critical = 9.0f;
    float hysteresis_v = 0.15f;

    std::vector<int> shed_safe_ch     { 10 };
    std::vector<int> shed_critical_ch { 10, 4, 3 };
    std::vector<int> keep_below_ch    { 1, 2 };

    float sunlit_s = 3300.0f, eclipse_s = 2100.0f;

    std::string eps_host = "127.0.0.1"; int eps_port = 10010;
    std::string obc_host = "127.0.0.1"; int obc_port = 10000;

    static std::uint16_t bits(const std::vector<int>& chs) {
        std::uint16_t m = 0;
        for (int c : chs)
            if (c >= 1 && c <= 16) m |= static_cast<std::uint16_t>(1u << (c - 1));
        return m;
    }
    std::uint16_t entry_bits(const std::string& name) const {
        for (const auto& e : power_menu)
            if (e.name == name) return bits(e.channels);
        return 0;
    }

    static FlightConfig load() {
        FlightConfig c;
        namespace fs = std::filesystem;
        fs::path p;
        for (auto cand : { "config/flight.json", "../config/flight.json",
                           "../../config/flight.json" })
            if (fs::exists(cand)) { p = cand; break; }
        if (p.empty()) return c;
        try {
            nlohmann::json j; std::ifstream(p) >> j;
            c.obc_boot_s       = j.value("obc_boot_s", c.obc_boot_s);
            c.stagger_s        = j.value("stagger_s", c.stagger_s);
            c.restore_period_s = j.value("restore_period_s", c.restore_period_s);
            if (j.contains("obc_channels"))
                c.obc_channels = j["obc_channels"].get<std::vector<int>>();
            if (j.contains("power_menu")) {
                c.power_menu.clear();
                for (const auto& e : j["power_menu"])
                    c.power_menu.push_back({ e.value("name", std::string()),
                                             e.value("channels", std::vector<int>{}) });
            }
            if (j.contains("default_on_sequence"))
                c.default_on_sequence =
                    j["default_on_sequence"].get<std::vector<std::string>>();
            if (j.contains("battery_thresholds_v")) {
                auto& t = j["battery_thresholds_v"];
                c.thr_full = t.value("full", c.thr_full);
                c.thr_normal = t.value("normal", c.thr_normal);
                c.thr_safe = t.value("safe", c.thr_safe);
                c.thr_critical = t.value("critical", c.thr_critical);
            }
            c.hysteresis_v = j.value("hysteresis_v", c.hysteresis_v);
            if (j.contains("load_shed")) {
                auto& s = j["load_shed"];
                if (s.contains("safe_channels"))
                    c.shed_safe_ch = s["safe_channels"].get<std::vector<int>>();
                if (s.contains("critical_channels"))
                    c.shed_critical_ch = s["critical_channels"].get<std::vector<int>>();
                if (s.contains("below_critical_keep_channels"))
                    c.keep_below_ch =
                        s["below_critical_keep_channels"].get<std::vector<int>>();
            }
            if (j.contains("orbit")) {
                c.sunlit_s  = j["orbit"].value("sunlit_s", c.sunlit_s);
                c.eclipse_s = j["orbit"].value("eclipse_s", c.eclipse_s);
            }
            if (j.contains("egse")) {
                auto& e = j["egse"];
                c.eps_host = e.value("eps_host", c.eps_host);
                c.eps_port = e.value("eps_port", c.eps_port);
                c.obc_host = e.value("obc_host", c.obc_host);
                c.obc_port = e.value("obc_port", c.obc_port);
            }
        } catch (...) { /* keep defaults on any parse error */ }
        return c;
    }
};

} // namespace xdos
#endif
