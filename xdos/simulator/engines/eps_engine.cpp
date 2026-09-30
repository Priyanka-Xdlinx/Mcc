// SPDX-License-Identifier: Apache-2.0
#define _USE_MATH_DEFINES   // unlock M_PI etc. on MSVC/MinGW <cmath>
#include "eps_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>

namespace xdos::sim {

float lifepo4_ocv(float soc) {
    // Per-cell open-circuit voltage curve (flat middle, shoulders at ends).
    soc = std::clamp(soc, 0.0f, 1.0f);
    if (soc >= 0.95f) return 3.45f + (soc - 0.95f) * (3.55f - 3.45f) / 0.05f;
    if (soc >= 0.20f) return 3.25f + (soc - 0.20f) * (3.45f - 3.25f) / 0.75f;
    if (soc >= 0.05f) return 3.00f + (soc - 0.05f) * (3.25f - 3.00f) / 0.15f;
    return 2.50f + soc * (3.00f - 2.50f) / 0.05f;
}

float liion_ocv(float soc) {
    // Per-cell OCV for a typical Li-ion (NMC) cell: 3.0 V empty → 4.2 V full.
    // Piecewise-linear over the familiar knee points.
    soc = std::clamp(soc, 0.0f, 1.0f);
    if (soc >= 0.90f) return 4.08f + (soc - 0.90f) * (4.20f - 4.08f) / 0.10f;
    if (soc >= 0.70f) return 3.90f + (soc - 0.70f) * (4.08f - 3.90f) / 0.20f;
    if (soc >= 0.50f) return 3.77f + (soc - 0.50f) * (3.90f - 3.77f) / 0.20f;
    if (soc >= 0.30f) return 3.68f + (soc - 0.30f) * (3.77f - 3.68f) / 0.20f;
    if (soc >= 0.10f) return 3.55f + (soc - 0.10f) * (3.68f - 3.55f) / 0.20f;
    return 3.00f + soc * (3.55f - 3.00f) / 0.10f;
}

static inline float pack_ocv(const EpsEngineConfig& c, float soc) {
    return (c.chemistry == 1 ? liion_ocv(soc) : lifepo4_ocv(soc))
           * c.batt_series_cells;
}

EpsEngine::EpsEngine(EpsEngineConfig cfg) : cfg_(cfg) {
    st_.batt_voltage = pack_ocv(cfg_, st_.batt_soc);
    st_.eps_mode     = 3;   // Normal until first flight-model evaluation
}

EpsEngine::~EpsEngine() { stop(); }

void EpsEngine::start() {
    if (running_.exchange(true)) return;
    th_ = std::thread(&EpsEngine::run, this);
}

void EpsEngine::stop() {
    if (!running_.exchange(false)) return;
    if (th_.joinable()) th_.join();
}

EpsEngineState EpsEngine::snapshot() const {
    std::lock_guard<std::mutex> g(state_m_);
    return st_;
}

void EpsEngine::run() {
    using namespace std::chrono;
    const auto tick_dur = milliseconds(cfg_.tick_real_ms);
    const float dt_sim = (cfg_.tick_real_ms / 1000.0f) * cfg_.time_scale;

    auto next = steady_clock::now() + tick_dur;
    while (running_.load()) {
        tick(dt_sim);
        std::this_thread::sleep_until(next);
        next += tick_dur;
    }
}

void EpsEngine::tick(float dt_sim_sec) {
    // Snapshot PDU for this tick.
    uint16_t mask;
    std::array<uint16_t,14> load_ma;
    {
        std::lock_guard<std::mutex> g(pdu_.m);
        mask = pdu_.channel_mask;
        load_ma = pdu_.per_channel_load_ma;
    }

    std::lock_guard<std::mutex> g(state_m_);

    // --- orbit propagation ---
    st_.sim_elapsed_sec += dt_sim_sec;
    st_.orbit_angle_deg = std::fmod(st_.sim_elapsed_sec / cfg_.orbit_period_sec
                                    * 360.0f, 360.0f);

    // Sun arc is the first sun_fraction × 360° of the orbit; rest is eclipse.
    const float sun_arc = cfg_.sun_fraction * 360.0f;
    OrbitPhase prev = st_.phase;
    st_.phase = (st_.orbit_angle_deg < sun_arc)
                ? OrbitPhase::Sun : OrbitPhase::Eclipse;
    if (prev == OrbitPhase::Sun && st_.phase == OrbitPhase::Eclipse)
        ++st_.eclipse_count;

    // Sim-seconds until the next phase boundary (for the GS status bar).
    {
        const float boundary = (st_.phase == OrbitPhase::Sun) ? sun_arc : 360.0f;
        st_.phase_remaining_s = (boundary - st_.orbit_angle_deg) / 360.0f
                                * cfg_.orbit_period_sec;
    }

    // --- solar input ---
    if (st_.phase == OrbitPhase::Sun) {
        // Sun-angle pseudo: peak at sun_arc/2, falls to zero at the boundaries.
        const float t = st_.orbit_angle_deg / sun_arc;       // 0..1 across the arc
        const float bell = std::sin(static_cast<float>(M_PI) * t); // 0→1→0
        st_.solar_power_w = cfg_.panel_power_w * bell * bell;
    } else {
        st_.solar_power_w = 0.0f;
    }

    // --- load current (kill switch gates every output) ---
    const bool bus_live = kill_.load();
    float load_a = 0.0f;
    for (int ch = 0; ch < 14; ++ch) {
        if (bus_live && (mask & (1u << ch))) load_a += load_ma[ch] / 1000.0f;
    }
    st_.load_current_a = load_a;

    // --- power balance ---
    st_.mppt_current_a = st_.solar_power_w * cfg_.mppt_eff / st_.batt_voltage;
    st_.net_current_a  = st_.mppt_current_a - st_.load_current_a;
    st_.batt_current_a = st_.net_current_a;

    // --- SOC integration ---
    float d_ah = st_.net_current_a * (dt_sim_sec / 3600.0f);
    st_.batt_soc = std::clamp(st_.batt_soc + d_ah / cfg_.batt_capacity_ah,
                              0.0f, 1.0f);
    st_.batt_voltage = pack_ocv(cfg_, st_.batt_soc);

    // --- flight power model: EPS mode from voltage + load shedding ---
    if (cfg_.flight_power) update_mode_and_shed(st_.batt_voltage);

    // --- energy bookkeeping (Wh) ---
    float dt_h = dt_sim_sec / 3600.0f;
    if (st_.net_current_a > 0)
        st_.energy_in_wh  += st_.net_current_a * st_.batt_voltage * dt_h;
    else
        st_.energy_out_wh += -st_.net_current_a * st_.batt_voltage * dt_h;

    // --- MPPT per-channel split (3 channels active, even spread) ---
    const float per_ch_w = st_.solar_power_w / 3.0f;
    for (int i = 0; i < 7; ++i) {
        if (i < 3) {
            st_.mppt_ch_voltage[i] = (st_.solar_power_w > 0) ? 4.5f : 0.0f;
            st_.mppt_ch_current[i] = per_ch_w / 4.5f;
        } else {
            st_.mppt_ch_voltage[i] = 0.0f;
            st_.mppt_ch_current[i] = 0.0f;
        }
    }

    // --- battery temp drifts slowly with charge/discharge ---
    for (auto& t : st_.batt_temp_c) {
        float target = 22.0f + (st_.batt_current_a > 0 ? 3.0f : -1.5f);
        t += (target - t) * std::min(1.0f, dt_sim_sec / 600.0f);
    }

    ++st_.tick_count;
}

std::uint16_t EpsEngine::effective_mask() const {
    if (!kill_.load()) return 0;
    std::lock_guard<std::mutex> g(pdu_.m);
    return pdu_.channel_mask;
}

// Called from tick() with state_m_ held.  Derives the Database EpsMode
// (0 All_OP_Off, 1 Critical, 2 Safe, 3 Normal, 4 Full) from battery voltage
// with hysteresis, and clears channels according to the shed policy when the
// mode drops.  Restoration is the OBC's job — the EPS only records what it
// shed (shed_mask_) so the OBC can tell shed channels from operator-off ones.
void EpsEngine::update_mode_and_shed(float v) {
    auto mode_for = [&](float vb, uint8_t cur) -> uint8_t {
        // Hysteresis: to move UP a level, voltage must exceed the boundary
        // by hysteresis_v; moving DOWN happens right at the boundary.
        const float h = cfg_.hysteresis_v;
        uint8_t down = (vb >= cfg_.thr_full)     ? 4
                     : (vb >= cfg_.thr_normal)   ? 3
                     : (vb >= cfg_.thr_safe)     ? 2
                     : (vb >= cfg_.thr_critical) ? 1 : 0;
        if (down > cur) {
            uint8_t up = (vb >= cfg_.thr_full + h)     ? 4
                       : (vb >= cfg_.thr_normal + h)   ? 3
                       : (vb >= cfg_.thr_safe + h)     ? 2
                       : (vb >= cfg_.thr_critical + h) ? 1 : 0;
            return std::max(cur, up);
        }
        return down;
    };

    uint8_t prev, next;
    {
        std::lock_guard<std::mutex> g(pdu_.m);
        prev = pdu_.current_mode;
        next = mode_for(v, prev);
        if (next != prev) pdu_.current_mode = next;
    }
    st_.eps_mode = next;
    if (next >= prev) {
        // Recovered or unchanged — a channel switched back on is no longer
        // considered shed (whoever re-enabled it now owns its state).
        std::lock_guard<std::mutex> g(pdu_.m);
        shed_mask_.store(shed_mask_.load() & ~pdu_.channel_mask
                                           & 0x3FFF);
        return;
    }

    // Mode DROPPED — apply the shed policy for the level we entered.
    std::uint16_t clear = 0;
    if (next == 2)      clear = cfg_.shed_safe_bits;
    else if (next == 1) clear = cfg_.shed_critical_bits;
    else if (next == 0) clear = static_cast<std::uint16_t>(~cfg_.keep_below_bits);

    std::lock_guard<std::mutex> g(pdu_.m);
    std::uint16_t actually = pdu_.channel_mask & clear & 0x3FFF;
    if (actually) {
        pdu_.channel_mask &= ~actually;
        shed_mask_.store(shed_mask_.load() | actually);
        std::cout << "      [EPS] mode → " << int(next)
                  << " — load shed: cleared 0x" << std::hex << actually
                  << std::dec << "\n";
    }
}

} // namespace xdos::sim
