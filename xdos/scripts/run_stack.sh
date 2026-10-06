#!/usr/bin/env bash
# run_stack.sh — one command to bring up the whole XDOS stack:
#
#   6 bus simulators  →  OBC  →  xdos_tm_view (+ MCC subframe feed)  →  xdos_gs
#
# The two interactive terminals are separate by design: xdos_gs is the
# telecommanding CLI, xdos_tm_view is the master-frame telemetry view that also
# forwards every subframe payload to the MCC software over TCP.  tm_view opens
# in its own window; xdos_gs runs in this terminal.  Ctrl-C here stops
# everything.
#
# Usage:
#   scripts/run_stack.sh                      # full stack, MCC feed on :9100
#   scripts/run_stack.sh --power-on           # also release the EPS kill switch
#   scripts/run_stack.sh --mcc-connect H:P    # dial out to a listening MCC
#   scripts/run_stack.sh --no-mcc             # no MCC feed
#   scripts/run_stack.sh --no-gs              # headless: no xdos_gs window
#   scripts/run_stack.sh --no-windows         # tm_view in background, log only
#   scripts/run_stack.sh --help
#
# Logs land in ./logs/.  The subframe log RateGroup_TM_<date>.log is written to
# the repo root by tm_view.

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/scripts/build_dir.sh"
BIN="$ROOT/$XDOS_BUILD"
LOG="$ROOT/logs"
cd "$ROOT"

# ── defaults ─────────────────────────────────────────────────────────────────
OBC_HOST=127.0.0.1
OBC_PORT=10000
MCC_MODE=listen                 # listen | connect | off
MCC_ENDPOINT=127.0.0.1:9100
USE_OBC=1
USE_GS=1
USE_WINDOWS=1
POWER_ON=0

usage() {
    # Print the header comment block and stop at the first line of code.
    awk 'NR>1 { if ($0 !~ /^#/) exit; sub(/^# ?/, ""); print }' "$0"
    exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --mcc-listen)  MCC_MODE=listen;  MCC_ENDPOINT="$2"; shift 2 ;;
        --mcc-connect) MCC_MODE=connect; MCC_ENDPOINT="$2"; shift 2 ;;
        --no-mcc)      MCC_MODE=off;     shift ;;
        --obc-host)    OBC_HOST="$2";    shift 2 ;;
        --obc-port)    OBC_PORT="$2";    shift 2 ;;
        --no-obc)      USE_OBC=0;        shift ;;
        --no-gs)       USE_GS=0;         shift ;;
        --no-windows)  USE_WINDOWS=0;    shift ;;
        --power-on)    POWER_ON=1;       shift ;;
        -h|--help)     usage 0 ;;
        *) echo "unknown option: $1" >&2; usage 1 ;;
    esac
done

RG_PORT=$(( OBC_PORT + 2 ))     # OBC rate-group stream
EPS_PORT=10010                  # EGSE umbilical lives on the EPS sim

if [[ ! -x "$BIN/xdos_gs" ]]; then
    echo "build first:  cmake --build $XDOS_BUILD -j   (macOS: scripts/build_mac.sh)" >&2
    exit 1
fi
mkdir -p "$LOG"

# Match only OUR binaries — a loose pattern like 'xdos_' also matches the
# shell running this script, which would make the script kill itself.
KILLPAT='build[^/]*/xdos_(eps|adcs|gps|payload|xlink|uhf|sensor)_sim|build[^/]*/xdos_obc|build[^/]*/xdos_tm_view'

PIDS=()
cleanup() {
    echo
    echo "==> stopping the stack"
    for p in "${PIDS[@]:-}"; do kill -INT  "$p" 2>/dev/null || true; done
    sleep 0.4
    for p in "${PIDS[@]:-}"; do kill -KILL "$p" 2>/dev/null || true; done
    pkill -f "$KILLPAT" 2>/dev/null || true
    exit 0
}
trap cleanup INT TERM

# ── helpers ──────────────────────────────────────────────────────────────────

# spawn <label> <logfile> <cmd...> — start in background, verify it came up.
spawn() {
    local label="$1"; shift
    local logf="$1";  shift
    if ! : > "$logf" 2>/dev/null; then
        echo "    !! cannot write $logf" >&2; cleanup
    fi
    "$@" > "$logf" 2>&1 < <(exec tail -f /dev/null) &
    local pid=$!
    PIDS+=("$pid")
    sleep 0.2
    if ! kill -0 "$pid" 2>/dev/null; then
        echo "    !! $label died on startup — see $logf" >&2
        tail -5 "$logf" | sed 's/^/    /' >&2
        cleanup
    fi
    printf "    %-28s log: %s\n" "$label" "${logf#$ROOT/}"
}

# wait_port <host> <port> <seconds> — block until something is listening.
wait_port() {
    local host="$1" port="$2" secs="$3" i=0
    while (( i < secs * 4 )); do
        if (exec 3<>"/dev/tcp/$host/$port") 2>/dev/null; then exec 3<&- 3>&-; return 0; fi
        sleep 0.25; i=$(( i + 1 ))
    done
    return 1
}

# open_window <title> <command...> — run in its own terminal window.
# Returns non-zero when there is no GUI terminal to use.
open_window() {
    local title="$1"; shift
    local cmd="$*"
    [[ "$(uname -s)" == Darwin || -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]] || return 1
    local inner="cd '$ROOT' && $cmd; echo; echo '[window closed — press enter]'; read -r"
    if [[ "$(uname -s)" == Darwin ]] && command -v osascript >/dev/null 2>&1; then
        local as=${inner//\\/\\\\}; as=${as//\"/\\\"}
        osascript -e "tell application \"Terminal\" to do script \"$as\"" \
            >/dev/null 2>&1 && return 0
        return 1
    fi
    if   command -v gnome-terminal  >/dev/null 2>&1; then
        gnome-terminal --title="$title" -- bash -lc "$inner" >/dev/null 2>&1 && return 0
    elif command -v xfce4-terminal  >/dev/null 2>&1; then
        xfce4-terminal --title="$title" -e "bash -lc \"$inner\"" >/dev/null 2>&1 && return 0
    elif command -v konsole         >/dev/null 2>&1; then
        konsole -p tabtitle="$title" -e bash -lc "$inner" >/dev/null 2>&1 && return 0
    elif command -v xterm           >/dev/null 2>&1; then
        xterm -T "$title" -e bash -lc "$inner" >/dev/null 2>&1 & return 0
    elif command -v x-terminal-emulator >/dev/null 2>&1; then
        x-terminal-emulator -e bash -lc "$inner" >/dev/null 2>&1 & return 0
    fi
    return 1
}

# ── clean slate ──────────────────────────────────────────────────────────────
if pgrep -f "$KILLPAT" >/dev/null 2>&1; then
    echo "==> clearing leftover XDOS processes"
    pkill -9 -f "$KILLPAT" 2>/dev/null || true
    sleep 0.5
fi

# ── 1. bus simulators ────────────────────────────────────────────────────────
echo "==> bus simulators"
for s in eps adcs gps payload xlink uhf; do
    spawn "$(printf %s "$s" | tr a-z A-Z) sim" "$LOG/$s.log" "$BIN/xdos_${s}_sim" --quiet --no-color
done
wait_port 127.0.0.1 "$EPS_PORT" 20 || { echo "    !! EPS sim never listened on $EPS_PORT" >&2; cleanup; }

# ── 2. OBC ───────────────────────────────────────────────────────────────────
if (( USE_OBC )); then
    echo "==> OBC bridge"
    spawn "OBC $OBC_HOST:$OBC_PORT" "$LOG/obc.log" "$BIN/xdos_obc" --no-color
    wait_port "$OBC_HOST" "$RG_PORT" 20 \
        || { echo "    !! OBC never listened on $RG_PORT" >&2; cleanup; }

    # The OBC boots UNPOWERED and waits for the EPS kill switch.  Without it
    # the rate-group engine never polls, so nothing reaches tm_view or MCC.
    if (( POWER_ON )); then
        echo "==> releasing the EPS kill switch (EGSE umbilical)"
        # XGSE frame: 'X','G','S','E', cmd 0x02 (KILL_SET), len 1, on=1
        if printf '\x58\x47\x53\x45\x02\x01\x01' > "/dev/tcp/127.0.0.1/$EPS_PORT" 2>/dev/null; then
            echo "    kill switch ON — OBC boots (10 s), then staggers channels"
        else
            echo "    !! could not reach the EPS EGSE port $EPS_PORT" >&2
        fi
    else
        echo "    note: OBC starts UNPOWERED.  Release the kill switch from the"
        echo "          xdos_gs power menu, or re-run with --power-on."
    fi
fi

# ── 3. tm_view + the MCC subframe feed ───────────────────────────────────────
TMV_ARGS=(--obc-host "$OBC_HOST" --obc-port "$OBC_PORT")
case "$MCC_MODE" in
    listen)  TMV_ARGS+=(--mcc-listen  "$MCC_ENDPOINT")
             echo "==> TM view + MCC feed — MCC connects to $MCC_ENDPOINT" ;;
    connect) TMV_ARGS+=(--mcc-connect "$MCC_ENDPOINT")
             echo "==> TM view + MCC feed — dialling out to $MCC_ENDPOINT" ;;
    off)     echo "==> TM view (MCC feed disabled)" ;;
esac

if (( USE_WINDOWS )) && open_window "XDOS TM View" "./$XDOS_BUILD/xdos_tm_view ${TMV_ARGS[*]}"; then
    echo "    opened in its own window"
else
    (( USE_WINDOWS )) && echo "    no GUI terminal — running headless (logging + MCC feed only)"
    # No TTY: the menu loop just idles, while ingest, logging and the MCC
    # forward all keep running.
    spawn "tm_view (headless)" "$LOG/tm_view.log" \
          "$BIN/xdos_tm_view" "${TMV_ARGS[@]}" --no-color < /dev/null
fi

# ── 4. xdos_gs — foreground, so Ctrl-C here tears the stack down ─────────────
if (( USE_GS )); then
    echo "==> xdos_gs (this terminal · Ctrl-C stops everything)"
    echo
    if (( USE_OBC )); then
        "$BIN/xdos_gs" --obc-host "$OBC_HOST" --obc-port "$OBC_PORT"
    else
        "$BIN/xdos_gs" --no-obc
    fi
    cleanup
else
    echo "==> running headless (no xdos_gs).  Ctrl-C to stop."
    echo
    # `wait` on a BACKGROUNDED sleep, not a foreground one: bash defers a trap
    # until the current foreground command finishes, so a plain `sleep 3600`
    # would swallow Ctrl-C for up to an hour.
    while true; do sleep 3600 & wait $!; done
fi
