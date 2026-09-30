#!/usr/bin/env bash
# start.sh - bring up the MCC app.
#
#   ./start.sh              backend only; start the simulator from the UI
#   ./start.sh --sim        also start the XDOS simulator stack
#   ./start.sh --build      rebuild schema, frontend and backend first
#   ./start.sh --fg         run the backend in the foreground (Ctrl-C stops it)
#
# Anything missing is built on demand, so a clean checkout needs no extra steps.
# Written for macOS's bash 3.2: no ${v^^}, no associative arrays, no mapfile.

set -u

ROOT="$(cd "$(dirname "$0")" && pwd)"
XDOS="$ROOT/xdos"
DB="$ROOT/mcc.db"
JAR="$ROOT/backend/target/mcc-backend-1.0.0.jar"
PIDFILE="$ROOT/.backend.pid"
LOG="$ROOT/backend.log"
PORT=8080

WITH_SIM=0
FORCE_BUILD=0
FOREGROUND=0

while [ $# -gt 0 ]; do
    case "$1" in
        --sim)    WITH_SIM=1;    shift ;;
        --build)  FORCE_BUILD=1; shift ;;
        --fg)     FOREGROUND=1;  shift ;;
        -h|--help)
            sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

say() { printf '==> %s\n' "$*"; }
die() { printf '!!  %s\n' "$*" >&2; exit 1; }

# ── toolchain ────────────────────────────────────────────────────────────────
# /usr/bin/java works, but JAVA_HOME is usually unset and Maven wants it.
if [ -z "${JAVA_HOME:-}" ]; then
    if [ -x /usr/libexec/java_home ]; then                       # macOS
        JAVA_HOME="$(/usr/libexec/java_home 2>/dev/null)"
    elif command -v java >/dev/null 2>&1; then                   # Linux: follow java on PATH
        JAVA_HOME="$(dirname "$(dirname "$(readlink -f "$(command -v java)")")")"
    fi
    export JAVA_HOME
fi
[ -n "${JAVA_HOME:-}" ] || die "no JDK found - install one, or set JAVA_HOME"
JAVA="$JAVA_HOME/bin/java"

# The system python3 is a licence-blocked shim on this machine; prefer Homebrew's.
PY=""
for cand in /opt/homebrew/bin/python3 /usr/local/bin/python3 python3; do
    if "$cand" -c 'import sqlite3' >/dev/null 2>&1; then PY="$cand"; break; fi
done
[ -n "$PY" ] || die "no working python3 found (needed to build the decode schema)"

# ── already running? ─────────────────────────────────────────────────────────
if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
    say "backend already running (pid $(cat "$PIDFILE")) - http://localhost:$PORT/"
    [ "$WITH_SIM" -eq 1 ] || exit 0
else
    rm -f "$PIDFILE"
fi

# ── 1. decode schema ─────────────────────────────────────────────────────────
if [ "$FORCE_BUILD" -eq 1 ] || [ ! -f "$DB" ]; then
    say "building the decode schema from the MIB"
    ( cd "$ROOT/schema" && "$PY" build_schema.py ) || die "schema build failed"
else
    say "decode schema present ($(basename "$DB"))"
fi

# ── 2. frontend ──────────────────────────────────────────────────────────────
STATIC="$ROOT/backend/src/main/resources/static"
if [ "$FORCE_BUILD" -eq 1 ] || [ ! -f "$STATIC/index.html" ]; then
    say "building the frontend"
    ( cd "$ROOT/frontend" \
      && { [ -d node_modules ] || npm install; } \
      && npm run build ) || die "frontend build failed"
    rm -rf "$STATIC"
    mkdir -p "$STATIC"
    cp -r "$ROOT/frontend/dist/"* "$STATIC/" || die "could not stage the frontend"
else
    say "frontend already staged"
fi

# ── 3. backend ───────────────────────────────────────────────────────────────
if [ "$FORCE_BUILD" -eq 1 ] || [ ! -f "$JAR" ]; then
    say "building the backend (this downloads dependencies on first run)"
    ( cd "$ROOT/backend" && mvn -B -DskipTests package ) || die "backend build failed"
else
    say "backend jar present"
fi

# ── 4. run ───────────────────────────────────────────────────────────────────
export MCC_DB="$DB"
export MCC_XDOS_HOME="$XDOS"

if [ "$FOREGROUND" -eq 1 ]; then
    say "starting the backend in the foreground - Ctrl-C to stop"
    exec "$JAVA" -jar "$JAR"
fi

say "starting the backend"
: > "$LOG"
nohup "$JAVA" -jar "$JAR" >> "$LOG" 2>&1 &
echo $! > "$PIDFILE"

# Wait on the health endpoint rather than sleeping a fixed amount: the first
# start is slower because the schema has to load.
say "waiting for http://localhost:$PORT/ ..."
i=0
while [ $i -lt 90 ]; do
    if curl -fsS "http://localhost:$PORT/api/status" >/dev/null 2>&1; then
        break
    fi
    if ! kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
        echo
        tail -20 "$LOG" >&2
        rm -f "$PIDFILE"
        die "backend exited during startup - see $LOG"
    fi
    i=$((i + 1))
    sleep 1
done
[ $i -lt 90 ] || die "backend did not become ready - see $LOG"

say "backend up (pid $(cat "$PIDFILE"))  log: ${LOG#$ROOT/}"

# ── 5. optional: the simulator ───────────────────────────────────────────────
if [ "$WITH_SIM" -eq 1 ]; then
    # Same rule as xdos/scripts/build_dir.sh: build-mac/ on macOS, build/ elsewhere.
    if [ -z "${XDOS_BUILD:-}" ]; then
        if [ "$(uname -s)" = Darwin ]; then XDOS_BUILD=build-mac; else XDOS_BUILD=build; fi
    fi
    if [ ! -x "$XDOS/$XDOS_BUILD/xdos_gs" ]; then
        if [ "$XDOS_BUILD" = build-mac ]; then
            die "no native XDOS build - run:  xdos/scripts/build_mac.sh"
        else
            die "no native XDOS build - run:  cmake -S xdos -B xdos/$XDOS_BUILD && cmake --build xdos/$XDOS_BUILD -j"
        fi
    fi
    say "starting the XDOS simulator"
    SIMERR=$(curl -fsS -X POST "http://localhost:$PORT/api/sim/start" 2>/dev/null \
        | "$PY" -c 'import sys,json; print(json.load(sys.stdin).get("error") or "")' 2>/dev/null) \
        || die "simulator failed to start - check the UI for the reason"
    [ -z "$SIMERR" ] || die "simulator failed to start: $SIMERR"
    # The OBC boots unpowered and takes ~10 s after the kill switch is released.
    say "waiting for telemetry ..."
    i=0
    while [ $i -lt 60 ]; do
        n=$(curl -fsS "http://localhost:$PORT/api/status" 2>/dev/null \
            | "$PY" -c 'import sys,json; print(json.load(sys.stdin)["feed"]["subframesReceived"])' 2>/dev/null)
        [ -n "${n:-}" ] && [ "$n" -gt 0 ] && break
        i=$((i + 1))
        sleep 1
    done
    if [ -n "${n:-}" ] && [ "$n" -gt 0 ]; then
        say "telemetry flowing ($n subframes)"
    else
        say "no telemetry yet - the OBC may still be booting; watch the UI"
    fi
fi

echo
echo "    MCC dashboard:  http://localhost:$PORT/"
[ "$WITH_SIM" -eq 1 ] || echo "    press 'Start feed' in the UI, or re-run with --sim"
echo "    stop with:      $ROOT/stop.sh"
echo
