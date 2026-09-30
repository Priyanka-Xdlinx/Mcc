#!/usr/bin/env bash
# run_sim.sh — start the bus simulator (one TCP server per subsystem).
set -u
cd "$(dirname "$0")/.."
exec ./build/xdos_sim "$@"
