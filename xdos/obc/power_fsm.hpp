// SPDX-License-Identifier: Apache-2.0
// OBC power FSM — models the flight computer's power-up behaviour on the bench.
//
//   UNPOWERED ──(kill ON)──▶ BOOTING ──(boot_s)──▶ SEQUENCING ──▶ ON
//        ▲                                                        │
//        └────────────────────(kill OFF at any point)─────────────┘
//
// While ON, the FSM watches the EPS: if load shedding switched a default
// subsystem off and the battery has recovered to Normal, it re-enables it
// (staggered, rate-limited).  All timing honours a time-scale factor so
// tests and demos can run the 30 s boot in milliseconds.
//
// Dependencies are injected as std::function so the FSM is unit-testable
// without sockets: a status query, a mask-setting TC sender, a bridge
// enable/disable hook and a log sink.

#ifndef XDOS_OBC_POWER_FSM_HPP
#define XDOS_OBC_POWER_FSM_HPP

#include "common/egse.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace xdos::obc {

class PowerFsm {
public:
    struct Config {
        float  boot_s           = 30.0f;
        float  stagger_s        = 5.0f;
        float  restore_period_s = 10.0f;
        float  time_scale       = 1.0f;     // divide all waits by this
        // Default-ON sequence: (name, channel bit mask) in switch-on order.
        std::vector<std::pair<std::string, std::uint16_t>> sequence;
    };

    using QueryEps    = std::function<std::optional<egse::EpsStatus>()>;
    using SendMask    = std::function<bool(std::uint16_t new_mask)>;
    using EnableBridge= std::function<void(bool)>;
    using Log         = std::function<void(const std::string&)>;

    PowerFsm(Config cfg, QueryEps q, SendMask s, EnableBridge e, Log log)
        : cfg_(std::move(cfg)), query_(std::move(q)), send_(std::move(s)),
          enable_(std::move(e)), log_(std::move(log)) {}

    // Advance the machine.  Call periodically (~1 Hz real time is plenty).
    void tick(std::chrono::steady_clock::time_point now);

    egse::ObcStatus status() const;

private:
    float scaled(float s) const { return s / (cfg_.time_scale > 0 ? cfg_.time_scale : 1.0f); }

    Config       cfg_;
    QueryEps     query_;
    SendMask     send_;
    EnableBridge enable_;
    Log          log_;

    egse::ObcState state_ = egse::ObcState::Unpowered;
    std::chrono::steady_clock::time_point t_boot_start_{};
    std::chrono::steady_clock::time_point t_next_step_{};
    std::chrono::steady_clock::time_point t_next_restore_{};
    size_t  seq_ix_ = 0;
    float   boot_remaining_s_ = 0.0f;
    mutable std::mutex m_;      // tick() runs on the FSM thread; status() is
                                // called from bridge client threads (EGSE).
};

} // namespace xdos::obc
#endif
