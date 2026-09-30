// SPDX-License-Identifier: Apache-2.0
// Rate-group engine — the OBC's autonomous housekeeping poller.
//
// IMPORTANT: the OBC has NO subsystem Database.  The engine executes the
// BINARY rate-group LUT (BinaryRateLut) uplinked from the ground: each entry
// carries the exact native TC bytes to replay and pick triplets that route
// response bytes into its tier's block buffer.  The engine never parses a
// command or interprets a field — replies are stripped with protocol-only
// rules and picks are plain memcpys.
//
// Tier-scheduled behaviour (T_sf = one subframe period, from the LUT header):
//   - Every subframe window, the engine polls the entries the precomputed
//     16-position schedule assigns to that slot: VH entries every subframe,
//     H entries once per 2-subframe block, M per 4, L per 8.  Polls are
//     spread evenly across the window; a window that overruns finishes late
//     (stats_.overruns) but never skips a slot, so block phase is kept.
//   - Poll results land in per-tier block buffers (VH 400 / H 800 / M 1200 /
//     L 1400 B).  A failed poll writes 0xFF over the entry's pick ranges —
//     the ground's "subsystem down" marker.  Unassigned block bytes stay 0.
//   - Every T_sf the engine assembles one 1280-B subframe: VH block + the
//     H/M/L block slices selected by slot k = sub-frame-count % 16, wraps it
//     in the 1432-B TM frame (APID 600) and pushes it to the GS.
//   - NO LUT loaded -> NO polls and NO pushes.  The OBC transmits nothing
//     until a binary LUT is uplinked (or restored from the stored copy).
//
// The LUT can be hot-swapped at runtime (swap_lut); block buffers restart
// from the down marker but the sub-frame counter keeps running, so the
// ground's slot alignment survives the swap.

#ifndef XDOS_OBC_RATE_GROUP_ENGINE_HPP
#define XDOS_OBC_RATE_GROUP_ENGINE_HPP

#include "binary_rate_lut.hpp"
#include "codec/ccsds_tm_codec.hpp"
#include "transport/tcp_transport.hpp"
#include "common/types.hpp"

#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace xdos::obc {

class RateGroupEngine {
public:
    using PushSink = std::function<bool(const Bytes& tm_frame)>;

    struct Config {
        // NOTE: there is no per-poll pacing constant.  The gap between polls
        // is T_sf / (polls in this window), so the schedule fits the window
        // by construction however dense the LUT is.  This is a SIMULATION
        // model: on flight hardware the OBC's own bus driver owns the real
        // inter-poll timing, so nothing here needs to pretend to know it.
        int   rm_id           = 1;
        float time_scale      = 1.0f;
    };

    // The engine needs the TM codec (to build the push frame) and a sink; NO
    // Database.  Endpoints default to the standard sim ports; override with
    // set_endpoint().
    RateGroupEngine(ccsds::CcsdsTmCodec& tm_codec, Config cfg, PushSink sink);
    ~RateGroupEngine();

    void set_endpoint(Subsystem s, const std::string& host, int port);

    // Install / hot-swap the binary LUT.  Thread-safe; an empty table is
    // rejected.  Blocks reset to the down marker; counters keep running.
    bool swap_lut(const BinaryRateLut& lut, std::string* err = nullptr);

    void start();
    void stop();
    bool running() const { return running_.load(); }
    bool has_lut() const { return have_lut_.load(); }

    std::uint16_t lut_signature() const;   // the file CRC-16
    int  command_count()  const;           // total entries across tiers
    int  payload_bytes()  const;           // total picked bytes
    int  t_sf_ms()        const;
    int  max_polls_per_window() const;     // schedule worst case (overrun hint)

    struct Stats {
        std::uint64_t polls    = 0;
        std::uint64_t pushes   = 0;
        std::uint64_t overruns = 0;        // subframe windows that ran late
        std::uint64_t ok_by_sub[6]   = {0};
        std::uint64_t fail_by_sub[6] = {0};
    };
    Stats stats() const;

    // One schedule position: an entry index within a tier.
    struct PollTask { int tier = 0; int entry = 0; };

    // Precompute the 16-position poll schedule: tier t entry i is polled in
    // the slots where slot % span == i % span (span = 1/2/4/8).  Static and
    // deterministic — also the unit-test surface.
    static std::array<std::vector<PollTask>, lut::kSubframesPerMaster>
    build_schedule(const BinaryRateLut& lut);

    // Assemble subframe k (0..15) from per-tier block buffers: VH block +
    // H slice k%2 + M slice k%4 + L slice k%8 + zeroed reserved tail.
    // Exposed static for the unit tests.
    static Bytes pack_subframe(const std::array<Bytes,4>& blocks, int k);

private:
    void  poll_loop();
    void  push_loop();
    Bytes poll_one(Subsystem sub, const Bytes& tc_bytes);
    void  apply_result(int tier, int entry, const Bytes& payload);

    // Protocol-only reply de-framing (NO Database).
    static Bytes strip_reply(Subsystem s, const Bytes& reply);

    struct Endpoint { std::string host = "127.0.0.1"; int port = 0; };

    ccsds::CcsdsTmCodec&  tm_;
    Config                cfg_;
    PushSink              sink_;

    mutable std::mutex    lut_mu_;
    BinaryRateLut         lut_;
    std::array<std::vector<PollTask>, lut::kSubframesPerMaster> schedule_;
    std::map<Subsystem, Endpoint> endpoints_;

    mutable std::mutex    block_mu_;
    std::array<Bytes,4>   block_;          // per-tier block buffers

    std::atomic<bool>     have_lut_ { false };

    std::map<Subsystem, std::unique_ptr<transport::TcpClient>> conns_;

    std::atomic<bool>     running_ { false };
    std::thread           poll_th_;
    std::thread           push_th_;

    mutable std::mutex    stats_mu_;
    Stats                 stats_;
};

}  // namespace xdos::obc
#endif
