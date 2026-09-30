// SPDX-License-Identifier: Apache-2.0
// Flight power model tests:
//   1. EPS engine — kill switch gating, voltage-driven mode transitions,
//      load shedding on the way down, shed-mask bookkeeping on recovery.
//   2. OBC PowerFsm — boot timing, staggered default-ON sequence through
//      the (scripted) EPS, bridge enable, kill-loss reset, shed restore.
//
// Everything runs heavily time-scaled so the whole file finishes in a few
// wall-clock seconds while covering minutes of sim time.

#include <catch2/catch_test_macros.hpp>

#include "engines/eps_engine.hpp"
#include "power_fsm.hpp"

#include <chrono>
#include <thread>
#include <vector>

using namespace xdos;
using namespace std::chrono;

// Channel bit helpers (mirror mib/eps.json "channels"):
//   CH1/CH2 = OBC, CH3 = GPS, CH4 = ADCS, CH5 = SBAND, CH10 = PAYLOAD
static constexpr std::uint16_t OBC_B   = 0x0003;
static constexpr std::uint16_t GPS_B   = 0x0004;
static constexpr std::uint16_t ADCS_B  = 0x0008;
static constexpr std::uint16_t SBAND_B = 0x0010;
static constexpr std::uint16_t PAY_B   = 0x0200;

TEST_CASE("EPS kill switch gates every output", "[power]") {
    sim::EpsEngineConfig cfg;
    cfg.flight_power = true;
    cfg.chemistry    = 1;
    sim::EpsEngine e(cfg);
    {
        std::lock_guard<std::mutex> g(e.pdu().m);
        e.pdu().channel_mask = OBC_B | ADCS_B;
    }
    e.set_kill(false);
    REQUIRE(e.effective_mask() == 0);
    e.set_kill(true);
    REQUIRE(e.effective_mask() == (OBC_B | ADCS_B));
}

TEST_CASE("EPS flight model: discharge sheds loads, bookkeeping clears on re-enable",
          "[power]") {
    sim::EpsEngineConfig cfg;
    cfg.flight_power   = true;
    cfg.chemistry      = 1;          // Li-ion 3S: 9.0 … 12.6 V
    cfg.tick_real_ms   = 5;
    cfg.time_scale     = 4000.0f;    // 20 sim-seconds per tick
    cfg.panel_power_w  = 0.0f;       // permanent night → guaranteed discharge
    cfg.shed_safe_bits     = PAY_B;
    cfg.shed_critical_bits = PAY_B | ADCS_B | GPS_B;
    cfg.keep_below_bits    = OBC_B;

    sim::EpsEngine e(cfg);
    {
        std::lock_guard<std::mutex> g(e.pdu().m);
        e.pdu().channel_mask = OBC_B | GPS_B | ADCS_B | SBAND_B | PAY_B;
        e.pdu().current_mode = 3;
    }
    e.start();

    bool saw_safe_shed = false;
    std::uint8_t mode = 3;
    for (int i = 0; i < 6000; ++i) {
        std::this_thread::sleep_for(milliseconds(6));
        auto st = e.snapshot();
        mode = st.eps_mode;
        std::uint16_t mask;
        {
            std::lock_guard<std::mutex> g(e.pdu().m);
            mask = e.pdu().channel_mask;
        }
        if (mode == 2 && !(mask & PAY_B)) saw_safe_shed = true;
        if (mode <= 1) break;
    }
    e.stop();

    std::uint16_t mask;
    {
        std::lock_guard<std::mutex> g(e.pdu().m);
        mask = e.pdu().channel_mask;
    }
    REQUIRE(mode <= 1);                              // reached Critical
    REQUIRE(saw_safe_shed);                          // Safe shed PAYLOAD on the way
    REQUIRE((mask & (PAY_B | ADCS_B | GPS_B)) == 0); // Critical shed the rest
    REQUIRE((mask & SBAND_B) == SBAND_B);            // comms lifeline kept
    REQUIRE((e.shed_mask() & (PAY_B | ADCS_B | GPS_B)) == (PAY_B | ADCS_B | GPS_B));

    // Re-enabling a shed channel clears its shed-bookkeeping bit.
    {
        std::lock_guard<std::mutex> g(e.pdu().m);
        e.pdu().channel_mask |= PAY_B;
    }
    e.start();
    std::this_thread::sleep_for(milliseconds(60));
    e.stop();
    REQUIRE((e.shed_mask() & PAY_B) == 0);
}

TEST_CASE("EPS flight model: charging recovers the mode", "[power]") {
    sim::EpsEngineConfig cfg;
    cfg.flight_power  = true;
    cfg.chemistry     = 1;
    cfg.tick_real_ms  = 5;
    cfg.time_scale    = 4000.0f;
    cfg.panel_power_w = 300.0f;      // strong sun, light load → net charge
    sim::EpsEngine e(cfg);
    {
        std::lock_guard<std::mutex> g(e.pdu().m);
        e.pdu().channel_mask = OBC_B;
        e.pdu().current_mode = 3;
    }
    e.start();
    std::uint8_t mode = 3;
    float v = 0.0f;
    for (int i = 0; i < 3000; ++i) {
        std::this_thread::sleep_for(milliseconds(6));
        auto st = e.snapshot();
        mode = st.eps_mode; v = st.batt_voltage;
        if (mode == 4) break;
    }
    e.stop();
    REQUIRE(mode == 4);              // Full
    REQUIRE(v >= 12.0f);
}

TEST_CASE("OBC PowerFsm: boot, staggered default-ON, kill loss, shed restore",
          "[power]") {
    // Scripted EPS the FSM talks to.
    bool           kill = false;
    std::uint16_t  mask = OBC_B;
    std::uint16_t  shed = 0;
    std::uint8_t   mode = 3;
    std::vector<std::uint16_t> sends;
    std::vector<bool>          enables;

    obc::PowerFsm::Config cfg;
    cfg.boot_s = 0.20f; cfg.stagger_s = 0.08f; cfg.restore_period_s = 0.15f;
    cfg.time_scale = 1.0f;
    cfg.sequence = { { "ADCS",  ADCS_B  },
                     { "GPS",   GPS_B   },
                     { "SBAND", SBAND_B } };

    obc::PowerFsm fsm(cfg,
        [&]() -> std::optional<egse::EpsStatus> {
            egse::EpsStatus s;
            s.kill = kill; s.mode = mode;
            s.chan_mask = kill ? mask : 0;
            s.shed_mask = shed;
            return s;
        },
        [&](std::uint16_t m) { sends.push_back(m); mask = m; return true; },
        [&](bool on)         { enables.push_back(on); },
        [](const std::string&) {});

    auto pump = [&](int ms) {
        auto end = steady_clock::now() + milliseconds(ms);
        while (steady_clock::now() < end) {
            fsm.tick(steady_clock::now());
            std::this_thread::sleep_for(milliseconds(15));
        }
    };

    // Kill OFF → stays unpowered, nothing sent.
    pump(80);
    REQUIRE(fsm.status().state == egse::ObcState::Unpowered);
    REQUIRE(sends.empty());

    // Kill ON → boots, then sequences ADCS → GPS → SBAND, then enables bridge.
    kill = true;
    pump(900);
    REQUIRE(fsm.status().state == egse::ObcState::On);
    REQUIRE(sends.size() == 3);
    REQUIRE(sends[0] == (OBC_B | ADCS_B));
    REQUIRE(sends[1] == (OBC_B | ADCS_B | GPS_B));
    REQUIRE(sends[2] == (OBC_B | ADCS_B | GPS_B | SBAND_B));
    REQUIRE(enables == std::vector<bool>{ true });

    // EPS shed GPS in eclipse; battery recovered → FSM restores it.
    mask &= static_cast<std::uint16_t>(~GPS_B);
    shed  = GPS_B;
    pump(500);
    REQUIRE(sends.size() == 4);
    REQUIRE((sends.back() & GPS_B) == GPS_B);
    shed = 0;

    // Kill loss from ON → bridge disabled, back to unpowered.
    kill = false;
    pump(80);
    REQUIRE(fsm.status().state == egse::ObcState::Unpowered);
    REQUIRE(enables.back() == false);

    // Cycle repeats cleanly.
    kill = true;
    pump(900);
    REQUIRE(fsm.status().state == egse::ObcState::On);
}
