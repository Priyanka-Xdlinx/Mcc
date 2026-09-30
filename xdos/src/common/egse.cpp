// SPDX-License-Identifier: Apache-2.0
#include "common/egse.hpp"

namespace xdos::egse {

static const std::uint8_t MAGIC[4] = { 'X', 'G', 'S', 'E' };

bool is_egse(const Bytes& b) {
    return b.size() >= 6 &&
           b[0]==MAGIC[0] && b[1]==MAGIC[1] && b[2]==MAGIC[2] && b[3]==MAGIC[3];
}

static Bytes frame(std::uint8_t cmd, const Bytes& payload = {}) {
    Bytes f(MAGIC, MAGIC + 4);
    f.push_back(cmd);
    f.push_back(static_cast<std::uint8_t>(payload.size()));
    f.insert(f.end(), payload.begin(), payload.end());
    return f;
}
static void put16(Bytes& b, std::uint16_t v) {
    b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF);
}
static std::uint16_t get16(const Bytes& b, size_t off) {
    return static_cast<std::uint16_t>(b[off] | (b[off+1] << 8));
}

Bytes make_req(std::uint8_t cmd)   { return frame(cmd); }
Bytes make_kill_set(bool on)       { return frame(CMD_KILL_SET, Bytes{ on ? std::uint8_t(1) : std::uint8_t(0) }); }
Bytes make_kill_reply(bool ok)     { return frame(REP_KILL_SET, Bytes{ ok ? std::uint8_t(1) : std::uint8_t(0) }); }

Bytes make_eps_status_reply(const EpsStatus& s) {
    Bytes p;
    p.push_back(s.kill ? 1 : 0);
    p.push_back(s.mode);
    p.push_back(s.phase);
    put16(p, s.phase_remaining_s);
    put16(p, s.vbatt_mv);
    p.push_back(s.soc_pct);
    put16(p, s.chan_mask);
    put16(p, s.shed_mask);
    return frame(REP_EPS_STATUS, p);
}

Bytes make_obc_status_reply(const ObcStatus& s) {
    Bytes p;
    p.push_back(static_cast<std::uint8_t>(s.state));
    put16(p, s.countdown_s);
    p.push_back(s.seq_done);
    return frame(REP_OBC_STATUS, p);
}

std::optional<std::uint8_t> parse_cmd(const Bytes& b) {
    if (!is_egse(b)) return std::nullopt;
    return b[4];
}

std::optional<bool> parse_kill_set(const Bytes& b) {
    auto c = parse_cmd(b);
    if (!c || *c != CMD_KILL_SET || b.size() < 7) return std::nullopt;
    return b[6] != 0;
}

std::optional<EpsStatus> parse_eps_status(const Bytes& b) {
    auto c = parse_cmd(b);
    if (!c || *c != REP_EPS_STATUS || b.size() < 6 + 12) return std::nullopt;
    EpsStatus s;
    size_t o = 6;
    s.kill              = b[o++] != 0;
    s.mode              = b[o++];
    s.phase             = b[o++];
    s.phase_remaining_s = get16(b, o); o += 2;
    s.vbatt_mv          = get16(b, o); o += 2;
    s.soc_pct           = b[o++];
    s.chan_mask         = get16(b, o); o += 2;
    s.shed_mask         = get16(b, o); o += 2;
    return s;
}

std::optional<ObcStatus> parse_obc_status(const Bytes& b) {
    auto c = parse_cmd(b);
    if (!c || *c != REP_OBC_STATUS || b.size() < 6 + 4) return std::nullopt;
    ObcStatus s;
    s.state       = static_cast<ObcState>(b[6]);
    s.countdown_s = get16(b, 7);
    s.seq_done    = b[9];
    return s;
}

const char* obc_state_name(ObcState s) {
    switch (s) {
        case ObcState::Unpowered:  return "UNPOWERED";
        case ObcState::Booting:    return "BOOTING";
        case ObcState::Sequencing: return "SEQUENCING";
        case ObcState::On:         return "ON";
    }
    return "?";
}

const char* eps_mode_name(std::uint8_t m) {
    switch (m) {
        case 0: return "All_OP_Off";
        case 1: return "Critical";
        case 2: return "Safe";
        case 3: return "Normal";
        case 4: return "Full";
    }
    return "?";
}

} // namespace xdos::egse
