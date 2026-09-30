#!/usr/bin/env bash
# run_cli.sh — start the terminal ground station.
set -u
cd "$(dirname "$0")/.."
source scripts/build_dir.sh
exec "./$XDOS_BUILD/xdos_gs" --obc-host 127.0.0.1 --obc-port 10000 "$@"
