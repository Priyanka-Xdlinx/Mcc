# ─────────────────────────────────────────────────────────────────────────────
#  XDOS Ground Station — C++17
#  Linux (gcc/clang) + Windows (MSVC/MinGW) · nlohmann/json (vendored) · Catch2 v3
# ─────────────────────────────────────────────────────────────────────────────
cmake_minimum_required(VERSION 3.20)
project(xdos_groundstation
        VERSION 0.1.0
        LANGUAGES CXX
        DESCRIPTION "XDOS satellite ground station — MIB-driven, subsystem-native framing")

set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Per-compiler warning flags
if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN
                            _WINSOCK_DEPRECATED_NO_WARNINGS
                            _WIN32_WINNT=0x0A00)   # target Windows 10 / 11
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wno-unused-parameter)
endif()

# Windows needs Winsock + suppress signed/unsigned conversion noise from
# the existing socket-fd integer plumbing.
if(WIN32)
    link_libraries(ws2_32)
endif()

find_package(Threads REQUIRED)

# vendored json.hpp
add_library(json_hdr INTERFACE)
target_include_directories(json_hdr INTERFACE ${CMAKE_SOURCE_DIR}/third_party)

# ── core library ──────────────────────────────────────────────────────────────
add_library(xdos_core STATIC
    src/common/crc.cpp
    src/common/egse.cpp
    src/common/hex.cpp
    src/mib/mib.cpp
    src/codec/codec.cpp
    src/codec/ccsds_codec.cpp
    src/codec/ccsds_tm_codec.cpp
    src/framing/eps_framing.cpp
    src/framing/adcs_framing.cpp
    src/framing/gps_framing.cpp
    src/framing/payload_framing.cpp
    src/framing/uhf_framing.cpp
    src/framing/sensor_framing.cpp
    src/subsystems/xlink_codec.cpp
    src/subsystems/xlink_keys.cpp
    src/transport/tcp_transport.cpp
    src/session/session.cpp
)
target_include_directories(xdos_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(xdos_core PUBLIC json_hdr Threads::Threads)

# ── command-line ground station ───────────────────────────────────────────────
add_executable(xdos_gs src/cli/main.cpp
    src/cli/value_format.cpp
    src/lut/binary_lut_builder.cpp
    src/lut/rate_group_csv.cpp)
target_include_directories(xdos_gs PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(xdos_gs PRIVATE xdos_core)

# ── standalone Master Frame telemetry terminal ────────────────────────────────
add_executable(xdos_tm_view src/cli/tm_view_main.cpp
    src/cli/master_frame_store.cpp
    src/cli/bin_lut_twin.cpp
    src/cli/mcc_forward.cpp
    src/cli/value_format.cpp)
target_include_directories(xdos_tm_view PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/obc ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(xdos_tm_view PRIVATE xdos_core)

# ── per-subsystem bus simulators ──────────────────────────────────────────────
set(XDOS_SIM_SHARED
    simulator/engines/eps_engine.cpp
    simulator/engines/adcs_engine.cpp
    simulator/engines/gps_engine.cpp
    simulator/handlers/engine_responder.cpp
    simulator/responders/responder.cpp
    simulator/plausible.cpp
)

add_executable(xdos_eps_sim  simulator/eps_sim_main.cpp  ${XDOS_SIM_SHARED})
target_include_directories(xdos_eps_sim  PRIVATE ${CMAKE_SOURCE_DIR}/simulator)
target_link_libraries     (xdos_eps_sim  PRIVATE xdos_core)

add_executable(xdos_adcs_sim simulator/adcs_sim_main.cpp ${XDOS_SIM_SHARED})
target_include_directories(xdos_adcs_sim PRIVATE ${CMAKE_SOURCE_DIR}/simulator)
target_link_libraries     (xdos_adcs_sim PRIVATE xdos_core)

add_executable(xdos_gps_sim  simulator/gps_sim_main.cpp  ${XDOS_SIM_SHARED})
target_include_directories(xdos_gps_sim  PRIVATE ${CMAKE_SOURCE_DIR}/simulator)
target_link_libraries     (xdos_gps_sim  PRIVATE xdos_core)

# Payload sim — no engine, no MIB; just decode and ACK.
add_executable(xdos_payload_sim simulator/payload_sim_main.cpp)
target_include_directories(xdos_payload_sim PRIVATE ${CMAKE_SOURCE_DIR}/simulator)
target_link_libraries     (xdos_payload_sim PRIVATE xdos_core)

# XLink sim — service-server responder for the IQ spacecom XLink transceiver.
add_executable(xdos_xlink_sim simulator/xlink_sim_main.cpp)
target_include_directories(xdos_xlink_sim PRIVATE ${CMAKE_SOURCE_DIR}/simulator)
target_link_libraries     (xdos_xlink_sim PRIVATE xdos_core)

# UHF sim — standalone byte-faithful model of the Drishti OpenLST firmware.
add_executable(xdos_uhf_sim simulator/uhf_sim_main.cpp)
target_include_directories(xdos_uhf_sim PRIVATE ${CMAKE_SOURCE_DIR}/simulator)
target_link_libraries     (xdos_uhf_sim PRIVATE xdos_core)

# Sensor controller sim — avionics board power / SSD select / temperatures.
add_executable(xdos_sensor_sim simulator/sensor_sim_main.cpp)
target_include_directories(xdos_sensor_sim PRIVATE ${CMAKE_SOURCE_DIR}/simulator)
target_link_libraries     (xdos_sensor_sim PRIVATE xdos_core)

# ── OBC bridge + inspector CLI ────────────────────────────────────────────────
add_executable(xdos_obc
    obc/main.cpp
    obc/bridge.cpp
    obc/power_fsm.cpp
    obc/binary_rate_lut.cpp
    obc/rate_group_engine.cpp
    obc/ccsds_validator.cpp
    obc/ccsds_tm_validator.cpp
)
target_include_directories(xdos_obc PRIVATE
    ${CMAKE_SOURCE_DIR}/obc
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(xdos_obc PRIVATE xdos_core)

# Ground-side LUT compiler: rate-group command CSV → binary LUT + GS twin
add_executable(xdos_lut_csv
    tools/rate_lut_from_csv.cpp
    src/lut/binary_lut_builder.cpp
    src/lut/rate_group_csv.cpp)
target_include_directories(xdos_lut_csv PRIVATE
    ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(xdos_lut_csv PRIVATE xdos_core)

# ── unit tests (Catch2 v3) ────────────────────────────────────────────────────
enable_testing()
find_package(Catch2 3 QUIET)
if(Catch2_FOUND)
    add_executable(xdos_tests
        tests/test_crc.cpp
        tests/test_hex.cpp
        tests/test_mib_load.cpp
        tests/test_codec.cpp
        tests/test_ccsds_codec.cpp
        tests/test_ccsds_validator.cpp
        tests/test_payload.cpp
        tests/test_flight_realism.cpp
        tests/test_eps_framing.cpp
        tests/test_eps_raw_tm.cpp
        tests/test_adcs_framing.cpp
        tests/test_xlink_codec.cpp
        tests/test_xlink_keys.cpp
        tests/test_gps_framing.cpp
        tests/test_uhf.cpp
        tests/test_sensor.cpp
        tests/test_scale.cpp
        tests/test_e2e.cpp
        tests/test_power_sequence.cpp
        tests/test_ccsds_tm_codec.cpp
        tests/test_rate_group.cpp
        tests/test_binary_lut.cpp
        tests/test_binary_rate_lut.cpp
        tests/test_rate_group_csv.cpp
        src/lut/binary_lut_builder.cpp
        src/lut/rate_group_csv.cpp
        ${XDOS_SIM_SHARED}
        obc/bridge.cpp
        obc/power_fsm.cpp
            obc/rate_group_engine.cpp
        src/cli/master_frame_store.cpp
        src/cli/value_format.cpp
        src/cli/bin_lut_twin.cpp
        obc/binary_rate_lut.cpp
        obc/ccsds_validator.cpp
    obc/ccsds_tm_validator.cpp
    )
    target_include_directories(xdos_tests PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/simulator
        ${CMAKE_SOURCE_DIR}/obc
    )
    target_link_libraries(xdos_tests PRIVATE xdos_core Catch2::Catch2WithMain)
    add_test(NAME unit COMMAND xdos_tests)
    message(STATUS "  tests target      : enabled (Catch2 v${Catch2_VERSION})")
else()
    message(STATUS "  tests target      : DISABLED  (install 'catch2' to enable)")
endif()

# ── install ──────────────────────────────────────────────────────────────────
install(TARGETS xdos_gs xdos_eps_sim xdos_adcs_sim xdos_gps_sim xdos_payload_sim
        xdos_uhf_sim xdos_sensor_sim xdos_obc
        DESTINATION bin)
install(DIRECTORY mib DESTINATION share/xdos)

message(STATUS "")
message(STATUS "xdos_groundstation ${PROJECT_VERSION}")
message(STATUS "  build type        : ${CMAKE_BUILD_TYPE}")
message(STATUS "  c++ standard      : ${CMAKE_CXX_STANDARD}")
message(STATUS "  compiler          : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "")
