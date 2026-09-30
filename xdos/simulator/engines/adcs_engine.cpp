// SPDX-License-Identifier: Apache-2.0
#define _USE_MATH_DEFINES   // unlock M_PI etc. on MSVC/MinGW <cmath>
#include "adcs_engine.hpp"

#include <chrono>
#include <cmath>

namespace xdos::sim {

namespace {

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return { a[1]*b[2] - a[2]*b[1],
             a[2]*b[0] - a[0]*b[2],
             a[0]*b[1] - a[1]*b[0] };
}

inline Quat quat_mul(const Quat& a, const Quat& b) {
    return {
        a[0]*b[0] - a[1]*b[1] - a[2]*b[2] - a[3]*b[3],
        a[0]*b[1] + a[1]*b[0] + a[2]*b[3] - a[3]*b[2],
        a[0]*b[2] - a[1]*b[3] + a[2]*b[0] + a[3]*b[1],
        a[0]*b[3] + a[1]*b[2] - a[2]*b[1] + a[3]*b[0]
    };
}

inline Quat quat_conj(const Quat& q) { return { q[0], -q[1], -q[2], -q[3] }; }

inline void quat_normalize(Quat& q) {
    double n = std::sqrt(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
    if (n > 0) for (auto& x : q) x /= n;
}

} // namespace

void quat_to_euler_deg(const Quat& q, double& roll, double& pitch, double& yaw) {
    const double w = q[0], x = q[1], y = q[2], z = q[3];
    // ZYX intrinsic — standard aerospace
    double sinr_cosp = 2.0 * (w * x + y * z);
    double cosr_cosp = 1.0 - 2.0 * (x * x + y * y);
    roll = std::atan2(sinr_cosp, cosr_cosp) * 180.0 / M_PI;

    double sinp = 2.0 * (w * y - z * x);
    if (std::fabs(sinp) >= 1.0)
        pitch = std::copysign(90.0, sinp);
    else
        pitch = std::asin(sinp) * 180.0 / M_PI;

    double siny_cosp = 2.0 * (w * z + x * y);
    double cosy_cosp = 1.0 - 2.0 * (y * y + z * z);
    yaw  = std::atan2(siny_cosp, cosy_cosp) * 180.0 / M_PI;
}

AdcsEngine::AdcsEngine(AdcsEngineConfig cfg) : cfg_(cfg) {
    st_.omega = cfg_.omega0;
    quat_to_euler_deg(st_.q, st_.roll_deg, st_.pitch_deg, st_.yaw_deg);
}

AdcsEngine::~AdcsEngine() { stop(); }

void AdcsEngine::start() {
    if (running_.exchange(true)) return;
    th_ = std::thread(&AdcsEngine::run, this);
}
void AdcsEngine::stop() {
    if (!running_.exchange(false)) return;
    if (th_.joinable()) th_.join();
}

AdcsEngineState AdcsEngine::snapshot() const {
    std::lock_guard<std::mutex> g(state_m_);
    return st_;
}

void AdcsEngine::set_reference_quaternion(Quat q_ref) {
    quat_normalize(q_ref);
    std::lock_guard<std::mutex> g(state_m_);
    st_.q_ref = q_ref;
}
void AdcsEngine::set_control_enabled(bool on) {
    std::lock_guard<std::mutex> g(state_m_); st_.control_enabled = on;
}
void AdcsEngine::set_run_mode(uint8_t mode) {
    std::lock_guard<std::mutex> g(state_m_); st_.run_mode = mode;
}
void AdcsEngine::set_control_mode(uint8_t mode) {
    std::lock_guard<std::mutex> g(state_m_); st_.control_mode = mode;
}
void AdcsEngine::set_unix_time(uint32_t t) {
    std::lock_guard<std::mutex> g(state_m_); st_.unix_time_s = t;
}

void AdcsEngine::run() {
    using namespace std::chrono;
    const auto tick_dur = milliseconds(cfg_.tick_real_ms);
    const double dt_sim = (cfg_.tick_real_ms / 1000.0) * cfg_.time_scale;

    auto next = steady_clock::now() + tick_dur;
    while (running_.load()) {
        tick(dt_sim);
        std::this_thread::sleep_until(next);
        next += tick_dur;
    }
}

void AdcsEngine::tick(double dt) {
    std::lock_guard<std::mutex> g(state_m_);

    st_.sim_elapsed_sec += dt;
    ++st_.tick_count;

    // ─── 1. Compute control torque (PD law on quaternion error) ────────
    Vec3 tau { 0, 0, 0 };
    if (st_.control_enabled) {
        // q_err = q_ref⁻¹ · q  (small-angle vector part drives torque)
        Quat q_err = quat_mul(quat_conj(st_.q_ref), st_.q);
        if (q_err[0] < 0) for (auto& x : q_err) x = -x;   // shortest path
        Vec3 q_err_v { q_err[1], q_err[2], q_err[3] };
        for (int i = 0; i < 3; ++i)
            tau[i] = -cfg_.Kp * q_err_v[i] - cfg_.Kd * st_.omega[i];
    }
    st_.torque_ctrl_nm = tau;

    // ─── 2. Euler rotational dynamics:  J · dω = (τ - ω × (J·ω)) dt ────
    Vec3 Jw   { cfg_.J[0]*st_.omega[0],
                cfg_.J[1]*st_.omega[1],
                cfg_.J[2]*st_.omega[2] };
    Vec3 wxJw = cross(st_.omega, Jw);
    Vec3 dw { (tau[0] - wxJw[0]) / cfg_.J[0],
              (tau[1] - wxJw[1]) / cfg_.J[1],
              (tau[2] - wxJw[2]) / cfg_.J[2] };
    for (int i = 0; i < 3; ++i) st_.omega[i] += dw[i] * dt;

    // ─── 3. Quaternion integration: q += ½ Ω(ω) q · dt ─────────────────
    const auto& w = st_.omega;
    Quat dq {
        -0.5 * (w[0]*st_.q[1] + w[1]*st_.q[2] + w[2]*st_.q[3]),
         0.5 * (w[0]*st_.q[0] + w[2]*st_.q[2] - w[1]*st_.q[3]),
         0.5 * (w[1]*st_.q[0] - w[2]*st_.q[1] + w[0]*st_.q[3]),
         0.5 * (w[2]*st_.q[0] + w[1]*st_.q[1] - w[0]*st_.q[2])
    };
    for (int i = 0; i < 4; ++i) st_.q[i] += dq[i] * dt;
    quat_normalize(st_.q);

    // ─── 4. Derived Euler angles for telemetry ─────────────────────────
    quat_to_euler_deg(st_.q, st_.roll_deg, st_.pitch_deg, st_.yaw_deg);

    // ─── 5. Sun vector in body frame (just point it at +Z in inertial,
    //         then rotate into body) — coarse placeholder ──────────────
    {
        // Inertial sun = (0, 0, 1) for first-order.  Rotate by q⁻¹.
        Quat sun_inert { 0, 0, 0, 1 };
        Quat tmp = quat_mul(quat_conj(st_.q), sun_inert);
        Quat rotated = quat_mul(tmp, st_.q);
        st_.sun_vec_body = { rotated[1], rotated[2], rotated[3] };
    }

    // Unix time advances at sim rate
    st_.unix_time_s = static_cast<uint32_t>(st_.unix_time_s + dt);
}

} // namespace xdos::sim
