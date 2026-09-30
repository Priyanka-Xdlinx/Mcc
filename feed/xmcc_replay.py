#!/usr/bin/env python3
"""
xmcc_replay.py - serve recorded XMCC telemetry on TCP 9100, so mcc-app runs
without building the XDLINX core team's C++ test stack.

The capture (testdata/feed_capture.xmcc) is real XMCC v1 output of that stack:
whole master frames of 16 subframes, 26-byte header + payload each. This server
sends one subframe per second to every connected client, exactly as the stack
does. Only two header fields are rewritten so the feed looks live:

  * master frame number: keeps counting up across loops (wraps at 4096 like
    the OBC), so the app never mistakes a loop for a reboot;
  * spacecraft time: the current UTC time.

Payload bytes are untouched, so every value decodes exactly as recorded, and
the downlink map (LUT CRC-16 45038) matches xdos/config.

Standard library only. Usage:
    python3 feed/xmcc_replay.py [--capture FILE] [--host 127.0.0.1] [--port 9100]
"""
import argparse
import datetime as dt
import socket
import struct
import sys
import threading
import time
from pathlib import Path

HEADER = 26


def load(path: Path) -> list[bytes]:
    """Split a capture into subframes, checking every header."""
    data = path.read_bytes()
    out, i = [], 0
    while i < len(data):
        h = data[i:i + HEADER]
        if len(h) < HEADER or h[:4] != b"XMCC" or h[4] != 1:
            raise SystemExit(f"{path}: not an XMCC v1 capture at byte {i}")
        length = struct.unpack(">I", h[22:26])[0]
        out.append(data[i:i + HEADER + length])
        i += HEADER + length
    if not out or struct.unpack(">H", out[0][10:12])[0] != 1 or len(out) % 16:
        raise SystemExit(f"{path}: capture must hold whole master frames starting at subframe 1")
    return out


def restamp(frame: bytes, master_frame: int, now: dt.datetime) -> bytes:
    h = bytearray(frame[:HEADER])
    struct.pack_into(">I", h, 6, master_frame)
    struct.pack_into(">HBBBBBH", h, 12, now.year, now.month, now.day,
                     now.hour, now.minute, now.second, now.microsecond // 1000)
    return bytes(h) + frame[HEADER:]


def main() -> int:
    here = Path(__file__).resolve().parent
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--capture", default=str(here.parent / "testdata" / "feed_capture.xmcc"))
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9100)
    ap.add_argument("--period", type=float, default=1.0, help="seconds between subframes")
    args = ap.parse_args()

    frames = load(Path(args.capture))
    clients: list[socket.socket] = []
    lock = threading.Lock()

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((args.host, args.port))
    srv.listen()
    print(f"xmcc_replay: {len(frames)} subframes ({len(frames) // 16} master frames) "
          f"from {args.capture}; serving XMCC on {args.host}:{args.port}", flush=True)

    def accept() -> None:
        while True:
            c, addr = srv.accept()
            c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            with lock:
                clients.append(c)
            print(f"xmcc_replay: client {addr[0]}:{addr[1]} connected", flush=True)

    threading.Thread(target=accept, daemon=True).start()

    master_frame, i, next_at = 1, 0, time.monotonic()
    while True:
        frame = restamp(frames[i], master_frame, dt.datetime.now(dt.timezone.utc))
        with lock:
            for c in clients[:]:
                try:
                    c.sendall(frame)
                except OSError:
                    clients.remove(c)
                    c.close()
        i = (i + 1) % len(frames)
        if i % 16 == 0:                         # next master frame
            master_frame = (master_frame + 1) % 4096
        next_at += args.period
        time.sleep(max(0.0, next_at - time.monotonic()))


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(0)
