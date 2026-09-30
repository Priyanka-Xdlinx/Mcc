// SPDX-License-Identifier: Apache-2.0
#include "lut/binary_lut_builder.hpp"

#include "codec/codec.hpp"
#include "framing/adcs_framing.hpp"
#include "framing/eps_framing.hpp"
#include "framing/gps_framing.hpp"
#include "framing/uhf_framing.hpp"
#include "subsystems/xlink_codec.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

using nlohmann::json;

namespace xdos::lut {

// ── little-endian emit helpers ───────────────────────────────────────────────
static void put_u16(Bytes& b, uint16_t v) { b.push_back(v & 0xFF); b.push_back(v >> 8); }
static void put_u32(Bytes& b, uint32_t v) { for (int i=0;i<4;++i) b.push_back((v>>(8*i))&0xFF); }

uint16_t BinaryLutBuilder::crc16_ccitt(const Bytes& d) {
    return lut::crc16_ccitt(d.data(), d.size());
}

// ── source config ────────────────────────────────────────────────────────────
bool BinaryLutBuilder::load_source(const std::string& path, json& out,
                                   std::string* err) {
    std::ifstream f(path);
    if (!f) { if (err) *err = "cannot open " + path; return false; }
    try { f >> out; } catch (const std::exception& e) {
        if (err) *err = std::string("bad JSON: ") + e.what(); return false;
    }
    if (!out.contains("tiers") || !out["tiers"].is_object()) {
        if (err) *err = "source config has no \"tiers\" object"; return false;
    }
    return true;
}

static bool parse_cmd(const json& j, SourceCmd& c, std::string& err) {
    try {
        c.id       = j.value("id", "");
        c.tc_name  = j.at("tc").get<std::string>();
        c.rsp_name = j.at("rsp").get<std::string>();
        c.gps_log_id = j.value("gps_log_id", -1);
        std::string sub = j.at("subsystem").get<std::string>();
        // Use the canonical name table rather than a private if-chain: the GS
        // twin stores the DISPLAY name (to_string gives "XLINK-S"), the CSV
        // writes "XLINK", and the composer's resume-for-edit path feeds the
        // twin's spelling straight back in here.  A local chain that knew only
        // "XLINK" rejected every XLINK entry on resume.
        auto parsed = subsystem_from_string(sub);
        if (!parsed) { err = "unknown subsystem: " + sub; return false; }
        switch (*parsed) {
            case Subsystem::EPS:  case Subsystem::GPS: case Subsystem::XLINK:
            case Subsystem::ADCS: case Subsystem::UHF:
                c.sub = *parsed; break;
            default:
                // Known name, but no rate-group encoder for it.
                err = "subsystem cannot be polled by the rate group: " + sub;
                return false;
        }
        c.picks.clear();
        for (const auto& p : j.value("picks", json::array())) {
            PickSpec ps;
            for (const auto& n : p.value("params", json::array()))
                ps.params.push_back(n.get<std::string>());
            if (p.contains("bytes") && p["bytes"].is_array()
                && p["bytes"].size() == 2)
                ps.bytes = { p["bytes"][0].get<int>(), p["bytes"][1].get<int>() };
            c.picks.push_back(std::move(ps));
        }
        return true;
    } catch (const std::exception& e) { err = e.what(); return false; }
}

// ── Database lookups ─────────────────────────────────────────────────────────
int BinaryLutBuilder::raw_response_len(Subsystem sub,
                                       const std::string& rsp) const {
    const mib::Message* m = mib_.find(sub, Direction::TM, rsp);
    if (!m) return -1;
    if (m->length_bytes) return *m->length_bytes;
    int end = 0; const bool bits = (m->offset_unit == "bits");
    for (const auto& p : m->parameters) {
        if (!p.offset) continue;
        int e = bits ? (*p.offset + p.length + 7) / 8 : *p.offset + p.length;
        end = std::max(end, e);
    }
    return end;
}

std::vector<BinaryLutBuilder::ParamByte>
BinaryLutBuilder::response_param_map(Subsystem sub,
                                     const std::string& rsp) const {
    std::vector<ParamByte> out;
    const mib::Message* m = mib_.find(sub, Direction::TM, rsp);
    if (!m) return out;
    const bool bits = (m->offset_unit == "bits");
    for (const auto& p : m->parameters) {
        if (!p.offset) continue;
        ParamByte pb;
        if (bits) {
            pb.off  = *p.offset / 8;
            pb.size = (*p.offset + p.length + 7) / 8 - pb.off;
        } else {
            pb.off  = *p.offset;
            pb.size = p.length;
        }
        pb.name = p.name;
        pb.type = p.data_type;
        pb.unit = p.unit.value_or("");
        out.push_back(std::move(pb));
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const ParamByte& a, const ParamByte& b){ return a.off < b.off; });
    return out;
}

// ── native TC encoding (mirrors tools/gen_rate_group_lut.cpp) ────────────────
Bytes BinaryLutBuilder::encode_tc(const SourceCmd& c, std::string& err) const {
    if (c.sub == Subsystem::EPS) {
        const mib::Message* tc = mib_.find(Subsystem::EPS, Direction::TC, c.tc_name);
        if (!tc) { err = "EPS TC not found: " + c.tc_name; return {}; }
        int node = 1;                                   // PDU1
        if (tc->node == "BMS1")  node = 9;
        else if (tc->node == "MPPT1") node = 12;
        Bytes payload = codec::encode(*tc, json::object());
        return framing::eps::encode(static_cast<Byte>(node),
                                    static_cast<Byte>(tc->wire_id), payload);
    }
    if (c.sub == Subsystem::GPS) {
        const mib::Message* logtc = mib_.find(Subsystem::GPS, Direction::TC, "LOG");
        if (!logtc) { err = "GPS LOG TC not found"; return {}; }
        json wrap = { {"port", framing::gps::kLogPort},
                      {"message_id", c.gps_log_id},
                      {"message_type",0}, {"reserved",0}, {"trigger",4},
                      {"period",0.0}, {"offset",0.0}, {"hold",0} };
        Bytes payload = codec::encode(*logtc, wrap);
        return framing::gps::encode(static_cast<std::uint16_t>(logtc->wire_id),
                                    payload);
    }
    if (c.sub == Subsystem::XLINK) {
        const mib::Message* tc = mib_.find(Subsystem::XLINK, Direction::TC, c.tc_name);
        if (!tc) { err = "XLINK TC not found: " + c.tc_name; return {}; }
        return xlink::codec::encode_frame(xlink_key_, tc->msg_id,
                                          xlink::codec::FLAG_GET, *tc,
                                          json::object());
    }
    if (c.sub == Subsystem::ADCS) {
        // ADCS rate-group poll = plain TM-request: a native frame carrying
        // only the TM id, empty body (1F 7F <id> 1F FF, escapes as needed).
        const mib::Message* tm = mib_.find(Subsystem::ADCS, Direction::TM,
                                           c.rsp_name);
        if (!tm) { err = "ADCS TM not found: " + c.rsp_name; return {}; }
        return framing::adcs::encode(static_cast<Byte>(tm->msg_id), {});
    }
    if (c.sub == Subsystem::UHF) {
        // UHF GET commands carry no payload; the opcode is the wire id.
        const mib::Message* tc = mib_.find(Subsystem::UHF, Direction::TC,
                                           c.tc_name);
        if (!tc) { err = "UHF TC not found: " + c.tc_name; return {}; }
        Bytes payload = codec::encode(*tc, json::object());
        return framing::uhf::encode(static_cast<Byte>(tc->wire_id), payload);
    }
    err = "unsupported subsystem for rate group";
    return {};
}

// ── pick resolution: names/bytes -> merged byte ranges ──────────────────────
bool BinaryLutBuilder::resolve_picks(const SourceCmd& c, uint16_t tm_len,
                                     std::vector<BuiltPick>& out,
                                     std::vector<std::string>& errors) const {
    struct Range { int a, b; std::vector<std::string> params; };
    std::vector<Range> ranges;
    const std::size_t err0 = errors.size();   // errors is a shared accumulator
    auto pmap = response_param_map(c.sub, c.rsp_name);

    auto add_param = [&](const std::string& name) -> bool {
        for (const auto& p : pmap)
            if (p.name == name) { ranges.push_back({p.off, p.off + p.size, {name}}); return true; }
        errors.push_back(c.id + ": pick parameter not in " + c.rsp_name
                         + ": \"" + name + "\"");
        return false;
    };

    if (c.picks.empty()) {
        // default: one pick covering the whole raw payload
        BuiltPick bp; bp.src = 0; bp.size = tm_len; bp.params = {"<full payload>"};
        out.push_back(std::move(bp));
        return true;
    }
    bool ok = true;
    for (const auto& ps : c.picks) {
        for (const auto& n : ps.params) ok &= add_param(n);
        if (ps.bytes) {
            int off = ps.bytes->first, sz = ps.bytes->second;
            if (off < 0 || sz <= 0 || off + sz > tm_len) {
                errors.push_back(c.id + ": bytes pick [" + std::to_string(off)
                                 + "," + std::to_string(sz) + ") outside 0.."
                                 + std::to_string(tm_len));
                ok = false;
            } else ranges.push_back({off, off + sz, {"<bytes>"}});
        }
    }
    if (!ok) return false;

    // merge overlapping / adjacent ranges into the minimal pick set
    std::sort(ranges.begin(), ranges.end(),
              [](const Range& x, const Range& y){ return x.a < y.a; });
    for (const auto& r : ranges) {
        if (!out.empty() && r.a <= out.back().src + out.back().size) {
            auto& last = out.back();
            int nb = std::max<int>(last.src + last.size, r.b);
            last.size = static_cast<uint16_t>(nb - last.src);
            last.params.insert(last.params.end(), r.params.begin(), r.params.end());
        } else {
            BuiltPick bp;
            bp.src  = static_cast<uint16_t>(r.a);
            bp.size = static_cast<uint16_t>(r.b - r.a);
            bp.params = r.params;
            out.push_back(std::move(bp));
        }
    }
    for (const auto& p : out)
        if (p.src + p.size > tm_len)
            errors.push_back(c.id + ": pick [" + std::to_string(p.src) + ","
                             + std::to_string(p.size) + ") exceeds raw payload "
                             + std::to_string(tm_len) + " B");
    return errors.size() == err0;
}

// ── the build pipeline ───────────────────────────────────────────────────────
BuildResult BinaryLutBuilder::build(const json& source) const {
    BuildResult r;
    for (int t = 0; t < 4; ++t) r.segs[t].budget = kBlockBudget[t];

    for (int t = 0; t < 4; ++t) {
        const auto& tier_arr = source["tiers"].value(kTierName[t], json::array());
        int cursor = 0;                              // block-offset packer
        for (const auto& jc : tier_arr) {
            SourceCmd c; std::string perr;
            if (!parse_cmd(jc, c, perr)) {
                r.errors.push_back(std::string(kTierName[t]) + ": bad entry: " + perr);
                continue;
            }
            BuiltEntry e; e.src = c;
            // APID from the CCSDS map lives in mib/ccsds_tc.json; keep the
            // builder self-contained with the hardware constants (asserted
            // against the map by the unit tests).
            switch (c.sub) {
                case Subsystem::ADCS:  e.apid = 1; break;
                case Subsystem::UHF:   e.apid = 2; break;
                case Subsystem::EPS:   e.apid = 3; break;
                case Subsystem::GPS:   e.apid = 4; break;
                case Subsystem::XLINK: e.apid = 6; break;
                default:               e.apid = 5; break;   // Payload
            }
            std::string eerr;
            e.tc_bytes = encode_tc(c, eerr);
            if (e.tc_bytes.empty()) {
                r.errors.push_back(c.id + ": " + eerr);
                continue;
            }
            int len = raw_response_len(c.sub, c.rsp_name);
            if (len <= 0) {
                r.errors.push_back(c.id + ": response TM not found: " + c.rsp_name);
                continue;
            }
            e.tm_raw_len = static_cast<uint16_t>(len);
            if (!resolve_picks(c, e.tm_raw_len, e.picks, r.errors)) continue;
            for (auto& p : e.picks) {                // sequential block packing
                p.block = static_cast<uint16_t>(cursor);
                cursor += p.size;
            }
            r.segs[t].entries.push_back(std::move(e));
        }
        r.segs[t].used = cursor;
        if (cursor > r.segs[t].budget)
            r.errors.push_back(std::string(kTierName[t]) + " block overflow: "
                               + std::to_string(cursor) + " B picked > budget "
                               + std::to_string(r.segs[t].budget) + " B — last entry: "
                               + (r.segs[t].entries.empty() ? "?"
                                  : r.segs[t].entries.back().src.id));
    }
    if (!r.ok()) return r;

    // ── emit the binary image ────────────────────────────────────────────────
    std::array<Bytes,4> segbytes;
    for (int t = 0; t < 4; ++t) {
        Bytes& b = segbytes[t];
        for (const auto& e : r.segs[t].entries) {
            put_u16(b, kEntrySync);
            b.push_back(static_cast<Byte>(e.apid));
            b.push_back(0);                                    // flags
            put_u16(b, static_cast<uint16_t>(e.tc_bytes.size()));
            b.insert(b.end(), e.tc_bytes.begin(), e.tc_bytes.end());
            put_u16(b, e.tm_raw_len);
            b.push_back(static_cast<Byte>(e.picks.size()));
            for (const auto& p : e.picks) {
                put_u16(b, p.src); put_u16(b, p.size); put_u16(b, p.block);
            }
        }
    }
    Bytes& f = r.file;
    f.reserve(kHeaderSize + segbytes[0].size() + segbytes[1].size()
              + segbytes[2].size() + segbytes[3].size());
    f.insert(f.end(), {'X','L','U','T'});
    f.push_back(kFormatVersion);
    f.push_back(0);                                            // flags
    put_u16(f, kSubframeSize);
    for (int t = 0; t < 4; ++t) put_u16(f, static_cast<uint16_t>(kSegBytes[t]));
    f.push_back(kReservedBytes);
    f.push_back(0);                                            // pad
    put_u16(f, kTsfMs);                                        // t_sf_ms (T_sf)
    for (int t = 0; t < 4; ++t)
        put_u16(f, static_cast<uint16_t>(r.segs[t].entries.size()));
    uint32_t off = kHeaderSize;
    for (int t = 0; t < 4; ++t) { put_u32(f, off); off += segbytes[t].size(); }
    put_u32(f, off);                                           // file_length
    put_u16(f, 0);                                             // crc placeholder
    for (int t = 0; t < 4; ++t)
        f.insert(f.end(), segbytes[t].begin(), segbytes[t].end());
    r.crc = crc16_ccitt(f);                                    // crc field is 0
    f[48] = r.crc & 0xFF; f[49] = r.crc >> 8;

    // ── GS twin ──────────────────────────────────────────────────────────────
    json twin;
    twin["_comment"] = "GS twin of rate_group_lut.bin — ids/names/picks for "
                       "decode. Entry position within a tier is the OBC link.";
    twin["format_version"] = kFormatVersion;
    twin["subframe_size"]  = kSubframeSize;
    twin["crc16"]          = r.crc;
    json jt = json::object();
    for (int t = 0; t < 4; ++t) {
        json arr = json::array();
        for (const auto& e : r.segs[t].entries) {
            json je = { {"id", e.src.id}, {"subsystem", to_string(e.src.sub)},
                        {"tc", e.src.tc_name}, {"rsp", e.src.rsp_name},
                        {"apid", e.apid}, {"tm_raw_len", e.tm_raw_len} };
            if (e.src.gps_log_id >= 0) je["gps_log_id"] = e.src.gps_log_id;
            json jp = json::array();
            for (const auto& p : e.picks)
                jp.push_back({ {"src", p.src}, {"size", p.size},
                               {"block", p.block}, {"params", p.params} });
            je["picks"] = jp;
            arr.push_back(std::move(je));
        }
        jt[kTierName[t]] = std::move(arr);
    }
    twin["tiers"] = std::move(jt);
    r.gs_twin = std::move(twin);
    return r;
}

// Make sure the containing directory exists before opening a file for write —
// otherwise ofstream just fails and the operator sees a bare "cannot write".
static bool ensure_parent_dir(const std::string& path, std::string* err) {
    std::error_code ec;
    auto dir = std::filesystem::path(path).parent_path();
    if (dir.empty() || std::filesystem::is_directory(dir, ec)) return true;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        if (err) *err = "cannot create " + dir.string() + ": " + ec.message();
        return false;
    }
    return true;
}

bool BinaryLutBuilder::write(const BuildResult& r, const std::string& bin_path,
                             const std::string& gs_json_path, std::string* err) {
    if (!r.ok()) { if (err) *err = "build has errors"; return false; }
    {
        if (!ensure_parent_dir(bin_path, err)) return false;
        std::ofstream f(bin_path, std::ios::binary);
        if (!f) {
            if (err) *err = "cannot write " + bin_path + ": " + std::strerror(errno);
            return false;
        }
        f.write(reinterpret_cast<const char*>(r.file.data()),
                static_cast<std::streamsize>(r.file.size()));
    }
    {
        if (!ensure_parent_dir(gs_json_path, err)) return false;
        std::ofstream f(gs_json_path);
        if (!f) {
            if (err) *err = "cannot write " + gs_json_path + ": " + std::strerror(errno);
            return false;
        }
        f << r.gs_twin.dump(1) << "\n";
    }
    return true;
}

} // namespace xdos::lut
