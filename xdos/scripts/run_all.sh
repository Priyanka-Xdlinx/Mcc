#!/usr/bin/env bash
# Launch the full XDOS Level-1 stack: three sims + OBC + foreground GS.
# Logs go to ./logs/ in the project root (per-user, no /tmp collisions).
# Ctrl-C cleans up everything.
#
# Usage:
#   scripts/run_all.sh                  # via OBC (default)
#   scripts/run_all.sh --no-obc         # skip OBC; GS talks direct to sims

set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/scripts/build_dir.sh"
BIN="$ROOT/$XDOS_BUILD"
LOG="$ROOT/logs"

if [[ ! -x "$BIN/xdos_gs" ]]; then
    echo "build first: cmake --build $XDOS_BUILD -j   (macOS: scripts/build_mac.sh)" >&2
    exit 1
fi
mkdir -p "$LOG"

# Kill any leftover XDOS processes from a previous run so ports are free
# and there's no zombie OBC stealing the connections.
if pgrep -f 'xdos_(eps|adcs|gps|payload|xlink|uhf)_sim|xdos_obc' >/dev/null 2>&1; then
    echo "==> cleaning up leftover XDOS processes from a previous run"
    pkill -9 -f 'xdos_(eps|adcs|gps|payload|xlink|uhf)_sim|xdos_obc' 2>/dev/null || true
    sleep 0.4
fi

NO_OBC=0
EXTRA_GS_ARGS=()
for a in "$@"; do
    case "$a" in
        --no-obc) NO_OBC=1; EXTRA_GS_ARGS+=("--no-obc") ;;
        *)        EXTRA_GS_ARGS+=("$a") ;;
    esac
done

PIDS=()
cleanup() {
    echo
    echo "Stopping..."
    for p in "${PIDS[@]:-}"; do kill -INT  "$p" 2>/dev/null || true; done
    sleep 0.3
    for p in "${PIDS[@]:-}"; do kill -KILL "$p" 2>/dev/null || true; done
    exit 0
}
trap cleanup INT TERM

# Spawn a backgrounded process, verify it actually came up.
#   $1 = friendly label
#   $2 = log path
#   $3.. = command + args
spawn() {
    local label="$1"; shift
    local logf="$1"; shift
    echo "==> $label   log: $logf"
    # Try to open the log file for write; report a clear error if we can't.
    if ! : > "$logf" 2>/dev/null; then
        echo "    !! cannot write to $logf (permission denied or path bad)" >&2
        cleanup
        exit 1
    fi
    "$@" > "$logf" 2>&1 &
    local pid=$!
    PIDS+=("$pid")
    sleep 0.25
    if ! kill -0 "$pid" 2>/dev/null; then
        echo "    !! $label died on startup — see $logf" >&2
        tail -5 "$logf" | sed 's/^/    /' >&2
        cleanup
        exit 1
    fi
}

spawn "EPS sim     127.0.0.1:10010" "$LOG/eps.log"     "$BIN/xdos_eps_sim"     --quiet --no-color
spawn "ADCS sim    127.0.0.1:10020" "$LOG/adcs.log"    "$BIN/xdos_adcs_sim"    --quiet --no-color
spawn "GPS sim     127.0.0.1:10030" "$LOG/gps.log"     "$BIN/xdos_gps_sim"     --quiet --no-color
spawn "Payload sim 127.0.0.1:10040" "$LOG/payload.log" "$BIN/xdos_payload_sim" --quiet --no-color
spawn "XLINK-S sim 127.0.0.1:10050" "$LOG/xlink.log"   "$BIN/xdos_xlink_sim"   --quiet --no-color
spawn "UHF sim     127.0.0.1:10060" "$LOG/uhf.log"     "$BIN/xdos_uhf_sim"     --quiet --no-color
sleep 0.3

if (( NO_OBC == 0 )); then
    spawn "OBC bridge 127.0.0.1:10000" "$LOG/obc.log" "$BIN/xdos_obc" --no-color
    sleep 0.3
    echo
    echo "GS link mode: via OBC"
else
    echo
    echo "GS link mode: direct (no OBC)"
fi
echo

# Run GS in the foreground.  When it exits, cleanup() fires.
"$BIN/xdos_gs" --obc-host 127.0.0.1 --obc-port 10000 "${EXTRA_GS_ARGS[@]}"
cleanup
