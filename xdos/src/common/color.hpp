// SPDX-License-Identifier: Apache-2.0
// Tiny ANSI colour helper.  Stays single-header to keep the dependency graph
// boring.  Call `set_use_color(false)` to globally disable (e.g. when the
// `--no-color` flag is passed) — every wrapper then returns an empty string.

#ifndef XDOS_COMMON_COLOR_HPP
#define XDOS_COMMON_COLOR_HPP

#include <string>
#include <string_view>

namespace xdos::color {

// Global toggle.  Default ON; CLI flips it from the command line.
inline bool& enabled() { static bool e = true; return e; }
inline void set_use_color(bool on) { enabled() = on; }

namespace ansi {
inline constexpr std::string_view RESET   = "\033[0m";
inline constexpr std::string_view BOLD    = "\033[1m";
inline constexpr std::string_view DIM     = "\033[2m";

inline constexpr std::string_view RED     = "\033[31m";
inline constexpr std::string_view GREEN   = "\033[32m";
inline constexpr std::string_view YELLOW  = "\033[33m";
inline constexpr std::string_view BLUE    = "\033[34m";
inline constexpr std::string_view MAGENTA = "\033[35m";
inline constexpr std::string_view CYAN    = "\033[36m";
inline constexpr std::string_view WHITE   = "\033[37m";

inline constexpr std::string_view BR_RED     = "\033[91m";
inline constexpr std::string_view BR_GREEN   = "\033[92m";
inline constexpr std::string_view BR_YELLOW  = "\033[93m";
inline constexpr std::string_view BR_CYAN    = "\033[96m";
inline constexpr std::string_view BR_WHITE   = "\033[97m";
} // namespace ansi

inline std::string wrap(std::string_view code, std::string_view text) {
    if (!enabled()) return std::string(text);
    std::string r;
    r.reserve(code.size() + text.size() + ansi::RESET.size());
    r.append(code); r.append(text); r.append(ansi::RESET);
    return r;
}

// Semantic colours used throughout the CLI/OBC.  Yellow=SET, green=GET,
// cyan=ACT, red=error, bold-white=header, dim=hint.
inline std::string set_tag(std::string_view t = "[SET]") { return wrap(ansi::BR_YELLOW, t); }
inline std::string get_tag(std::string_view t = "[GET]") { return wrap(ansi::BR_GREEN,  t); }
inline std::string act_tag(std::string_view t = "[ACT]") { return wrap(ansi::BR_CYAN,   t); }
inline std::string err_tag(std::string_view t = "[ERR]") { return wrap(ansi::BR_RED,    t); }

inline std::string header(std::string_view t) {
    if (!enabled()) return std::string(t);
    return std::string(ansi::BOLD) + std::string(ansi::BR_WHITE)
         + std::string(t) + std::string(ansi::RESET);
}
inline std::string hint(std::string_view t)  { return wrap(ansi::DIM,     t); }
inline std::string ok(std::string_view t)    { return wrap(ansi::GREEN,   t); }
inline std::string warn(std::string_view t)  { return wrap(ansi::YELLOW,  t); }
inline std::string err(std::string_view t)   { return wrap(ansi::BR_RED,  t); }
inline std::string addr(std::string_view t)  { return wrap(ansi::CYAN,    t); }   // for IP/ports

} // namespace xdos::color

#endif
