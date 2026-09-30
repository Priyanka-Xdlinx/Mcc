// SPDX-License-Identifier: Apache-2.0
#define _USE_MATH_DEFINES   // unlock M_PI etc. on MSVC/MinGW <cmath>
#include "engine_responder.hpp"
#include "../plausible.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>

namespace xdos::sim {

using nlohmann::json;

namespace {
std::string lower(std::string_view s) {
    std::string r; r.reserve(s.size());
    for (char c : s) r.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(c))));
    return r;
}
bool contains(std::string_view hay, std::string_view needle) {
    return lower(hay).find(needle) != std::string::npos;
}
} // namespace

EngineResponder::EngineResponder(const mib::Mib& mib, Subsystem sub)
    : mib_(mib), sub_(sub) {}

// ─── EPS live-value mapping ────────────────────────────────────────────────
static void fill_eps_live(const EpsEngine& eps, const mib::Message& tm,
                          json& values) {
    auto st = eps.snapshot();

    auto set_if_present = [&](const std::string& k, double v) {
        if (values.contains(k)) values[k] = v;
    };

    const std::string n = lower(tm.name);

    // Battery readings
    if (contains(n, "batt") || contains(n, "voltage") || contains(n, "current")
        || contains(n, "soc")  || contains(n, "temp")) {
        set_if_present("voltage",      st.batt_voltage);
        set_if_present("batt_voltage", st.batt_voltage);
        set_if_present("stack_voltage",st.batt_voltage);
        set_if_present("current",      st.batt_current_a);
        set_if_present("batt_current", st.batt_current_a);
        set_if_present("stack_current",st.batt_current_a);
        set_if_present("soc",          st.batt_soc * 100.0);
        set_if_present("soc_percent",  st.batt_soc * 100.0);
        set_if_present("temp",         st.batt_temp_c[0]);
        set_if_present("temperature",  st.batt_temp_c[0]);
        set_if_present("stack_temp",   st.batt_temp_c[0]);
    }

    // Channel readings (PDU current per channel)
    if (contains(n, "ch_current") || contains(n, "channel_current")
        || contains(n, "ch_curr")) {
        std::lock_guard<std::mutex> g(eps.pdu().m);
        const auto& pdu = eps.pdu();
        for (int i = 0; i < 14; ++i) {
            std::string k1 = "ch" + std::to_string(i+1) + "_current";
            std::string k2 = "ch" + std::to_string(i+1);
            double v = (pdu.channel_mask & (1u << i))
                       ? pdu.per_channel_load_ma[i] / 1000.0 : 0.0;
            if (values.contains(k1)) values[k1] = v;
            if (values.contains(k2)) values[k2] = v;
        }
    }

    // Channel enable mask
    if (contains(n, "ch_en") || contains(n, "channel_enable")
        || contains(n, "ch_switch")) {
        std::lock_guard<std::mutex> g(eps.pdu().m);
        if (values.contains("channel_mask")) values["channel_mask"] = eps.pdu().channel_mask;
        if (values.contains("ch_mask"))      values["ch_mask"]      = eps.pdu().channel_mask;
        if (values.contains("mask"))         values["mask"]         = eps.pdu().channel_mask;
    }

    // PDU channel state (Database messages: Get/Set_PDU_CH_State[_RSP]).
    // The Database models the masks as two byte parameters; report the
    // EFFECTIVE state — kill switch OFF means every output reads 0.
    if (contains(n, "ch_state")) {
        const std::uint16_t eff = eps.effective_mask();
        set_if_present("CH1 to CH8 Enable/Disable",  eff & 0xFF);
        set_if_present("CH9 to CH16 Enable/Disable", (eff >> 8) & 0xFF);
        // Set_PDU_CH_State_RSP carries a Status byte: reject when bus is dead.
        set_if_present("Status", eps.kill() ? 1 : 0);
    }

    // EPS mode — generic keys plus the Database's exact parameter names
    // (Get_PDU_Alert_Flags_RSP has "EPS Mode").
    if (contains(n, "mode") || contains(n, "alert") || contains(n, "flag")) {
        std::lock_guard<std::mutex> g(eps.pdu().m);
        if (values.contains("mode"))     values["mode"]     = eps.pdu().current_mode;
        if (values.contains("eps_mode")) values["eps_mode"] = eps.pdu().current_mode;
        if (values.contains("EPS Mode")) values["EPS Mode"] = eps.pdu().current_mode;
    }

    // MPPT readings
    if (contains(n, "mppt")) {
        for (int i = 0; i < 7; ++i) {
            std::string kv = "ch" + std::to_string(i+1) + "_voltage";
            std::string ki = "ch" + std::to_string(i+1) + "_current";
            if (values.contains(kv)) values[kv] = st.mppt_ch_voltage[i];
            if (values.contains(ki)) values[ki] = st.mppt_ch_current[i];
        }
        set_if_present("solar_power",   st.solar_power_w);
        set_if_present("mppt_current",  st.mppt_current_a);
    }
}

// ─── GPS live-value mapping ────────────────────────────────────────────────
static void fill_gps_live(const GpsEngine& gps, const mib::Message& tm,
                          json& values) {
    auto st = gps.snapshot();
    const std::string n = lower(tm.name);

    auto set_if_present = [&](const std::string& k, auto v) {
        if (values.contains(k)) values[k] = v;
    };

    if (n == "bestpos") {
        set_if_present("sol_status", (int)st.sol_status);
        set_if_present("pos_type",   (int)st.pos_type);
        set_if_present("lat",        st.lat_deg);
        set_if_present("lon",        st.lon_deg);
        set_if_present("hgt",        st.alt_m);
        set_if_present("n_sats",     (int)st.n_sats);
    } else if (n == "bestvel") {
        set_if_present("sol_status", (int)st.sol_status);
        set_if_present("hor_spd",    st.speed_mps);
        set_if_present("trk_gnd",    st.course_deg);
        set_if_present("vert_spd",   0.0);
    } else if (n == "time") {
        set_if_present("week",          (int)st.week);
        set_if_present("seconds",       st.tow_sec);
        set_if_present("tow",           st.tow_sec);
    } else if (n == "psrdop") {
        // dop values mildly evolve with satellite geometry; first-order static
        set_if_present("gdop", 1.6);
        set_if_present("pdop", 1.4);
        set_if_present("hdop", 0.9);
        set_if_present("vdop", 1.1);
        set_if_present("tdop", 0.8);
    }
}

// ─── ADCS live-value mapping ───────────────────────────────────────────────
static void fill_adcs_live(const AdcsEngine& adcs, const mib::Message& tm,
                           json& values) {
    auto st = adcs.snapshot();
    const std::string n = lower(tm.name);

    auto set_if_present = [&](const std::string& k, auto v) {
        if (values.contains(k)) values[k] = v;
    };

    if (contains(n, "estimated attitude") || contains(n, "euler")) {
        set_if_present("roll",     st.roll_deg);
        set_if_present("pitch",    st.pitch_deg);
        set_if_present("yaw",      st.yaw_deg);
    }
    if (contains(n, "rate") || contains(n, "angular rate")) {
        set_if_present("rate_x",  st.omega[0] * 180.0 / M_PI);
        set_if_present("rate_y",  st.omega[1] * 180.0 / M_PI);
        set_if_present("rate_z",  st.omega[2] * 180.0 / M_PI);
        set_if_present("omega_x", st.omega[0] * 180.0 / M_PI);
        set_if_present("omega_y", st.omega[1] * 180.0 / M_PI);
        set_if_present("omega_z", st.omega[2] * 180.0 / M_PI);
    }
    if (contains(n, "quaternion")) {
        set_if_present("q0", st.q[0]); set_if_present("qw", st.q[0]);
        set_if_present("q1", st.q[1]); set_if_present("qx", st.q[1]);
        set_if_present("q2", st.q[2]); set_if_present("qy", st.q[2]);
        set_if_present("q3", st.q[3]); set_if_present("qz", st.q[3]);
    }
    if (contains(n, "current unix") || contains(n, "unix time")) {
        set_if_present("unix_time",   (int64_t)st.unix_time_s);
        set_if_present("time",        (int64_t)st.unix_time_s);
        set_if_present("current_time",(int64_t)st.unix_time_s);
    }
    if (contains(n, "sun") && contains(n, "vector")) {
        set_if_present("sun_x", st.sun_vec_body[0]);
        set_if_present("sun_y", st.sun_vec_body[1]);
        set_if_present("sun_z", st.sun_vec_body[2]);
    }
    if (contains(n, "control") && contains(n, "mode")) {
        set_if_present("mode",         (int)st.control_mode);
        set_if_present("control_mode", (int)st.control_mode);
    }
    if (contains(n, "run") && contains(n, "mode")) {
        set_if_present("mode",     (int)st.run_mode);
        set_if_present("run_mode", (int)st.run_mode);
    }
}

json EngineResponder::build_tm_values(const mib::Message& tm) const {
    // Start with the synthetic plausible values to fill every field with
    // *something* sensible.  Then overlay live engine values where we can.
    json v = plausible_values(tm, /*now_seconds*/ 0.0);

    if (sub_ == Subsystem::EPS  && eps_ ) fill_eps_live (*eps_,  tm, v);
    if (sub_ == Subsystem::GPS  && gps_ ) fill_gps_live (*gps_,  tm, v);
    if (sub_ == Subsystem::ADCS && adcs_) fill_adcs_live(*adcs_, tm, v);
    return v;
}

// ─── SET-side mutation ─────────────────────────────────────────────────────
void EngineResponder::apply_tc(const mib::Message& tc, const json& values) const {
    if (tc.direction != Direction::TC) return;

    if (sub_ == Subsystem::EPS && eps_) {
        const std::string n = lower(tc.name);

        // Set_PDU_CH_State — the Database's real channel-switch TC.  Masks
        // arrive as two byte parameters.  Rejected while the kill switch is
        // OFF (bus dead ⇒ the PDU can't actuate anything).
        if (contains(n, "ch_state")) {
            if (!eps_->kill()) {
                std::cout << "      [PDU] SET rejected — kill switch is OFF\n";
                return;
            }
            std::uint16_t old_m, m;
            {
                std::lock_guard<std::mutex> g(eps_->pdu().m);
                old_m = eps_->pdu().channel_mask;
            }
            m = old_m;
            auto grab = [&](const char* key, int shift) {
                if (values.contains(key) && values[key].is_number_integer()) {
                    m &= static_cast<std::uint16_t>(~(0xFFu << shift));
                    m |= static_cast<std::uint16_t>(
                        (values[key].get<unsigned>() & 0xFF) << shift);
                }
            };
            grab("CH1 to CH8 Enable/Disable",  0);
            grab("CH9 to CH16 Enable/Disable", 8);

            const auto& chans = mib_.channels(Subsystem::EPS);
            if (!chans.empty()) {
                std::string turned_on, turned_off;
                for (int i = 0; i < 16; ++i) {
                    bool was = (old_m >> i) & 1, now = (m >> i) & 1;
                    if (was == now) continue;
                    auto* c = mib_.channel(Subsystem::EPS, i + 1);
                    std::string label = "CH" + std::to_string(i + 1)
                        + (c ? "(" + c->subsystem + ")" : "");
                    auto& dst = now ? turned_on : turned_off;
                    dst += (dst.empty() ? "" : " ") + label;
                }
                if (!turned_on.empty())  std::cout << "      [PDU] ON :  " << turned_on  << "\n";
                if (!turned_off.empty()) std::cout << "      [PDU] OFF:  " << turned_off << "\n";
            }
            std::lock_guard<std::mutex> g(eps_->pdu().m);
            eps_->pdu().channel_mask = m & 0x3FFF;
        }

        if (contains(n, "ch_en") || contains(n, "ch_switch")) {
            // Set channel mask
            uint16_t m = 0;
            if (values.contains("channel_mask")) m = values["channel_mask"];
            else if (values.contains("ch_mask")) m = values["ch_mask"];
            else if (values.contains("mask"))    m = values["mask"];

            // Diff against current mask and print human-readable changes.
            const auto& chans = mib_.channels(Subsystem::EPS);
            uint16_t old_m;
            {
                std::lock_guard<std::mutex> g(eps_->pdu().m);
                old_m = eps_->pdu().channel_mask;
            }
            if (!chans.empty()) {
                std::string turned_on, turned_off;
                for (int i = 0; i < 16; ++i) {
                    bool was = (old_m >> i) & 1;
                    bool now = (m     >> i) & 1;
                    if (was == now) continue;
                    int ch_id = i + 1;
                    auto* c = mib_.channel(Subsystem::EPS, ch_id);
                    std::string label = c
                        ? ("CH" + std::to_string(ch_id) + "(" + c->subsystem + ")")
                        : ("CH" + std::to_string(ch_id));
                    (now ? turned_on : turned_off) += (turned_on.empty() && now ? "" :
                                                       turned_off.empty() && !now ? "" : " ") + label;
                }
                if (!turned_on.empty())  std::cout << "      [PDU] ON :  " << turned_on  << "\n";
                if (!turned_off.empty()) std::cout << "      [PDU] OFF:  " << turned_off << "\n";
            }

            std::lock_guard<std::mutex> g(eps_->pdu().m);
            eps_->pdu().channel_mask = m;
        }
        if (contains(n, "eps_mode") || (contains(n, "set") && contains(n, "mode"))) {
            uint8_t mode = 0;
            if (values.contains("mode"))     mode = values["mode"];
            else if (values.contains("eps_mode")) mode = values["eps_mode"];
            std::lock_guard<std::mutex> g(eps_->pdu().m);
            eps_->pdu().current_mode = mode;
        }
    }

    if (sub_ == Subsystem::ADCS && adcs_) {
        const std::string n = lower(tc.name);
        if (contains(n, "run mode")) {
            if (values.contains("mode"))     adcs_->set_run_mode(values["mode"]);
            else if (values.contains("run_mode")) adcs_->set_run_mode(values["run_mode"]);
        }
        if (contains(n, "control mode")) {
            if (values.contains("mode")) adcs_->set_control_mode(values["mode"]);
        }
        if (contains(n, "unix time")) {
            if (values.contains("time"))      adcs_->set_unix_time(values["time"]);
            else if (values.contains("unix_time")) adcs_->set_unix_time(values["unix_time"]);
        }
        if (contains(n, "reference") && contains(n, "quaternion")) {
            Quat q { 1, 0, 0, 0 };
            if (values.contains("q0")) q[0] = values["q0"];
            if (values.contains("q1")) q[1] = values["q1"];
            if (values.contains("q2")) q[2] = values["q2"];
            if (values.contains("q3")) q[3] = values["q3"];
            adcs_->set_reference_quaternion(q);
            adcs_->set_control_enabled(true);
        }
        if (tc.name == "Reset") {
            // no-op for now; engine has no reset path
        }
    }
}

} // namespace xdos::sim
