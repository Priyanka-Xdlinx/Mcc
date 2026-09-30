// SPDX-License-Identifier: Apache-2.0
#include "mib/mib.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

namespace xdos::mib {

using json = nlohmann::json;

// ─── EnumType lookups ────────────────────────────────────────────────────────
const EnumValue* EnumType::by_value(std::int64_t v) const {
    for (auto const& ev : values) if (ev.value == v) return &ev;
    return nullptr;
}
const EnumValue* EnumType::by_name(std::string_view n) const {
    for (auto const& ev : values) if (ev.name == n) return &ev;
    return nullptr;
}

// ─── helpers ────────────────────────────────────────────────────────────────
static std::optional<std::string> opt_string(const json& v) {
    if (v.is_null()) return std::nullopt;
    return v.get<std::string>();
}
static std::optional<int> opt_int(const json& v) {
    if (v.is_null()) return std::nullopt;
    return v.get<int>();
}
static std::optional<double> opt_double(const json& v) {
    if (v.is_null()) return std::nullopt;
    return v.get<double>();
}

static std::string key3(const char* a, const char* b, int c) {
    return std::string(a) + ":" + b + ":" + std::to_string(c);
}
static std::string key4(const char* a, const char* b,
                        std::string_view node, int c) {
    return std::string(a) + ":" + b + ":" + std::string(node) +
           ":" + std::to_string(c);
}

// ─── Mib::load ──────────────────────────────────────────────────────────────
void Mib::load(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open MIB JSON: " + path.string());
    json j;  in >> j;

    SubsystemDef def;
    def.name        = j.at("subsystem").get<std::string>();
    def.protocol    = j.value("protocol", "");
    def.description = j.value("description", "");

    auto sys = subsystem_from_string(def.name);
    if (!sys) throw std::runtime_error("unknown subsystem name: " + def.name);

    // nodes
    for (auto const& jn : j.value("nodes", json::array())) {
        Node n;
        n.name        = jn.at("name").get<std::string>();
        n.address     = opt_int(jn.value("address", json(nullptr)));
        n.description = jn.value("description", "");
        def.nodes.push_back(std::move(n));
    }

    // channels (EPS only — informational map from power sheet)
    for (auto const& jc : j.value("channels", json::array())) {
        ChannelInfo c;
        c.id          = jc.at("id").get<int>();
        c.subsystem   = jc.at("subsystem").get<std::string>();
        c.voltage     = opt_string(jc.value("voltage",      json(nullptr)));
        c.avg_power_w = jc.value("avg_power_w",  json(nullptr)).is_number()
                        ? std::optional<double>(jc["avg_power_w"].get<double>())
                        : std::nullopt;
        c.peak_power_w= jc.value("peak_power_w", json(nullptr)).is_number()
                        ? std::optional<double>(jc["peak_power_w"].get<double>())
                        : std::nullopt;
        c.input_range = opt_string(jc.value("input_range",  json(nullptr)));
        c.owner       = opt_string(jc.value("owner",        json(nullptr)));
        c.description = opt_string(jc.value("description",  json(nullptr)));
        def.channels.push_back(std::move(c));
    }

    // enums (own them via unique_ptr so pointers stay stable)
    std::unordered_map<std::string, const EnumType*> enum_by_name;
    for (auto const& je : j.value("enums", json::array())) {
        auto et = std::make_unique<EnumType>();
        et->name          = je.at("name").get<std::string>();
        et->doc_reference = je.value("doc_reference", "");
        for (auto const& jv : je.value("values", json::array())) {
            EnumValue ev;
            ev.value       = jv.at("value").get<std::int64_t>();
            ev.name        = jv.at("name").get<std::string>();
            ev.description = jv.value("description", "");
            et->values.push_back(std::move(ev));
        }
        enum_by_name[et->name] = et.get();
        def.enums.push_back(std::move(et));
    }

    // messages
    for (auto const& jm : j.value("messages", json::array())) {
        Message m;
        m.subsystem = *sys;
        m.node      = opt_string(jm.value("node", json(nullptr)));
        auto dir    = direction_from_string(jm.at("direction").get<std::string>());
        if (!dir) throw std::runtime_error("bad direction in MIB JSON");
        m.direction = *dir;
        m.msg_id    = jm.at("msg_id").get<int>();
        m.wire_id   = jm.at("wire_id").get<int>();
        m.name      = jm.at("name").get<std::string>();
        m.param_id  = jm.value("param_id", "");
        m.description       = jm.value("description", "");
        m.length_bytes      = opt_int(jm.value("length_bytes", json(nullptr)));
        m.is_variable_length= jm.value("is_variable_length", false);
        m.is_config         = jm.value("is_config", false);
        m.request_via       = opt_string(jm.value("request_via", json(nullptr)));
        m.offset_unit       = jm.value("offset_unit", "bytes");
        m.doc_reference     = jm.value("doc_reference", "");

        for (auto const& jp : jm.value("parameters", json::array())) {
            Parameter p;
            p.seq         = jp.at("seq").get<int>();
            p.offset      = opt_int(jp.value("offset", json(nullptr)));
            p.length      = jp.at("length").get<int>();
            p.name        = jp.at("name").get<std::string>();
            p.data_type   = jp.at("data_type").get<std::string>();
            // Accept both 'enum' (legacy) and 'enum_type' (newer EPS MIB)
            auto en = opt_string(jp.value("enum", json(nullptr)));
            if (!en) en = opt_string(jp.value("enum_type", json(nullptr)));
            if (en) {
                auto it = enum_by_name.find(*en);
                if (it != enum_by_name.end()) p.enum_type = it->second;
            }
            // Byte order is per-field and little-endian unless the MIB says
            // otherwise.  Only the sensor controller's temperature needs this
            // so far; every other subsystem in the fleet is little-endian.
            p.big_endian  = jp.value("endian", std::string("little")) == "big";
            p.unit        = opt_string(jp.value("unit", json(nullptr)));
            p.scale       = opt_double(jp.value("scale", json(nullptr)));
            p.description = jp.value("description", "");
            p.is_array    = jp.value("is_array", false);
            p.array_count = opt_int(jp.value("array_count", json(nullptr)));
            m.parameters.push_back(std::move(p));
        }
        def.messages.push_back(std::move(m));
    }

    subs_[static_cast<int>(*sys)] = std::move(def);
    index(subs_[static_cast<int>(*sys)]);
}

void Mib::load_dir(const std::filesystem::path& dir) {
    if (!std::filesystem::is_directory(dir))
        throw std::runtime_error("MIB dir not found: " + dir.string());
    std::vector<std::filesystem::path> files;
    for (auto const& e : std::filesystem::directory_iterator(dir)) {
        if (e.is_regular_file() && e.path().extension() == ".json")
            files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    for (auto const& f : files) {
        // Skip JSON files that aren't subsystem MIBs (e.g. ccsds_tc.json).
        // Peek at the top-level "subsystem" key without throwing.
        std::ifstream peek(f);
        if (!peek) continue;
        json jp;
        try { peek >> jp; } catch (...) { continue; }
        if (!jp.is_object() || !jp.contains("subsystem")) continue;
        load(f);
    }
}

void Mib::index(SubsystemDef& def) {
    const char* sname = def.name.c_str();
    for (auto const& m : def.messages) {
        const char* dname = to_string(m.direction);
        by_name_[std::string(sname) + ":" + dname + ":" + m.name] = &m;
        by_wire_[key3(sname, dname, m.wire_id)] = &m;
        if (m.node) {
            // Index under the canonical node name
            by_node_wire_[key4(sname, dname, *m.node, m.wire_id)] = &m;
            // ALSO index under every alias (any other node with same address)
            int addr = -1;
            for (auto const& n : def.nodes) if (n.name == *m.node && n.address) {
                addr = *n.address; break;
            }
            if (addr >= 0) {
                for (auto const& n : def.nodes) {
                    if (n.address && *n.address == addr && n.name != *m.node) {
                        by_node_wire_[key4(sname, dname, n.name, m.wire_id)] = &m;
                    }
                }
            }
        } else {
            by_node_wire_[key4(sname, dname, "-", m.wire_id)] = &m;
        }
    }
}

// ─── lookups ────────────────────────────────────────────────────────────────
const Message* Mib::find(Subsystem s, Direction d, std::string_view name) const {
    auto key = std::string(to_string(s)) + ":" + to_string(d) +
               ":" + std::string(name);
    auto it = by_name_.find(key);
    return it == by_name_.end() ? nullptr : it->second;
}

const Message* Mib::by_wire(Subsystem s, Direction d, int wire_id) const {
    auto it = by_wire_.find(key3(to_string(s), to_string(d), wire_id));
    return it == by_wire_.end() ? nullptr : it->second;
}

const Message* Mib::by_node_wire(Subsystem s, Direction d,
                                 std::optional<std::string_view> node,
                                 int wire_id) const {
    std::string n = node ? std::string(*node) : "-";
    auto it = by_node_wire_.find(key4(to_string(s), to_string(d), n, wire_id));
    if (it != by_node_wire_.end()) return it->second;
    return by_wire(s, d, wire_id);
}

int Mib::node_address(Subsystem s, std::optional<std::string_view> node) const {
    if (!node) return 0;
    auto* def = subsystem(s);
    if (!def) return 0;
    for (auto const& n : def->nodes) if (n.name == *node)
        return n.address.value_or(0);
    return 0;
}

std::optional<std::string> Mib::node_by_address(Subsystem s, int addr) const {
    if (addr == 0) return std::nullopt;
    auto* def = subsystem(s);
    if (!def) return std::nullopt;
    for (auto const& n : def->nodes) if (n.address && *n.address == addr)
        return n.name;
    return std::nullopt;
}

const std::vector<ChannelInfo>& Mib::channels(Subsystem s) const {
    static const std::vector<ChannelInfo> empty;
    auto* def = subsystem(s);
    return def ? def->channels : empty;
}

const ChannelInfo* Mib::channel(Subsystem s, int channel_id) const {
    for (auto const& c : channels(s)) if (c.id == channel_id) return &c;
    return nullptr;
}

// Normalise param-ID input: "EP-019", "EP-19", "EP19", "19" → ("EP", 19).
// Returns (prefix, number) on success, or empty prefix on failure.
static std::pair<std::string, int> parse_param_id(const std::string& in) {
    std::string s;
    for (char c : in) {
        if (c == ' ' || c == '\t' || c == '-' || c == '_') continue;
        s.push_back(static_cast<char>(std::toupper(c)));
    }
    if (s.empty()) return {"", 0};
    // Split alphabetic prefix from numeric suffix
    std::size_t i = 0;
    while (i < s.size() && !std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    std::string prefix = s.substr(0, i);
    std::string num    = s.substr(i);
    if (num.empty()) return {"", 0};
    try {
        int n = std::stoi(num);
        return {prefix, n};
    } catch (...) { return {"", 0}; }
}

const Message* Mib::by_param_id(Subsystem s, const std::string& input,
                                Direction dir) const {
    auto [prefix, num] = parse_param_id(input);
    if (num <= 0) return nullptr;

    char idbuf[16];
    std::snprintf(idbuf, sizeof(idbuf), "%s-%03d",
                  prefix.empty() ? "EP" : prefix.c_str(), num);
    std::string target = idbuf;

    auto* def = subsystem(s);
    if (!def) return nullptr;
    const Message* found = nullptr;
    for (auto const& m : def->messages) {
        if (m.direction != dir) continue;
        if (m.param_id == target) {
            if (found) return nullptr;          // ambiguous
            found = &m;
        }
    }
    return found;
}

// ─── listings ───────────────────────────────────────────────────────────────
const std::vector<Message>& Mib::messages(Subsystem s) const {
    static const std::vector<Message> empty;
    auto it = subs_.find(static_cast<int>(s));
    return it == subs_.end() ? empty : it->second.messages;
}

std::vector<const Message*> Mib::messages(Subsystem s, Direction d) const {
    std::vector<const Message*> out;
    for (auto const& m : messages(s))
        if (m.direction == d) out.push_back(&m);
    return out;
}

const SubsystemDef* Mib::subsystem(Subsystem s) const {
    auto it = subs_.find(static_cast<int>(s));
    return it == subs_.end() ? nullptr : &it->second;
}

std::vector<Subsystem> Mib::loaded_subsystems() const {
    std::vector<Subsystem> out;
    for (auto const& kv : subs_) out.push_back(static_cast<Subsystem>(kv.first));
    std::sort(out.begin(), out.end());
    return out;
}

Mib::Counts Mib::counts() const {
    Counts c{};
    for (auto const& kv : subs_) {
        for (auto const& m : kv.second.messages) {
            switch (m.direction) {
                case Direction::TC:    ++c.tc; break;
                case Direction::TM:    ++c.tm; break;
                case Direction::Event: ++c.event; break;
            }
            c.params += m.parameters.size();
        }
    }
    return c;
}

} // namespace xdos::mib
