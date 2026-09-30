#!/usr/bin/env bash
# run_tests.sh — configure (if needed), build, run unit + e2e tests.
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/build_dir.sh
if [[ "$(uname -s)" == Darwin ]]; then
  scripts/build_mac.sh
else
  if [[ ! -d build ]]; then
    cmake -B build -DCMAKE_BUILD_TYPE=Release
  fi
  cmake --build build -j
fi
"./$XDOS_BUILD/xdos_tests" "$@"
