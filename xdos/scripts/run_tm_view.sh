#!/usr/bin/env bash
# Master Frame telemetry viewer — run this in its OWN terminal, alongside the
# telecommanding CLI (run_cli.sh / xdos_gs).  Defaults here target the local
# simulator stack; bare ./build/xdos_tm_view targets the bench OBC.
cd "$(dirname "$0")/.."
source scripts/build_dir.sh
exec "./$XDOS_BUILD/xdos_tm_view" --obc-host 127.0.0.1 --obc-port 10000 "$@"
