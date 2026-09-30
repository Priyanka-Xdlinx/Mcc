// SPDX-License-Identifier: Apache-2.0
#include "plausible.hpp"
#include <cmath>

namespace xdos::sim {

using json = nlohmann::json;

static double drift(double t, int seq) {
    return std::sin(t * 0.3 + seq * 0.7);
}

static json one_value(const mib::Parameter& p, double t) {
    const auto& nm = p.name;
    auto contains = [&](const char* s){
        return nm.find(s) != std::string::npos;
    };
    auto& dt = p.data_type;
    double d = drift(t, p.seq);

    if (dt == "BOOL")   return false;
    if (dt == "ENUM")   {
        if (p.enum_type && !p.enum_type->values.empty())
            return p.enum_type->values.front().value;
        return 0;
    }
    if (dt == "CHAR" || dt == "STRING") return std::string("XDOS");
    if (dt == "HEX" || dt == "RESERVED") return 0;

    if (dt == "FLOAT" || dt == "DOUBLE") {
        if (contains("lat"))                              return 12.9716 + d * 0.001;
        if (contains("lon"))                              return 77.5946 + d * 0.001;
        if (contains("Volt") || contains("volt"))         return 3.7    + d * 0.05;
        if (contains("Curr") || contains("curr"))         return 0.45   + d * 0.1;
        if (contains("Temp") || contains("temp"))         return 27.0   + d * 2.0;
        if (contains("hgt")  || contains("height") || contains("alt")) return 920.0 + d * 5.0;
        return std::round(d * 10000.0) / 1000.0;
    }
    // integer-ish
    if (contains("count") || contains("num") || contains("#")) return 8 + static_cast<int>(2 * (d + 1));
    if (contains("Unix") || contains("time") || contains("seconds")) return static_cast<long long>(1700000000.0 + t);
    if (contains("year")) return 2026;
    return static_cast<int>((d + 1) * 5);
}

json plausible_values(const mib::Message& tm, double t, int array_records) {
    json out = json::object();
    bool has_arrays = false;
    for (auto const& p : tm.parameters) if (p.is_array) { has_arrays = true; break; }

    for (auto const& p : tm.parameters) {
        if (p.is_array) continue;
        const auto& nm = p.name;
        bool count = (nm.find("num") != std::string::npos
                   || nm.find("count") != std::string::npos
                   || nm.find('#')     != std::string::npos);
        if (has_arrays && count)  out[p.name] = array_records;
        else                       out[p.name] = one_value(p, t);
    }
    return out;
}

} // namespace xdos::sim
