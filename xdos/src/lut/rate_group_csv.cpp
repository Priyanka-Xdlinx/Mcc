// SPDX-License-Identifier: Apache-2.0
#include "lut/rate_group_csv.hpp"

#include "lut/binary_lut_builder.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <sstream>

using nlohmann::json;

namespace xdos::lut {

namespace {

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string upper(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

std::optional<int> parse_int(const std::string& s) {
    if (s.empty()) return std::nullopt;
    for (char c : s) if (!std::isdigit((unsigned char)c)) return std::nullopt;
    try { return std::stoi(s); } catch (...) { return std::nullopt; }
}

// One CSV record per source row (RFC-4180 minimal: quoted cells, doubled "",
// CRLF).  Newlines inside quotes are kept in the cell.
struct CsvRow { int line = 0; std::vector<std::string> cells; };

std::vector<CsvRow> split_csv(const std::string& text) {
    std::vector<CsvRow> rows;
    CsvRow row; row.line = 1;
    std::string cell;
    bool in_quotes = false, row_has_data = false;
    int line = 1;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i+1] == '"') { cell += '"'; ++i; }
                else in_quotes = false;
            } else { cell += c; if (c == '\n') ++line; }
            continue;
        }
        if (c == '"') { in_quotes = true; row_has_data = true; }
        else if (c == ',') { row.cells.push_back(cell); cell.clear(); row_has_data = true; }
        else if (c == '\n') {
            ++line;
            if (row_has_data || !trim(cell).empty()) {
                row.cells.push_back(cell);
                rows.push_back(std::move(row));
            }
            row = CsvRow{}; row.line = line;
            cell.clear(); row_has_data = false;
        } else if (c != '\r') { cell += c; row_has_data = true; }
    }
    if (row_has_data || !trim(cell).empty()) {
        row.cells.push_back(cell);
        rows.push_back(std::move(row));
    }
    return rows;
}

int tier_index(const std::string& t) {
    for (int i = 0; i < 4; ++i) if (t == kTierName[i]) return i;
    return -1;
}

// Trailing number in a tc_id label: "GPS-42" -> 42, "963" -> 963.
std::optional<int> trailing_number(const std::string& id) {
    size_t end = id.size();
    while (end > 0 && !std::isdigit((unsigned char)id[end-1])) --end;
    size_t start = end;
    while (start > 0 && std::isdigit((unsigned char)id[start-1])) --start;
    if (start == end) return std::nullopt;
    return parse_int(id.substr(start, end - start));
}

}  // namespace

CsvResult parse_rate_group_csv_text(const std::string& text,
                                    const mib::Mib& mib) {
    CsvResult r;
    r.source = { {"version", 1}, {"tiers", { {"VH", json::array()},
                 {"H", json::array()}, {"M", json::array()},
                 {"L", json::array()} }} };
    BinaryLutBuilder bld(mib);

    auto rows = split_csv(text);
    std::vector<std::string> seen_ids;
    bool first_data_row = true;
    for (const auto& row : rows) {
        auto cell = [&](size_t i) {
            return i < row.cells.size() ? trim(row.cells[i]) : std::string();
        };
        const std::string rowpfx = "row " + std::to_string(row.line) + ": ";
        auto rerr = [&](const std::string& m) { r.errors.push_back(rowpfx + m); };

        // header row: first non-blank row whose first cell is "tier"
        if (first_data_row) {
            first_data_row = false;
            std::string c0 = cell(0);
            std::transform(c0.begin(), c0.end(), c0.begin(),
                           [](unsigned char ch){ return (char)std::tolower(ch); });
            if (c0 == "tier") continue;
        }
        if (row.cells.size() < 4) {
            rerr("expected 5 columns (tier,tc_id,subsystem,tc_name,picks), got "
                 + std::to_string(row.cells.size()));
            continue;
        }
        const std::string tier = upper(cell(0));
        std::string id         = cell(1);
        const std::string ss   = upper(cell(2));
        const std::string tc   = cell(3);
        const std::string pks  = cell(4);

        const int t = tier_index(tier);
        if (t < 0) { rerr("unknown tier \"" + cell(0) + "\" (use VH/H/M/L)"); continue; }

        Subsystem sub;
        if      (ss == "ADCS")                    sub = Subsystem::ADCS;
        else if (ss == "EPS")                     sub = Subsystem::EPS;
        else if (ss == "GPS")                     sub = Subsystem::GPS;
        else if (ss == "XLINK" || ss == "XLINK-S") sub = Subsystem::XLINK;
        else if (ss == "UHF")                     sub = Subsystem::UHF;
        else { rerr("unknown subsystem \"" + cell(2) + "\""); continue; }

        json je = { {"subsystem", ss == "XLINK-S" ? "XLINK" : ss} };
        std::string rsp;
        if (sub == Subsystem::ADCS) {
            // the poll IS a TM request — tc_name is the TM name or numeric id
            const mib::Message* tm = nullptr;
            if (auto n = parse_int(tc)) {
                for (const auto* m : mib.messages(Subsystem::ADCS, Direction::TM))
                    if (m->msg_id == *n) { tm = m; break; }
                if (!tm) { rerr("ADCS TM id " + tc + " not in the Database"); continue; }
            } else {
                tm = mib.find(Subsystem::ADCS, Direction::TM, tc);
                if (!tm) { rerr("ADCS TM not in the Database: \"" + tc + "\""); continue; }
            }
            rsp = tm->name;
            if (id.empty()) id = "AD-" + std::to_string(tm->msg_id);
            je["tc"] = rsp; je["rsp"] = rsp;
        } else if (sub == Subsystem::GPS) {
            if (upper(tc) != "LOG") { rerr("GPS tc_name must be LOG"); continue; }
            auto logid = trailing_number(id);
            if (!logid) { rerr("GPS tc_id must carry the log id (e.g. GPS-42)"); continue; }
            const mib::Message* tm = nullptr;
            for (const auto* m : mib.messages(Subsystem::GPS, Direction::TM))
                if (m->msg_id == *logid) { tm = m; break; }
            if (!tm) { rerr("GPS log id " + std::to_string(*logid)
                            + " not in the Database"); continue; }
            rsp = tm->name;
            je["tc"] = "LOG"; je["rsp"] = rsp;
            je["gps_log_id"] = *logid;
        } else {
            // EPS / XLINK: TC by name, response is <name>_RSP.
            // UHF: replies drop the GET_ prefix instead (GET_TELEM -> TELEM).
            const mib::Message* tcm = mib.find(sub, Direction::TC, tc);
            if (!tcm) { rerr(ss + " TC not in the Database: \"" + tc + "\""); continue; }
            rsp = (sub == Subsystem::UHF && tc.rfind("GET_", 0) == 0)
                      ? tc.substr(4) : tc + "_RSP";
            if (!mib.find(sub, Direction::TM, rsp)) {
                rerr(ss + " TC \"" + tc + "\" has no " + rsp + " response TM");
                continue;
            }
            if (id.empty())
                id = std::string(sub == Subsystem::EPS ? "EP-"
                               : sub == Subsystem::UHF ? "UH-" : "XL-")
                     + std::to_string(tcm->msg_id);
            je["tc"] = tc; je["rsp"] = rsp;
        }
        je["id"] = id;
        if (std::find(seen_ids.begin(), seen_ids.end(), id) != seen_ids.end())
            r.warnings.push_back(rowpfx + "duplicate tc_id \"" + id + "\"");
        seen_ids.push_back(id);

        // picks: semicolon-separated names / numbers / "bytes OFF:SIZE"
        auto pmap = bld.response_param_map(sub, rsp);
        const int rlen = bld.raw_response_len(sub, rsp);
        json picks = json::array();
        json params = json::array();
        bool pick_err = false;
        std::stringstream ssv(pks); std::string tok;
        while (std::getline(ssv, tok, ';')) {
            tok = trim(tok);
            if (tok.empty()) continue;
            if (tok.rfind("bytes", 0) == 0) {
                int off = 0, szv = 0;
                if (std::sscanf(tok.c_str(), "bytes %d:%d", &off, &szv) == 2) {
                    if (off < 0 || szv <= 0 || off + szv > rlen) {
                        rerr("bytes pick " + tok + " outside 0.."
                             + std::to_string(rlen) + " of " + rsp);
                        pick_err = true;
                    } else picks.push_back({ {"bytes", json::array({off, szv})} });
                } else { rerr("bad bytes pick \"" + tok + "\" (use bytes OFF:SIZE)");
                         pick_err = true; }
            } else if (auto n = parse_int(tok)) {
                if (*n >= 1 && *n <= (int)pmap.size())
                    params.push_back(pmap[*n-1].name);
                else { rerr("parameter #" + tok + " out of range for " + rsp
                            + " (1.." + std::to_string(pmap.size()) + ")");
                       pick_err = true; }
            } else {
                bool known = false;
                for (const auto& p : pmap) if (p.name == tok) { known = true; break; }
                if (!known) { rerr("pick parameter not in " + rsp + ": \""
                                   + tok + "\"");
                              pick_err = true; }
                else params.push_back(tok);
            }
        }
        if (pick_err) continue;
        if (!params.empty()) picks.push_back({ {"params", params} });
        je["picks"] = picks;
        r.source["tiers"][kTierName[t]].push_back(std::move(je));
    }
    return r;
}

CsvResult parse_rate_group_csv_file(const std::string& path,
                                    const mib::Mib& mib) {
    std::ifstream f(path);
    if (!f) {
        CsvResult r;
        r.errors.push_back("cannot open " + path);
        return r;
    }
    std::ostringstream ss; ss << f.rdbuf();
    return parse_rate_group_csv_text(ss.str(), mib);
}

} // namespace xdos::lut
