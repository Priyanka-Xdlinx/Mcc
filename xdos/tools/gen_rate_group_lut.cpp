// SPDX-License-Identifier: Apache-2.0
// Rate-group LUT generator (GROUND-SIDE tool).
//
// This represents ground engineering: it reads the Database (which lives on
// the ground), pre-builds the exact native TC bytes for each rate-group
// command, and writes the LUT JSON that the OBC replays.  Run it to produce
// the default table, or to regenerate after editing the command set — then
// uplink the resulting file to the OBC.
//
//   build:  compiled as the `xdos_gen_rate_lut` target
//   usage:  xdos_gen_rate_lut [mib_dir] [out.json]
//           defaults: mib dir autodetected, out = config/rate_group_lut.json
//
// The command set + order is defined here (the ground's master list).  For
// each entry we resolve its TC + response size from the Database and build the
// native TC bytes with the SAME encoders the live stack uses, so the replayed
// bytes are byte-identical to a real command.

#include "common/types.hpp"
#include "mib/mib.hpp"
#include "codec/codec.hpp"
#include "framing/eps_framing.hpp"
#include "framing/gps_framing.hpp"
#include "subsystems/xlink_codec.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

using namespace xdos;
using nlohmann::json;

namespace {

// The ground's master rate-group list: id, subsystem, how to request, and the
// TM response whose payload gets stored.  (SB = XLink-S band.)
struct GenCmd {
    std::string id;
    Subsystem   sub;
    std::string tc_name;      // TC to build ("Get_*", "LOG", XLink event name)
    std::string rsp_name;     // TM response (for resp_size lookup)
    int         gps_log_id;   // GPS: which log to request; -1 otherwise
};

const std::vector<GenCmd> kMasterList = {
    { "EP-003", Subsystem::EPS,   "Get_PDU_CH_State",                 "Get_PDU_CH_State_RSP",                 -1 },
    { "EP-006", Subsystem::EPS,   "Get_PDU_CH_Current_Readings",      "Get_PDU_CH_Current_Readings_RSP",      -1 },
    { "EP-015", Subsystem::EPS,   "Get_PDU_Alert_Flags",              "Get_PDU_Alert_Flags_RSP",              -1 },
    { "GPS-42", Subsystem::GPS,   "LOG",                              "BESTPOS",                              42 },
    { "XL-010", Subsystem::XLINK, "DEVICE_CONF",                      "DEVICE_CONF_RSP",                      -1 },
    { "EP-016", Subsystem::EPS,   "Get_PDU_Voltage_Readings",         "Get_PDU_Voltage_Readings_RSP",         -1 },
    { "EP-019", Subsystem::EPS,   "BMS_Get_Batt_V_and_I_Readings",    "BMS_Get_Batt_V_and_I_Readings_RSP",    -1 },
    { "EP-021", Subsystem::EPS,   "BMS_Get_Battery_Heater_Param",     "BMS_Get_Battery_Heater_Param_RSP",     -1 },
    { "GPS-99", Subsystem::GPS,   "LOG",                              "BESTVEL",                              99 },
    { "XL-008", Subsystem::XLINK, "HOUSE_KEEPING",                    "HOUSE_KEEPING_RSP",                    -1 },
    { "EP-033", Subsystem::EPS,   "MPPT_Get_MPPT_State",              "MPPT_Get_MPPT_State_RSP",              -1 },
    { "EP-034", Subsystem::EPS,   "Get_MPPT_Voltage_Current_Readings","Get_MPPT_Voltage_Current_Readings_RSP",-1 },
    { "GPS-963",Subsystem::GPS,   "LOG",                              "HWMONITOR",                            963 },
};

std::string to_hex(const Bytes& b) {
    std::ostringstream o;
    for (Byte x : b) o << std::setw(2) << std::setfill('0') << std::hex << (int)x;
    return o.str();
}

int resp_size(const mib::Mib& mib, const GenCmd& c) {
    const mib::Message* m = mib.find(c.sub, Direction::TM, c.rsp_name);
    if (!m) return -1;
    if (m->length_bytes) return *m->length_bytes;
    int end = 0; const bool bits = (m->offset_unit == "bits");
    for (const auto& p : m->parameters) {
        if (!p.offset) continue;
        int e = bits ? (*p.offset + p.length + 7) / 8 : *p.offset + p.length;
        if (e > end) end = e;
    }
    return end;
}

// Build the exact native TC bytes for one command, using the live encoders.
Bytes build_tc(const mib::Mib& mib, const GenCmd& c, std::string& err) {
    if (c.sub == Subsystem::EPS) {
        const mib::Message* tc = mib.find(Subsystem::EPS, Direction::TC, c.tc_name);
        if (!tc) { err = "EPS TC not found: " + c.tc_name; return {}; }
        // Node address comes from the Database (PDU1=1, BMS1=9, MPPT1=12).
        int node = 1;
        if (tc->node == "BMS1")  node = 9;
        else if (tc->node == "MPPT1") node = 12;
        Bytes payload = codec::encode(*tc, json::object());
        return framing::eps::encode(static_cast<Byte>(node),
                                    static_cast<Byte>(tc->wire_id), payload);
    }
    if (c.sub == Subsystem::GPS) {
        const mib::Message* logtc = mib.find(Subsystem::GPS, Direction::TC, "LOG");
        if (!logtc) { err = "GPS LOG TC not found"; return {}; }
        json wrap = { {"port",0}, {"message_id", c.gps_log_id}, {"message_type",0},
                      {"reserved",0}, {"trigger",4}, {"period",0.0},
                      {"offset",0.0}, {"hold",0} };
        Bytes payload = codec::encode(*logtc, wrap);
        return framing::gps::encode(static_cast<std::uint16_t>(logtc->wire_id), payload);
    }
    if (c.sub == Subsystem::XLINK) {
        const mib::Message* tc = mib.find(Subsystem::XLINK, Direction::TC, c.tc_name);
        if (!tc) { err = "XLINK TC not found: " + c.tc_name; return {}; }
        xlink::codec::ProductKey key{};   // default key placeholder
        return xlink::codec::encode_frame(key, tc->msg_id,
                                          xlink::codec::FLAG_GET, *tc, json::object());
    }
    err = "unsupported subsystem";
    return {};
}

std::uint16_t signature(const std::vector<int>& sizes) {
    std::uint16_t sig = 0x1F35; int off = 0;
    for (int sz : sizes) {
        sig = static_cast<std::uint16_t>((sig << 3) | (sig >> 13));
        sig ^= static_cast<std::uint16_t>(off * 131 + sz);
        off += sz;
    }
    return sig;
}

}  // namespace

int main(int argc, char** argv) {
    std::string mib_dir, out_path = "config/rate_group_lut.json";
    if (argc > 1) mib_dir = argv[1];
    if (argc > 2) out_path = argv[2];

    if (mib_dir.empty()) {
        for (auto p : { "mib", "../mib", "../../mib" })
            if (std::filesystem::is_directory(p)) { mib_dir = p; break; }
    }
    if (mib_dir.empty()) {
        std::fprintf(stderr, "gen_rate_lut: no Database directory found\n");
        return 1;
    }
    mib::Mib mib;
    mib.load_dir(mib_dir);

    json obc_cmds = json::array();   // OBC side: NO id (memory-minimal)
    json gs_cmds  = json::array();   // GS side: id + response name for decode
    std::vector<int> sizes;
    int order = 1;
    for (const auto& c : kMasterList) {
        std::string err;
        Bytes tc = build_tc(mib, c, err);
        if (tc.empty() && !err.empty()) {
            std::fprintf(stderr, "gen_rate_lut: %s\n", err.c_str());
            return 1;
        }
        int rs = resp_size(mib, c);
        if (rs < 0) {
            std::fprintf(stderr, "gen_rate_lut: no resp size for %s\n", c.id.c_str());
            return 1;
        }
        sizes.push_back(rs);
        std::string subname = std::string(to_string(c.sub)) == "XLINK-S"
                                ? "XLINK" : std::string(to_string(c.sub));
        // OBC row — position, subsystem, bytes to replay, bytes to store. No id.
        obc_cmds.push_back({
            { "order", order },
            { "subsystem", subname },
            { "tc_hex", to_hex(tc) },
            { "resp_size", rs },
        });
        // GS row — the label + response definition, matched by the same order.
        gs_cmds.push_back({
            { "order", order },
            { "id", c.id },
            { "subsystem", subname },
            { "resp_size", rs },
            { "rsp_name", c.rsp_name },
        });
        ++order;
    }

    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tmv{};
#if defined(_WIN32)
    gmtime_s(&tmv, &now);
#else
    gmtime_r(&now, &tmv);
#endif
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tmv);

    std::uint16_t sig = signature(sizes);
    int total = 0; for (int sz : sizes) total += sz;

    // OBC LUT — the minimal table that ships to / uplinks to the OBC (no id).
    json obc_lut = {
        { "_comment", "OBC-side rate-group LUT (NO command id — memory-minimal). "
                      "OBC replays tc_hex and stores resp_size bytes, by order. "
                      "Uplink this file to the OBC." },
        { "revision", 1 },
        { "generated", ts },
        { "signature", sig },
        { "tm_data_bytes", 1280 },
        { "commands", obc_cmds },
    };
    // GS LUT — full table kept on the ground for decoding (id + response name),
    // matched to the OBC table by 'order' and cross-checked by 'signature'.
    json gs_lut = {
        { "_comment", "GS-side rate-group LUT (labels + response names for "
                      "decode). Matches the OBC LUT by 'order'; same 'signature'. "
                      "Keep on the ground; do NOT uplink." },
        { "revision", 1 },
        { "generated", ts },
        { "signature", sig },
        { "tm_data_bytes", 1280 },
        { "commands", gs_cmds },
    };

    // out_path is the OBC file; derive the GS filename next to it.
    std::filesystem::path op(out_path);
    std::filesystem::create_directories(op.parent_path());
    std::filesystem::path gp = op.parent_path() /
        (op.stem().string() + "_gs" + op.extension().string());

    { std::ofstream o(out_path); o << obc_lut.dump(2) << "\n"; }
    { std::ofstream o(gp.string()); o << gs_lut.dump(2) << "\n"; }

    std::fprintf(stderr,
        "gen_rate_lut: wrote OBC LUT %s and GS LUT %s "
        "(%zu commands, %d payload bytes, sig 0x%04X)\n",
        out_path.c_str(), gp.string().c_str(),
        kMasterList.size(), total, sig);
    return 0;
}
