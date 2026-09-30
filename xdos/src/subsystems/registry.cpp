// SPDX-License-Identifier: Apache-2.0
#include "subsystems/plugin.hpp"

#include <algorithm>
#include <cctype>

namespace xdos::subsystems {

Registry& Registry::instance() {
    static Registry inst;
    return inst;
}

static std::string upper(std::string_view s) {
    std::string out; out.reserve(s.size());
    for (char c : s) out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    return out;
}

void Registry::add(std::unique_ptr<SubsystemPlugin> p) {
    if (!p) return;
    // Prevent double-registration
    for (auto const& existing : plugins_)
        if (upper(existing->name()) == upper(p->name())) return;
    plugins_.push_back(std::move(p));
}

SubsystemPlugin* Registry::find(std::string_view name) const {
    auto want = upper(name);
    for (auto const& p : plugins_)
        if (upper(p->name()) == want) return p.get();
    return nullptr;
}

// Forward-declared factory functions from each plugin's own file.
std::unique_ptr<SubsystemPlugin> make_xlink_plugin();

void register_builtins() {
    auto& r = Registry::instance();
    r.add(make_xlink_plugin());
    // EPS/ADCS/GPS/Payload plugins will be added here as they're migrated
    // from legacy switch-based dispatch.  Until then, they continue to work
    // via the existing code paths.
}

} // namespace xdos::subsystems
