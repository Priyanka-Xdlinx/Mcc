// SPDX-License-Identifier: Apache-2.0
// EGSE "umbilical" — a tiny sim-control protocol between the ground station
// and the simulators, deliberately OUTSIDE the flight Database.  It models
// the physical bench harness: the kill switch and status telemetry that in
// reality travel over GSE lines, not RF.  It must work even when the OBC is
// unpowered, which is why the GS talks to the EPS sim directly.
//
// Frame:  'X' 'G' 'S' 'E'  cmd(1)  len(1)  payload[len]
//
// Commands (GS → sim)                 Replies (sim → GS)
//   0x01 EPS_STATUS_REQ                 0x81 EpsStatus
//   0x02 KILL_SET   {on u8}             0x82 {ok u8}
//   0x11 OBC_STATUS_REQ                 0x91 ObcStatus

#ifndef XDOS_COMMON_EGSE_HPP
#define XDOS_COMMON_EGSE_HPP

#include <cstdint>
#include <optional>
#include <vector>

namespace xdos::egse {

using Bytes = std::vector<std::uint8_t>;

constexpr std::uint8_t CMD_EPS_STATUS = 0x01;
constexpr std::uint8_t CMD_KILL_SET   = 0x02;
constexpr std::uint8_t CMD_OBC_STATUS = 0x11;
constexpr std::uint8_t REP_EPS_STATUS = 0x81;
constexpr std::uint8_t REP_KILL_SET   = 0x82;
constexpr std::uint8_t REP_OBC_STATUS = 0x91;

struct EpsStatus {
    bool          kill            = false;
    std::uint8_t  mode            = 0;      // Database EpsMode 0..4
    std::uint8_t  phase           = 0;      // 0 = sunlit, 1 = eclipse
    std::uint16_t phase_remaining_s = 0;    // sim-seconds to next phase
    std::uint16_t vbatt_mv        = 0;
    std::uint8_t  soc_pct         = 0;
    std::uint16_t chan_mask       = 0;      // effective (kill-gated) mask
    std::uint16_t shed_mask       = 0;      // channels EPS shed autonomously
};

enum class ObcState : std::uint8_t { Unpowered = 0, Booting = 1, Sequencing = 2, On = 3 };

struct ObcStatus {
    ObcState      state       = ObcState::Unpowered;
    std::uint16_t countdown_s = 0;          // boot countdown (Booting only)
    std::uint8_t  seq_done    = 0;          // default-ON steps completed
};

bool  is_egse(const Bytes& b);

Bytes make_req(std::uint8_t cmd);                    // no-payload request
Bytes make_kill_set(bool on);
Bytes make_eps_status_reply(const EpsStatus& s);
Bytes make_kill_reply(bool ok);
Bytes make_obc_status_reply(const ObcStatus& s);

// Parse helpers — return nullopt on wrong magic/cmd/short frame.
std::optional<std::uint8_t>  parse_cmd(const Bytes& b);
std::optional<bool>          parse_kill_set(const Bytes& b);
std::optional<EpsStatus>     parse_eps_status(const Bytes& b);
std::optional<ObcStatus>     parse_obc_status(const Bytes& b);

const char* obc_state_name(ObcState s);
const char* eps_mode_name(std::uint8_t m);   // Database EpsMode names

} // namespace xdos::egse
#endif
