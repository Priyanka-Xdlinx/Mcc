// SPDX-License-Identifier: Apache-2.0
// ADCS sim engine — torque-free body dynamics with PD attitude control.
//
//   J · dω/dt = -ω × (J·ω) + τ_ctrl
//   dq/dt     = ½ · Ω(ω) · q
//
// τ_ctrl is computed from quaternion error to a commanded reference using a
// classical PD law:  τ = -Kp · q_err_vec - Kd · ω
// First-order — no sensor noise, no magnetorquer saturation, no IGRF field.

#ifndef XDOS_SIM_ADCS_ENGINE_HPP
#define XDOS_SIM_ADCS_ENGINE_HPP

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

namespace xdos::sim {

using Vec3 = std::array<double, 3>;
using Quat = std::array<double, 4>;     // [w, x, y, z]

struct AdcsEngineConfig {
    // Inertia diagonal (kg·m²) — 3U cubesat
    Vec3 J { 0.05, 0.05, 0.08 };

    // PD gains
    double Kp = 0.01;
    double Kd = 0.05;

    // Initial body rates (rad/s) — small spin to make TMs interesting
    Vec3 omega0 { 0.005, -0.003, 0.002 };

    int    tick_real_ms = 100;          // 10 Hz wall-clock
    double time_scale   = 30.0;
};

struct AdcsEngineState {
    Quat q     { 1.0, 0.0, 0.0, 0.0 };  // attitude  (body→inertial)
    Vec3 omega { 0.0, 0.0, 0.0 };       // body rates rad/s

    Quat q_ref { 1.0, 0.0, 0.0, 0.0 };  // commanded reference attitude
    bool control_enabled = false;       // PD loop on/off

    // Derived
    double roll_deg  = 0.0;
    double pitch_deg = 0.0;
    double yaw_deg   = 0.0;

    Vec3   torque_ctrl_nm { 0.0, 0.0, 0.0 };

    double sim_elapsed_sec = 0.0;
    uint32_t tick_count    = 0;

    // Configurable state ADCS TCs can twiddle (placeholder; expanded over time)
    uint8_t  run_mode      = 1;   // 0=off 1=detumble 2=safe 3=nominal
    uint8_t  control_mode  = 0;   // 0=open-loop 1=Bdot 2=quaternion-PD
    uint32_t unix_time_s   = 1'700'000'000;

    // Plausible sensor placeholders
    Vec3 sun_vec_body  { 0.0, 0.0, 1.0 };
    Vec3 mag_vec_body  { 25e-6, 0.0, -45e-6 };  // µT-ish
};

class AdcsEngine {
public:
    explicit AdcsEngine(AdcsEngineConfig cfg = {});
    ~AdcsEngine();

    void start();
    void stop();

    AdcsEngineState snapshot() const;

    // Commanded reference attitude (called by a SET handler).
    void set_reference_quaternion(Quat q_ref);
    void set_control_enabled(bool on);
    void set_run_mode(uint8_t mode);
    void set_control_mode(uint8_t mode);
    void set_unix_time(uint32_t t);

    const AdcsEngineConfig& config() const { return cfg_; }

private:
    void run();
    void tick(double dt_sim_sec);

    AdcsEngineConfig          cfg_;
    mutable std::mutex        state_m_;
    AdcsEngineState           st_;
    std::atomic<bool>         running_ { false };
    std::thread               th_;
};

// Utility: convert quaternion to roll/pitch/yaw (degrees, ZYX intrinsic).
void quat_to_euler_deg(const Quat& q, double& roll, double& pitch, double& yaw);

} // namespace xdos::sim

#endif
