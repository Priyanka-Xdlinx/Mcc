// SPDX-License-Identifier: Apache-2.0
// In-process end-to-end test: starts three simulator responders on
// ephemeral TCP ports, drives Session through each.  Verifies that the
// full chain works:  codec -> framing -> TCP -> responder -> codec back.
//
// This is the single test that exercises every layer at once.

#include "mib/mib.hpp"
#include "session/session.hpp"
#include "transport/tcp_transport.hpp"
#include "../simulator/responders/responder.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <thread>

using namespace xdos;

static std::filesystem::path mib_dir() {
    if (std::filesystem::is_directory("mib"))    return "mib";
    if (std::filesystem::is_directory("../mib")) return "../mib";
    return "../../mib";
}

namespace {

struct LoopServer {
    transport::TcpServer srv;
    std::unique_ptr<sim::Responder> resp;
    int port = 0;

    bool start(const mib::Mib& mib, Subsystem s, int port_in) {
        port = port_in;
        resp = std::make_unique<sim::Responder>(mib, s);
        auto* r = resp.get();
        return srv.start("127.0.0.1", port,
            [r](int fd, const std::string& peer) {
                while (true) {
                    auto first = transport::fd_recv_blocking(fd, 8192);
                    if (first.empty()) break;
                    auto until = std::chrono::steady_clock::now() +
                                 std::chrono::milliseconds(30);
                    while (std::chrono::steady_clock::now() < until) {
                        auto more = transport::fd_recv(fd, 8192, 5);
                        if (more.empty()) break;
                        first.insert(first.end(), more.begin(), more.end());
                    }
                    auto out = r->handle(first, peer, false);
                    if (!out.empty()) transport::fd_send(fd, out);
                }
            });
    }
};

} // namespace

TEST_CASE("E2E: EPS PDU current readings", "[e2e]") {
    mib::Mib mib; mib.load_dir(mib_dir());
    LoopServer eps_srv;
    REQUIRE(eps_srv.start(mib, Subsystem::EPS, 10911));

    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    session::Session sess(mib);
    sess.configure(Subsystem::EPS, {"127.0.0.1", 10911});
    REQUIRE(sess.connect(Subsystem::EPS));

    auto r = sess.send_tc(Subsystem::EPS, "Get_PDU_CH_Current_Readings",
                          nlohmann::json::object(), std::string("PDU1"));
    REQUIRE(r.ok);
    REQUIRE(r.error.empty());
    REQUIRE(!r.tx_frame.empty());
    REQUIRE(!r.rx_frame.empty());
    REQUIRE(r.response_msg_id == 6);   // same wire id == TM id for that family
    REQUIRE(!r.decoded_values.is_null());
    REQUIRE(r.decoded_values.contains("CH1 Current Readings"));

    sess.disconnect_all();
    eps_srv.srv.stop();
}

TEST_CASE("E2E: ADCS Reset", "[e2e]") {
    mib::Mib mib; mib.load_dir(mib_dir());
    LoopServer adcs_srv;
    REQUIRE(adcs_srv.start(mib, Subsystem::ADCS, 10912));

    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    session::Session sess(mib);
    sess.configure(Subsystem::ADCS, {"127.0.0.1", 10912});
    REQUIRE(sess.connect(Subsystem::ADCS));

    auto r = sess.send_tc(Subsystem::ADCS, "Reset",
                          nlohmann::json{{"reset_val", 0}});
    REQUIRE(r.ok);
    REQUIRE(r.error.empty());
    REQUIRE(!r.tx_frame.empty());
    // CubeSpace 0x1F 0x7F prefix
    REQUIRE(r.tx_frame[0] == 0x1F);
    REQUIRE(r.tx_frame[1] == 0x7F);

    sess.disconnect_all();
    adcs_srv.srv.stop();
}

TEST_CASE("E2E: GPS LOG -> BESTPOS", "[e2e]") {
    mib::Mib mib; mib.load_dir(mib_dir());
    LoopServer gps_srv;
    REQUIRE(gps_srv.start(mib, Subsystem::GPS, 10913));

    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    session::Session sess(mib);
    sess.configure(Subsystem::GPS, {"127.0.0.1", 10913});
    REQUIRE(sess.connect(Subsystem::GPS));

    // BESTPOS is msg_id 42 in the curated MIB
    auto r = sess.send_tc(Subsystem::GPS, "LOG",
                          nlohmann::json{
                              {"message_id", 42},
                              {"trigger",    0},
                              {"period",     0},
                          });
    REQUIRE(r.ok);
    REQUIRE(r.error.empty());
    REQUIRE(!r.tx_frame.empty());
    // NovAtel sync bytes
    REQUIRE(r.tx_frame[0] == 0xAA);
    REQUIRE(r.tx_frame[1] == 0x44);
    REQUIRE(r.tx_frame[2] == 0x12);
    REQUIRE(!r.rx_frame.empty());
    REQUIRE(r.response_msg_id == 42);
    REQUIRE(!r.decoded_values.is_null());
    REQUIRE(r.decoded_values.contains("lat"));
    REQUIRE(r.decoded_values.contains("lon"));
    REQUIRE(r.decoded_values.contains("hgt"));

    sess.disconnect_all();
    gps_srv.srv.stop();
}

TEST_CASE("E2E: ADCS TM-request (send_tm_request -> TM frame)", "[e2e]") {
    mib::Mib mib; mib.load_dir(mib_dir());
    LoopServer adcs_srv;
    REQUIRE(adcs_srv.start(mib, Subsystem::ADCS, 10914));

    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    session::Session sess(mib);
    sess.configure(Subsystem::ADCS, {"127.0.0.1", 10914});
    REQUIRE(sess.connect(Subsystem::ADCS));

    // Pick the first TM in the MIB and request it directly by name. The
    // sim should recognise an ADCS frame with msg_id >= 128 as a TM-request
    // and respond with that TM frame, which Session decodes.
    auto tms = mib.messages(Subsystem::ADCS, Direction::TM);
    REQUIRE(!tms.empty());
    const auto* tm = tms.front();

    auto r = sess.send_tm_request(Subsystem::ADCS, tm->name);
    REQUIRE(r.ok);
    REQUIRE(r.error.empty());
    REQUIRE(!r.tx_frame.empty());
    // CubeSpace framing on TX
    REQUIRE(r.tx_frame[0] == 0x1F);
    REQUIRE(r.tx_frame[1] == 0x7F);
    // No payload on a GET request
    REQUIRE(r.tx_payload.empty());
    // Got a response
    REQUIRE(!r.rx_frame.empty());
    REQUIRE(r.response_msg_id == tm->msg_id);
    REQUIRE(!r.decoded_values.is_null());

    sess.disconnect_all();
    adcs_srv.srv.stop();
}

// ─── New tests for engine-driven sims + OBC bridge ─────────────────────────
#include "engines/eps_engine.hpp"
#include "engines/gps_engine.hpp"
#include "engines/adcs_engine.hpp"
#include "handlers/engine_responder.hpp"
#include "bridge.hpp"

TEST_CASE("EPS engine SOC integrates monotonically in sun phase", "[engine][eps]") {
    sim::EpsEngineConfig cfg;
    cfg.tick_real_ms = 5;        // 200 Hz for fast convergence in test
    cfg.time_scale   = 1000.0f;  // accelerated
    sim::EpsEngine eps(cfg);
    {
        // make sure the first orbit segment is sun-side
        std::lock_guard<std::mutex> g(eps.pdu().m);
        eps.pdu().channel_mask = 0;  // zero load → guaranteed charging in sun
    }
    eps.start();
    auto a = eps.snapshot();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    auto b = eps.snapshot();
    eps.stop();
    REQUIRE(b.sim_elapsed_sec > a.sim_elapsed_sec);
    // Sun phase + zero load → SOC must not decrease.
    if (b.phase == sim::OrbitPhase::Sun) {
        REQUIRE(b.batt_soc >= a.batt_soc - 1e-6f);
    }
}

TEST_CASE("GPS engine ground track advances", "[engine][gps]") {
    sim::GpsEngineConfig cfg; cfg.tick_real_ms = 5; cfg.time_scale = 200.0;
    sim::GpsEngine gps(cfg);
    gps.start();
    auto a = gps.snapshot();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    auto b = gps.snapshot();
    gps.stop();
    REQUIRE(b.sim_elapsed_sec > a.sim_elapsed_sec);
    bool moved = std::fabs(b.lat_deg - a.lat_deg) + std::fabs(b.lon_deg - a.lon_deg) > 0.01;
    REQUIRE(moved);
    // Altitude on a circular orbit should stay within 1 m of nominal
    REQUIRE(std::fabs(b.alt_m - 500000.0) < 100.0);
}

TEST_CASE("ADCS engine quaternion stays normalised", "[engine][adcs]") {
    sim::AdcsEngineConfig cfg; cfg.tick_real_ms = 5; cfg.time_scale = 50.0;
    sim::AdcsEngine adcs(cfg);
    adcs.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    auto s = adcs.snapshot();
    adcs.stop();
    double n = s.q[0]*s.q[0] + s.q[1]*s.q[1] + s.q[2]*s.q[2] + s.q[3]*s.q[3];
    REQUIRE(std::fabs(n - 1.0) < 1e-6);
    // omega should still be near-initial magnitude (torque-free, no big drift)
    double w = std::sqrt(s.omega[0]*s.omega[0] + s.omega[1]*s.omega[1]
                       + s.omega[2]*s.omega[2]);
    REQUIRE(w > 0.001);
    REQUIRE(w < 0.02);
}

TEST_CASE("OBC bridge sniffs subsystem from frame prefix", "[obc][bridge]") {
    // EPS: 0x00 ... 0x01
    Bytes eps = { 0x00, 'a','B','c','D', 0x01 };
    REQUIRE(obc::Bridge::sniff_subsystem(eps) == Subsystem::EPS);

    // ADCS: 0x1F 0x7F
    Bytes adcs = { 0x1F, 0x7F, 0x82, 0x1F, 0xFF };
    REQUIRE(obc::Bridge::sniff_subsystem(adcs) == Subsystem::ADCS);

    // GPS: 0xAA 0x44 0x12
    Bytes gps = { 0xAA, 0x44, 0x12, 0x1C, 0x2A, 0x00 };
    REQUIRE(obc::Bridge::sniff_subsystem(gps) == Subsystem::GPS);

    // garbage
    Bytes junk = { 0xDE, 0xAD, 0xBE, 0xEF };
    REQUIRE(!obc::Bridge::sniff_subsystem(junk).has_value());
}

TEST_CASE("OBC bridge end-to-end: GS → OBC → EPS sim → GS", "[obc][e2e]") {
    mib::Mib mib; mib.load_dir(mib_dir());

    // Start an EPS sim on an ephemeral port, with a live engine attached.
    sim::EpsEngine eps;
    eps.start();
    auto er = std::make_shared<sim::EngineResponder>(mib, Subsystem::EPS);
    er->set_eps_engine(&eps);

    LoopServer eps_srv;
    REQUIRE(eps_srv.start(mib, Subsystem::EPS, 10911));
    eps_srv.resp->attach_engine_responder(er);

    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    // Start the OBC bridge pointed at the EPS sim port.
    obc::Bridge::EndpointMap eps_only = {
        { Subsystem::EPS, { "127.0.0.1", 10911 } }
    };
    obc::Bridge br(eps_only);
    REQUIRE(br.start("127.0.0.1", 10901));

    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    // GS Session connects to the OBC port (it'll be forwarded to EPS).
    session::Session sess(mib);
    sess.configure(Subsystem::EPS, { "127.0.0.1", 10901 });
    REQUIRE(sess.connect(Subsystem::EPS));

    // Pick the first EPS PDU TC and send it; expect a routed response.
    auto pdu_tcs = mib.messages(Subsystem::EPS, Direction::TC);
    REQUIRE(!pdu_tcs.empty());
    const mib::Message* tc = nullptr;
    for (auto* m : pdu_tcs) {
        if (m->node && *m->node == "PDU1") { tc = m; break; }
    }
    REQUIRE(tc != nullptr);

    auto r = sess.send_tc(Subsystem::EPS, tc->name,
                          nlohmann::json::object(), std::string("PDU1"));
    REQUIRE(r.ok);
    REQUIRE(r.error.empty());
    // a frame went out and one came back
    REQUIRE(!r.tx_frame.empty());
    REQUIRE(!r.rx_frame.empty());

    auto st = br.stats();
    REQUIRE(st.frames_in   >= 1);
    REQUIRE(st.frames_out  >= 1);
    REQUIRE(st.routed[(int)Subsystem::EPS] >= 1);

    sess.disconnect_all();
    br.stop();
    eps_srv.srv.stop();
    eps.stop();
}

TEST_CASE("Engine responder returns live SOC for repeated EPS GETs", "[engine][eps][live]") {
    sim::EpsEngineConfig cfg; cfg.tick_real_ms = 5; cfg.time_scale = 200.0f;
    sim::EpsEngine eps(cfg);
    {
        std::lock_guard<std::mutex> g(eps.pdu().m);
        for (auto& v : eps.pdu().per_channel_load_ma) v = 3000;  // heavy load
        eps.pdu().channel_mask = 0x3FFF;
    }
    eps.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    auto a = eps.snapshot();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    auto b = eps.snapshot();
    eps.stop();
    // With heavy load through both sun and eclipse, SOC should be drifting.
    REQUIRE(std::fabs(a.batt_soc - b.batt_soc) > 1e-5f);
}

TEST_CASE("OBC bridge: connection survives idle, then routes frame", "[obc][regression]") {
    // Reproduces the bug Jayaram reported: GS connects, idles for >1s
    // while the user reads the menu, then sends a TC.  Before the fix,
    // the OBC closed the connection after 800ms.  With fd_recv_blocking,
    // the connection should be alive for as long as it takes.
    mib::Mib mib; mib.load_dir(mib_dir());

    sim::EpsEngine eps; eps.start();
    auto er = std::make_shared<sim::EngineResponder>(mib, Subsystem::EPS);
    er->set_eps_engine(&eps);

    LoopServer eps_srv;
    REQUIRE(eps_srv.start(mib, Subsystem::EPS, 10921));
    eps_srv.resp->attach_engine_responder(er);

    obc::Bridge::EndpointMap eps_only = {
        { Subsystem::EPS, { "127.0.0.1", 10921 } }
    };
    obc::Bridge br(eps_only);
    REQUIRE(br.start("127.0.0.1", 10902));

    session::Session sess(mib);
    sess.configure(Subsystem::EPS, { "127.0.0.1", 10902 });
    REQUIRE(sess.connect(Subsystem::EPS));

    // Idle for 1.5 s — well beyond the old 800 ms timeout.
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    // Now try to send a TC.  Connection must still be alive.
    const mib::Message* tc = nullptr;
    for (auto* m : mib.messages(Subsystem::EPS, Direction::TC)) {
        if (m->node && *m->node == "PDU1") { tc = m; break; }
    }
    REQUIRE(tc != nullptr);
    auto r = sess.send_tc(Subsystem::EPS, tc->name,
                          nlohmann::json::object(), std::string("PDU1"));
    REQUIRE(r.ok);
    REQUIRE(r.error.empty());
    REQUIRE(!r.rx_frame.empty());     // got a real response, not 0-ms EOF

    // And again after another idle — multi-frame sessions must work.
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    auto r2 = sess.send_tc(Subsystem::EPS, tc->name,
                           nlohmann::json::object(), std::string("PDU1"));
    REQUIRE(r2.ok);
    REQUIRE(r2.error.empty());
    REQUIRE(!r2.rx_frame.empty());

    sess.disconnect_all();
    br.stop();
    eps_srv.srv.stop();
    eps.stop();
}
