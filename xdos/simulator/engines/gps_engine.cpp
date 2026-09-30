// SPDX-License-Identifier: Apache-2.0
#define _USE_MATH_DEFINES   // unlock M_PI etc. on MSVC/MinGW <cmath>
#include "gps_engine.hpp"

#include <chrono>
#include <cmath>

namespace xdos::sim {

static constexpr double EARTH_RADIUS_M = 6'378'137.0;
static constexpr double MU_EARTH       = 3.986004418e14;
static constexpr double EARTH_ROT_RAD_S = 7.2921159e-5;
static constexpr double DEG = M_PI / 180.0;

GpsEngine::GpsEngine(GpsEngineConfig cfg) : cfg_(cfg) {
    r_m_      = EARTH_RADIUS_M + cfg_.altitude_km * 1000.0;
    n_rad_s_  = std::sqrt(MU_EARTH / (r_m_ * r_m_ * r_m_));
    v_mps_    = std::sqrt(MU_EARTH / r_m_);
    inc_rad_  = cfg_.inclination_deg   * DEG;
    raan_rad_ = cfg_.raan_deg          * DEG;
    arg_p_rad_= cfg_.arg_perigee_deg   * DEG;
    m0_rad_   = cfg_.mean_anomaly_0_deg * DEG;

    st_.alt_m       = cfg_.altitude_km * 1000.0;
    st_.speed_mps   = v_mps_;
    tick(0.0);                                 // populate initial sub-point
}

GpsEngine::~GpsEngine() { stop(); }

void GpsEngine::start() {
    if (running_.exchange(true)) return;
    th_ = std::thread(&GpsEngine::run, this);
}
void GpsEngine::stop() {
    if (!running_.exchange(false)) return;
    if (th_.joinable()) th_.join();
}

GpsEngineState GpsEngine::snapshot() const {
    std::lock_guard<std::mutex> g(state_m_);
    return st_;
}

void GpsEngine::run() {
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

void GpsEngine::tick(double dt_sim_sec) {
    std::lock_guard<std::mutex> g(state_m_);

    st_.sim_elapsed_sec += dt_sim_sec;
    double M = m0_rad_ + n_rad_s_ * st_.sim_elapsed_sec;
    st_.mean_anomaly_deg = std::fmod(M / DEG, 360.0);
    if (st_.mean_anomaly_deg < 0) st_.mean_anomaly_deg += 360.0;

    // Circular orbit: true anomaly = mean anomaly. u = arg-of-latitude.
    double u = arg_p_rad_ + M;

    // Position in perifocal frame, then rotate to ECI.
    // For a circular orbit aligned with inclination i and RAAN Ω:
    //   x_eci =  r·(cos Ω · cos u  -  sin Ω · sin u · cos i)
    //   y_eci =  r·(sin Ω · cos u  +  cos Ω · sin u · cos i)
    //   z_eci =  r·(sin u · sin i)
    double cu = std::cos(u),  su = std::sin(u);
    double cO = std::cos(raan_rad_), sO = std::sin(raan_rad_);
    double ci = std::cos(inc_rad_),  si = std::sin(inc_rad_);

    double x_eci = r_m_ * (cO * cu - sO * su * ci);
    double y_eci = r_m_ * (sO * cu + cO * su * ci);
    double z_eci = r_m_ * (su * si);

    // Rotate ECI → ECEF by GMST (using sim_elapsed as the time base).
    double theta = EARTH_ROT_RAD_S * st_.sim_elapsed_sec;
    double ct = std::cos(theta), stt = std::sin(theta);
    double x_ecef =  ct * x_eci + stt * y_eci;
    double y_ecef = -stt * x_eci +  ct * y_eci;
    double z_ecef = z_eci;

    // Spherical → lat/lon (good enough for first-order; ignores ellipsoid).
    double r = std::sqrt(x_ecef*x_ecef + y_ecef*y_ecef + z_ecef*z_ecef);
    st_.lat_deg = std::asin(z_ecef / r) / DEG;
    st_.lon_deg = std::atan2(y_ecef, x_ecef) / DEG;
    st_.alt_m   = r - EARTH_RADIUS_M;

    // Ground-track course: derivative of (lat, lon) wrt time.  Approximate
    // by differencing across the tick.  For dt=0 (init), leave default.
    static double last_lat = 0.0, last_lon = 0.0; static bool have_last = false;
    if (have_last && dt_sim_sec > 0) {
        double dlat = (st_.lat_deg - last_lat);
        double dlon = (st_.lon_deg - last_lon) * std::cos(st_.lat_deg * DEG);
        st_.course_deg = std::fmod(std::atan2(dlon, dlat) / DEG + 360.0, 360.0);
    }
    last_lat = st_.lat_deg; last_lon = st_.lon_deg; have_last = true;

    st_.speed_mps = v_mps_;
    st_.tow_sec  = std::fmod(st_.sim_elapsed_sec, 7 * 86400.0);
}

} // namespace xdos::sim
