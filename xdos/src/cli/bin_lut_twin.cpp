// SPDX-License-Identifier: Apache-2.0
#include "cli/bin_lut_twin.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

using nlohmann::json;

namespace xdos::cli {

bool BinLutTwin::load_file(const std::string& path, BinLutTwin& out,
                           std::string* err) {
    out = BinLutTwin{};
    std::ifstream f(path);
    if (!f) { if (err) *err = "cannot open " + path; return false; }
    json j;
    try { f >> j; } catch (const std::exception& e) {
        if (err) *err = std::string("bad JSON: ") + e.what();
        return false;
    }
    try {
        out.crc16         = j.value("crc16", 0);
        out.subframe_size = j.value("subframe_size", lut::kSubframeSize);
        if (!j.contains("tiers") || !j["tiers"].is_object()) {
            if (err) *err = "twin has no \"tiers\" object";
            return false;
        }
        for (int t = 0; t < 4; ++t) {
            for (const auto& je : j["tiers"].value(lut::kTierName[t],
                                                   json::array())) {
                TwinEntry e;
                e.id  = je.value("id", "");
                e.tc  = je.value("tc", "");
                e.rsp = je.value("rsp", "");
                e.apid = je.value("apid", 0);
                e.tm_raw_len = je.value("tm_raw_len", 0);
                e.gps_log_id = je.value("gps_log_id", -1);
                auto sub = subsystem_from_string(je.value("subsystem", ""));
                if (!sub) {
                    if (err) *err = "twin entry " + e.id
                                    + ": unknown subsystem";
                    return false;
                }
                e.sub = *sub;
                for (const auto& jp : je.value("picks", json::array())) {
                    TwinPick p;
                    p.src   = jp.value("src", 0);
                    p.size  = jp.value("size", 0);
                    p.block = jp.value("block", 0);
                    for (const auto& n : jp.value("params", json::array()))
                        p.params.push_back(n.get<std::string>());
                    e.picks.push_back(std::move(p));
                }
                out.tiers[t].push_back(std::move(e));
            }
        }
        return true;
    } catch (const std::exception& e) {
        if (err) *err = std::string("twin parse error: ") + e.what();
        return false;
    }
}

}  // namespace xdos::cli
