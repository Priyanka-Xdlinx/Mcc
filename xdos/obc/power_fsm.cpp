// SPDX-License-Identifier: Apache-2.0
#include "power_fsm.hpp"

#include <algorithm>

namespace xdos::obc {

using clock = std::chrono::steady_clock;
using std::chrono::duration;
using std::chrono::duration_cast;
using std::chrono::milliseconds;

static float secs_between(clock::time_point a, clock::time_point b) {
    return duration_cast<duration<float>>(b - a).count();
}

void PowerFsm::tick(clock::time_point now) {
    auto eps = query_();          // network I/O stays outside the lock
    std::lock_guard<std::mutex> g(m_);

    // Loss of the kill switch (or of the EPS itself) de-powers the OBC from
    // ANY state — physically, O1 just went dead.
    const bool powered = eps && eps->kill;
    if (!powered) {
        if (state_ != egse::ObcState::Unpowered) {
            log_("[OBC] power LOST (kill switch OFF) — shutting down");
            enable_(false);
        }
        state_ = egse::ObcState::Unpowered;
        seq_ix_ = 0;
        return;
    }

    switch (state_) {
    case egse::ObcState::Unpowered:
        t_boot_start_ = now;
        boot_remaining_s_ = cfg_.boot_s;
        state_ = egse::ObcState::Booting;
        log_("[OBC] O1 energized — booting (" + std::to_string(int(cfg_.boot_s)) + " s)");
        break;

    case egse::ObcState::Booting: {
        const float elapsed = secs_between(t_boot_start_, now) * cfg_.time_scale;
        boot_remaining_s_ = std::max(0.0f, cfg_.boot_s - elapsed);
        if (boot_remaining_s_ <= 0.0f) {
            state_ = egse::ObcState::Sequencing;
            seq_ix_ = 0;
            t_next_step_ = now;                 // first subsystem immediately
            log_("[OBC] boot complete — commanding default subsystems ON");
        }
        break;
    }

    case egse::ObcState::Sequencing: {
        if (seq_ix_ >= cfg_.sequence.size()) {
            state_ = egse::ObcState::On;
            t_next_restore_ = now;
            enable_(true);
            log_("[OBC] default sequence done — bridge ENABLED");
            break;
        }
        if (now < t_next_step_) break;
        const auto& [name, bits] = cfg_.sequence[seq_ix_];
        std::uint16_t mask = eps->chan_mask | bits;
        if (send_(mask))
            log_("[OBC] → EPS Set_PDU_CH_State: " + name + " ON");
        else
            log_("[OBC] ! EPS did not ack " + name + " ON (will continue)");
        ++seq_ix_;
        t_next_step_ = now + milliseconds(long(scaled(cfg_.stagger_s) * 1000));
        break;
    }

    case egse::ObcState::On: {
        // Restore defaults the EPS shed, once the battery has recovered.
        if (now < t_next_restore_) break;
        t_next_restore_ = now + milliseconds(long(scaled(cfg_.restore_period_s) * 1000));
        if (eps->mode < 3) break;               // still below Normal
        for (const auto& [name, bits] : cfg_.sequence) {
            if ((eps->shed_mask & bits) && !(eps->chan_mask & bits)) {
                if (send_(eps->chan_mask | bits))
                    log_("[OBC] battery recovered — restoring " + name);
                break;                          // one per period (staggered)
            }
        }
        break;
    }
    }
}

egse::ObcStatus PowerFsm::status() const {
    std::lock_guard<std::mutex> g(m_);
    egse::ObcStatus s;
    s.state       = state_;
    s.countdown_s = static_cast<std::uint16_t>(boot_remaining_s_ + 0.5f);
    s.seq_done    = static_cast<std::uint8_t>(seq_ix_);
    return s;
}

} // namespace xdos::obc
