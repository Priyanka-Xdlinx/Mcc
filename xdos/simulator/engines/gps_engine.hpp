// SPDX-License-Identifier: Apache-2.0
// GPS sim engine — first-order Keplerian orbit propagator.
// Defaults: 500 km circular LEO, inclination 51.6°, RAAN 0°.  No J2, no drag.
//
// On each tick we:
//   M  = n · (t - t0)                              // mean anomaly
//   x_eci = r·cos(M+ω₀), y = r·sin(M+ω₀)·cos i,  z = r·sin(M+ω₀)·sin i
//   ECEF = ECI · R_z(-θ_GMST)                       // earth rotation
//   lat = asin(z/|r|),  lon = atan2(y,x)            // sub-satellite point
//   alt = |r| - R_earth
//   v   = sqrt(μ/r) at right angles to position
//
// This is enough to make consecutive BESTPOS reads show genuine motion along
// a believable ground track at ~7.6 km/s.

#ifndef XDOS_SIM_GPS_ENGINE_HPP
#define XDOS_SIM_GPS_ENGINE_HPP

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

namespace xdos::sim {

struct GpsEngineConfig {
    double altitude_km   = 500.0;
    double inclination_deg = 51.6;
    double raan_deg      = 0.0;
    double arg_perigee_deg = 0.0;      // argument of perigee (omega_0)
    double mean_anomaly_0_deg = 0.0;   // starting true/mean anomaly
    int    tick_real_ms  = 200;        // 5 Hz wall-clock
    double time_scale    = 30.0;
};

struct GpsEngineState {
    // sub-satellite point
    double lat_deg = 0.0;
    double lon_deg = 0.0;
    double alt_m   = 500'000.0;

    // velocity
    double speed_mps = 7600.0;
    double course_deg = 0.0;    // direction of motion over ground

    // orbital
    double mean_anomaly_deg = 0.0;
    double sim_elapsed_sec  = 0.0;

    // book-keeping
    uint32_t week     = 2200;        // arbitrary GPS week
    double   tow_sec  = 0.0;         // time of week

    // fix quality (static-ish; doesn't really evolve in first-order)
    uint8_t  sol_status  = 0;        // SOL_COMPUTED
    uint8_t  pos_type    = 50;       // NARROW_INT
    uint8_t  n_sats      = 12;
};

class GpsEngine {
public:
    explicit GpsEngine(GpsEngineConfig cfg = {});
    ~GpsEngine();

    void start();
    void stop();

    GpsEngineState snapshot() const;
    const GpsEngineConfig& config() const { return cfg_; }

private:
    void run();
    void tick(double dt_sim_sec);

    GpsEngineConfig          cfg_;
    mutable std::mutex       state_m_;
    GpsEngineState           st_;
    std::atomic<bool>        running_ { false };
    std::thread              th_;

    // pre-computed orbital constants
    double r_m_         = 0.0;       // radius from earth centre
    double n_rad_s_     = 0.0;       // mean motion
    double v_mps_       = 0.0;       // speed
    double inc_rad_     = 0.0;
    double raan_rad_    = 0.0;
    double arg_p_rad_   = 0.0;
    double m0_rad_      = 0.0;
};

} // namespace xdos::sim

#endif
