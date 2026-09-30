#!/usr/bin/env python3
"""
mcc.py - build, start and stop mcc-app on Windows, macOS and Linux.

The same steps as start.sh / stop.sh, in Python so it runs the same way on
every OS (standard library only).

    python mcc.py start           app only; press "Start feed" in the UI
    python mcc.py start --sim     also start the telemetry feed
    python mcc.py start --build   rebuild schema, web UI and backend first
    python mcc.py start --fg      run the backend in this window (Ctrl-C stops it)
    python mcc.py stop            stop the feed and the app
    python mcc.py stop --sim      stop the feed only
    python mcc.py status          show what is running

The telemetry feed started by mcc.py is always the recorded XMCC feed
(feed/xmcc_replay.py), so no C++ build is needed. start.sh / stop.sh are for
the live XDLINX test stack (macOS / Linux, built with xdos/scripts/build_mac.sh).
"""
import argparse
import json
import os
import shutil
import signal
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent
DB = ROOT / "mcc.db"
XDOS = ROOT / "xdos"
JAR = ROOT / "backend" / "target" / "mcc-backend-1.0.0.jar"
STATIC = ROOT / "backend" / "src" / "main" / "resources" / "static"
PIDFILE = ROOT / ".backend.pid"
LOG = ROOT / "backend.log"
PORT = 8080
URL = f"http://localhost:{PORT}"
WINDOWS = os.name == "nt"


def say(msg: str) -> None:
    print(f"==> {msg}", flush=True)


def die(msg: str) -> None:
    print(f"!!  {msg}", file=sys.stderr, flush=True)
    sys.exit(1)


def tool(name: str, hint: str) -> str:
    """Find a command; on Windows this also finds name.cmd / name.exe."""
    found = shutil.which(name)
    if not found:
        die(f"{name} not found - {hint}")
    return found


def run(cmd: list[str], cwd: Path) -> None:
    say(" ".join(Path(cmd[0]).name if i == 0 else c for i, c in enumerate(cmd)) + f"   (in {cwd.relative_to(ROOT) if cwd != ROOT else '.'})")
    if subprocess.call(cmd, cwd=cwd) != 0:
        die(f"failed: {' '.join(cmd)}")


def api(path: str, method: str = "GET", timeout: float = 5) -> dict | None:
    try:
        req = urllib.request.Request(URL + path, method=method)
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read() or b"{}")
    except Exception:
        return None


def java_exe() -> str:
    home = os.environ.get("JAVA_HOME")
    if home:
        exe = Path(home) / "bin" / ("java.exe" if WINDOWS else "java")
        if exe.exists():
            return str(exe)
    return tool("java", "install a JDK 17 or later")


def pid_alive(pid: int) -> bool:
    if WINDOWS:
        out = subprocess.run(["tasklist", "/FI", f"PID eq {pid}", "/NH"], capture_output=True, text=True).stdout
        return str(pid) in out
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def read_pid() -> int | None:
    try:
        return int(PIDFILE.read_text().strip())
    except Exception:
        return None


def build(force: bool) -> None:
    if force or not DB.exists():
        say("building the decode schema (mcc.db) from xdos/mib")
        run([sys.executable, "build_schema.py"], ROOT / "schema")
    if force or not (STATIC / "index.html").exists():
        npm = tool("npm", "install Node.js 22")
        web = ROOT / "frontend"
        if not (web / "node_modules").exists():
            run([npm, "install"], web)
        run([npm, "run", "build"], web)
        say("copying the web UI into the backend")
        shutil.rmtree(STATIC, ignore_errors=True)
        shutil.copytree(web / "dist", STATIC)
    if force or not JAR.exists():
        mvn = tool("mvn", "install Maven 3.9")
        run([mvn, "-B", "-DskipTests", "package"], ROOT / "backend")


def start(args: argparse.Namespace) -> None:
    pid = read_pid()
    if pid and pid_alive(pid):
        say(f"backend already running (pid {pid})")
        if not args.sim:
            return
    else:
        PIDFILE.unlink(missing_ok=True)
        build(args.build)

        # mcc.py is the launcher for developers without the C++ test stack: its feed is the
        # recording (feed/xmcc_replay.py). start.sh keeps using the native stack.
        env = dict(os.environ, MCC_DB=str(DB), MCC_XDOS_HOME=str(XDOS), MCC_FEED="replay")
        cmd = [java_exe(), "-jar", str(JAR)]
        if args.fg:
            say("starting the backend in this window - Ctrl-C to stop")
            sys.exit(subprocess.call(cmd, env=env, cwd=ROOT))
        say("starting the backend")
        log = open(LOG, "w")
        kw = {"creationflags": subprocess.CREATE_NEW_PROCESS_GROUP | subprocess.DETACHED_PROCESS} if WINDOWS \
            else {"start_new_session": True}
        proc = subprocess.Popen(cmd, env=env, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, **kw)
        PIDFILE.write_text(str(proc.pid))
        say(f"waiting for {URL}/ ...")
        for _ in range(90):
            if api("/api/status"):
                break
            if proc.poll() is not None:
                print(LOG.read_text(errors="replace")[-3000:])
                PIDFILE.unlink(missing_ok=True)
                die("backend exited during start-up - see the log above")
            time.sleep(1)
        else:
            die(f"backend did not become ready - see {LOG.name}")
        say(f"backend up (pid {proc.pid})  log: {LOG.name}")

    if args.sim:
        say("starting the telemetry feed")
        st = api("/api/sim/start", "POST", timeout=15) or {}
        if st.get("error"):
            die(f"feed failed to start: {st['error']}")
        say(f"feed: {st.get('mode') or 'starting'}")
        for _ in range(60):
            s = api("/api/status") or {}
            n = (s.get("feed") or {}).get("subframesReceived", 0)
            if n:
                say(f"telemetry flowing ({n} subframes)")
                break
            time.sleep(1)
        else:
            say("no telemetry yet - watch the UI")

    print(f"\n    MCC dashboard:  {URL}/")
    if not args.sim:
        print("    press 'Start feed' in the UI, or run:  python mcc.py start --sim")
    print("    stop with:      python mcc.py stop\n")


def stop(args: argparse.Namespace) -> None:
    say("stopping the telemetry feed")
    if api("/api/sim/stop", "POST", timeout=15) is None:
        say("backend not reachable - stopping the feed by its pid")
        feed = ROOT / ".feed.pid"
        try:
            fpid = int(feed.read_text().strip())
            kill(fpid)
        except Exception:
            pass
        feed.unlink(missing_ok=True)
    if args.sim:
        return
    pid = read_pid()
    if pid and pid_alive(pid):
        say(f"stopping the backend (pid {pid})")
        kill(pid)
        for _ in range(20):
            if not pid_alive(pid):
                break
            time.sleep(0.5)
    PIDFILE.unlink(missing_ok=True)
    say("all stopped")


def kill(pid: int) -> None:
    if WINDOWS:
        subprocess.run(["taskkill", "/PID", str(pid), "/T", "/F"], capture_output=True)
    else:
        try:
            os.kill(pid, signal.SIGTERM)
        except OSError:
            pass


def status(_: argparse.Namespace) -> None:
    pid = read_pid()
    print(f"backend: {'running (pid %d)' % pid if pid and pid_alive(pid) else 'stopped'}")
    s = api("/api/status")
    if s:
        f, sim = s.get("feed", {}), s.get("simulator", {})
        print(f"feed:    {'running (' + str(sim.get('mode')) + ')' if sim.get('running') else 'stopped'}"
              f"; connected {f.get('connected')}; subframes {f.get('subframesReceived')}; LUT CRC-16 {f.get('lutCrc16')}")


def main() -> None:
    ap = argparse.ArgumentParser(description="Build, start and stop mcc-app (any OS).")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("start", help="build what is missing and start the app")
    p.add_argument("--sim", action="store_true", help="also start the telemetry feed")
    p.add_argument("--build", action="store_true", help="rebuild schema, web UI and backend")
    p.add_argument("--fg", action="store_true", help="run the backend in this window")
    p.set_defaults(fn=start)
    p = sub.add_parser("stop", help="stop the feed and the app")
    p.add_argument("--sim", action="store_true", help="stop the feed only")
    p.set_defaults(fn=stop)
    sub.add_parser("status", help="show what is running").set_defaults(fn=status)
    a = ap.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
