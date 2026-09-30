// SPDX-License-Identifier: Apache-2.0
// EPS sim engine — closed-loop power model for the spacecraft.
//
// Models a LEO orbit (default 90-min period, 60% sunlit).  Every tick:
//   solar_w       = panel_power · cos²(orbit-90°)   during SUN; 0 in ECLIPSE
//   load_a        = Σ enabled channels · current_per_ch
//   net_a         = solar_w / Vbatt - load_a
//   d_charge_ah   = net_a · dt_hours
//   SOC          += d_charge_ah / capacity_ah
//   Vbatt         = lifepo4_ocv(SOC) · series_cells
//
// Lifted from the reference simulator, ported to C++17 + std::thread.

#ifndef XDOS_SIM_EPS_ENGINE_HPP
#define XDOS_SIM_EPS_ENGINE_HPP

#define _USE_MATH_DEFINES   // unlock M_PI etc. on MSVC/MinGW <cmath>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

namespace xdos::sim {

enum class OrbitPhase { Sun = 0, Eclipse = 1 };

struct EpsEngineConfig {
    float orbit_period_sec = 5400.0f;   // 90 min LEO
    float sun_fraction     = 0.60f;     // 60% sunlit
    float panel_power_w    = 40.0f;     // total solar power at full sun
    float mppt_eff         = 0.97f;
    float batt_capacity_ah = 20.0f;     // 240 Wh @ 12 V
    int   batt_series_cells = 3;        // 3S → ~10 V LiFePO4
    int   tick_real_ms     = 200;       // 5 Hz wall-clock
    float time_scale       = 30.0f;     // 1 real-sec = 30 sim-sec

    // ── Flight power model (off by default: legacy/in-process behaviour) ──
    // When enabled: Li-ion OCV curve (matches the EP-007 voltage thresholds),
    // kill switch gates all channel outputs, EPS mode is derived from battery
    // voltage, and load shedding clears channels on mode drops.
    bool  flight_power = false;
    int   chemistry    = 0;             // 0 = LiFePO4 (legacy), 1 = Li-ion 3S
    float thr_full = 12.0f, thr_normal = 11.1f, thr_safe = 9.9f, thr_critical = 9.0f;
    float hysteresis_v = 0.15f;
    std::uint16_t shed_safe_bits     = 0;   // cleared on entering Safe
    std::uint16_t shed_critical_bits = 0;   // cleared on entering Critical
    std::uint16_t keep_below_bits    = 0;   // ONLY these survive below Critical
};

// Snapshot of engine state — what handlers see when answering a GET.
struct EpsEngineState {
    float    orbit_angle_deg = 0.0f;
    OrbitPhase phase         = OrbitPhase::Sun;
    float    sim_elapsed_sec = 0.0f;

    float    solar_power_w   = 0.0f;
    float    mppt_current_a  = 0.0f;
    float    load_current_a  = 0.0f;
    float    net_current_a   = 0.0f;

    float    batt_soc        = 0.80f;          // 0..1
    float    batt_voltage    = 10.05f;         // V
    float    batt_current_a  = 0.0f;           // +charge / -discharge
    std::array<float,3> batt_temp_c { 22.0f, 22.0f, 22.0f };

    std::array<float,7> mppt_ch_voltage { 0,0,0,0,0,0,0 };
    std::array<float,7> mppt_ch_current { 0,0,0,0,0,0,0 };

    uint32_t tick_count    = 0;
    uint32_t eclipse_count = 0;
    float    energy_in_wh  = 0.0f;
    float    energy_out_wh = 0.0f;

    uint8_t  eps_mode          = 3;    // Database EpsMode: 0..4 (flight model)
    float    phase_remaining_s = 0.0f; // sim-seconds until next phase boundary
};

// Live PDU state — mutated by SET handlers, read by sim engine.
struct PduState {
    mutable std::mutex m;
    uint16_t channel_mask = 0x01FF;            // 14-ch, default first 9 on
    std::array<uint16_t,14> current_limit_ma   = {500,500,1500,2000,1500,2000,300,500,2000,1500,800,200,200,200};
    std::array<uint16_t,14> per_channel_load_ma{ 50,50,300,400,250,400, 50,100,500,300,100, 0, 0, 0};
    uint8_t  current_mode = 0;                 // 0=STARTUP 1=SAFE 2=NOMINAL 3=COMMS
};

// LiFePO4 OCV per cell as a function of SOC.  Piecewise-linear approximation;
// good enough for first-order — flat 3.30 V over most of the range with
// shoulders at the top and bottom.
float lifepo4_ocv(float soc);

// Li-ion (NMC/LCO) OCV per cell — 3.0 V empty to 4.2 V full.  Used by the
// flight power model so the pack voltage spans the EP-007 thresholds
// (9.0 … 12.6 V on a 3S pack).
float liion_ocv(float soc);

class EpsEngine {
public:
    explicit EpsEngine(EpsEngineConfig cfg = {});
    ~EpsEngine();

    void start();
    void stop();

    // Thread-safe state snapshot for handlers to read.
    EpsEngineState snapshot() const;

    // ── Kill switch (flight model) ─────────────────────────────────────────
    // ON  → battery bus energized, channel outputs follow channel_mask.
    // OFF → every output dead regardless of mask; PDU SETs are rejected.
    void set_kill(bool on)        { kill_.store(on); }
    bool kill() const             { return kill_.load(); }

    // Mask actually driving outputs: channel_mask gated by the kill switch.
    std::uint16_t effective_mask() const;

    // Channels the EPS shed autonomously (mode drop) that are still off.
    std::uint16_t shed_mask() const { return shed_mask_.load(); }

    // PDU state is shared (mutex inside).  SET handlers grab the lock,
    // mutate, release.  Engine reads it on every tick to compute the load.
    PduState&       pdu()       { return pdu_; }
    const PduState& pdu() const { return pdu_; }

    const EpsEngineConfig& config() const { return cfg_; }

private:
    void run();
    void tick(float dt_sim_sec);
    void update_mode_and_shed(float vbatt);

    EpsEngineConfig         cfg_;
    PduState                pdu_;
    mutable std::mutex      state_m_;
    EpsEngineState          st_;
    std::atomic<bool>       running_ { false };
    std::thread             th_;
    std::atomic<bool>          kill_      { true };   // legacy default: bus live
    std::atomic<std::uint16_t> shed_mask_ { 0 };
};

} // namespace xdos::sim

#endif
