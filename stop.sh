#!/usr/bin/env bash
# stop.sh - shut down the MCC app and the XDOS simulator stack.
#
#   ./stop.sh           stop the simulator and the backend
#   ./stop.sh --sim     stop only the simulator, leave the backend up
#
# Deliberately does NOT call xdos/scripts/kill_all.sh: that script
# misses xdos_tm_view and xdos_sensor_sim while printing "all XDOS processes
# terminated", and since the headless-EOF fix a stray tm_view waits on a signal
# forever. The pattern below is run_stack.sh's own, which does cover them.

set -u

ROOT="$(cd "$(dirname "$0")" && pwd)"
PIDFILE="$ROOT/.backend.pid"
PORT=8080

# Matches only the simulator binaries. A looser pattern such as "xdos_" also
# matches the shell running this script, which would make it kill itself.
KILLPAT='build[^/]*/xdos_(eps|adcs|gps|payload|xlink|uhf|sensor)_sim|build[^/]*/xdos_obc|build[^/]*/xdos_tm_view|feed/xmcc_replay\.py'

SIM_ONLY=0
while [ $# -gt 0 ]; do
    case "$1" in
        --sim) SIM_ONLY=1; shift ;;
        -h|--help)
            sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

say() { printf '==> %s\n' "$*"; }

# ── 1. simulator ─────────────────────────────────────────────────────────────
# Ask the backend first so it releases its own process handle cleanly; fall back
# to signals when the backend is already gone.
if curl -fsS -X POST "http://localhost:$PORT/api/sim/stop" >/dev/null 2>&1; then
    say "simulator stopped via the backend"
else
    say "backend not reachable - signalling the simulator directly"
fi

if pgrep -f "$KILLPAT" >/dev/null 2>&1; then
    pkill -INT -f "$KILLPAT" 2>/dev/null
    i=0
    while [ $i -lt 6 ] && pgrep -f "$KILLPAT" >/dev/null 2>&1; do
        i=$((i + 1)); sleep 0.5
    done
    pkill -KILL -f "$KILLPAT" 2>/dev/null
fi
# run_stack.sh itself, if it is still supervising.
pkill -f 'bash .*run_stack\.sh' 2>/dev/null

# ── 2. backend ───────────────────────────────────────────────────────────────
if [ "$SIM_ONLY" -eq 0 ]; then
    STOPPED=0
    if [ -f "$PIDFILE" ]; then
        PID="$(cat "$PIDFILE")"
        if kill -0 "$PID" 2>/dev/null; then
            kill "$PID" 2>/dev/null
            i=0
            while [ $i -lt 20 ] && kill -0 "$PID" 2>/dev/null; do
                i=$((i + 1)); sleep 0.5
            done
            kill -9 "$PID" 2>/dev/null
            STOPPED=1
        fi
        rm -f "$PIDFILE"
    fi
    # Catch a backend started by hand, without a pid file.
    if pgrep -f 'mcc-backend-1\.0\.0\.jar' >/dev/null 2>&1; then
        pkill -f 'mcc-backend-1\.0\.0\.jar' 2>/dev/null
        STOPPED=1
    fi
    [ "$STOPPED" -eq 1 ] && say "backend stopped" || say "backend was not running"
fi

# ── 3. prove it ──────────────────────────────────────────────────────────────
sleep 1
LEFT="$(pgrep -fl "$KILLPAT" 2>/dev/null)"
if [ -n "$LEFT" ]; then
    printf '!!  simulator processes still running:\n%s\n' "$LEFT" >&2
    exit 1
fi
if [ "$SIM_ONLY" -eq 0 ] && pgrep -f 'mcc-backend-1\.0\.0\.jar' >/dev/null 2>&1; then
    echo '!!  backend still running' >&2
    exit 1
fi

if [ "$SIM_ONLY" -eq 1 ]; then
    say "simulator stopped; backend still up on http://localhost:$PORT/"
else
    say "all stopped - no simulator, no backend"
fi
