#!/usr/bin/env python3
"""
mcc_decode.py — MCC-side reference decoder for the XDOS "XMCC" v1 subframe feed.

Turns the ground station's TCP feed (26-byte header + 1280-byte payload) into
named, engineering-unit telemetry.

    live     python3 mcc_decode.py --connect <gs-host>:9100
    live     python3 mcc_decode.py --listen 9100
    replay   python3 mcc_decode.py --file capture.xmcc
    verify   python3 mcc_decode.py --selftest logs/master_frame_decoded.txt

Decode semantics mirror the ground station's own codec (src/codec/codec.cpp)
field for field, so both sides render identical values.

Needs, from the GS:
  * config/rate_group_lut_bin_gs.json   the ground twin of the flown LUT
  * mib/*.json                          the mission database
Its CRC-16 MUST match the LUT the OBC is executing. Position inside a tier is
the only link between the two sides — a mismatched twin reads every value from
the wrong offset and still looks plausible.
"""

from __future__ import annotations

import argparse
import json
import socket
import struct
import sys
from pathlib import Path

MAGIC = b"XMCC"
HDR_LEN = 26
VERSION = 1

# Tier layout inside the 1280-byte subframe: (start, end, subframes per block)
TIERS = {
    "VH": (0, 400, 1),
    "H": (400, 800, 2),
    "M": (800, 1100, 4),
    "L": (1100, 1275, 8),
}


# ── mission database ────────────────────────────────────────────────────────


class Mib:
    """The mission database. Authoritative for offsets, types, scale and enums."""

    def __init__(self, mib_dir: Path):
        self.messages: dict[tuple[str, str], dict] = {}
        self.enums: dict[tuple[str, str], dict[int, str]] = {}
        for path in sorted(mib_dir.glob("*.json")):
            doc = json.loads(path.read_text())
            sub = doc.get("subsystem")
            if not sub:
                continue
            for en in doc.get("enums") or []:
                self.enums[(sub, en["name"])] = self._enum_map(en.get("values", []))
            for msg in doc.get("messages", []):
                msg["_offset_unit"] = doc.get("offset_unit") or msg.get("offset_unit")
                msg["_subsystem"] = sub
                self.messages[(sub, msg["name"])] = msg

    @staticmethod
    def _enum_map(values) -> dict[int, str]:
        out: dict[int, str] = {}
        items = values.get("values", values) if isinstance(values, dict) else values
        if isinstance(items, dict):
            for k, v in items.items():
                try:
                    out[int(k)] = v if isinstance(v, str) else v.get("name", str(v))
                except (TypeError, ValueError):
                    pass
        elif isinstance(items, list):
            for e in items:
                if isinstance(e, dict) and "value" in e:
                    out[int(e["value"])] = e.get("name", "")
        return out

    def message(self, subsystem: str, name: str) -> dict | None:
        return self.messages.get((subsystem, name))

    def enum_name(self, subsystem: str, enum_ref: str | None, value: int) -> str | None:
        if not enum_ref:
            return None
        return self.enums.get((subsystem, enum_ref), {}).get(value)


# ── field decoding — mirrors src/codec/codec.cpp ────────────────────────────


def _sign_extend(v: int, bits: int) -> int:
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def _get_int(raw: bytes, big_endian: bool) -> int:
    return int.from_bytes(raw, "big" if big_endian else "little")


def decode_byte_field(p: dict, raw: bytes, mib: Mib, subsystem: str):
    """Byte-addressed parameter (EPS, GPS, Payload, Sensor, UHF)."""
    t, be, scale = p["data_type"], p.get("endian") == "big", p.get("scale")

    if t in ("CHAR", "STRING"):
        return raw.split(b"\x00")[0].decode("ascii", "replace")
    if t in ("HEX", "RESERVED"):
        order = raw if be else raw[::-1]
        return "0x" + order.hex().upper()
    if t == "BOOL":
        return any(raw)
    if t == "ENUM":
        v = _get_int(raw, be)
        return {"value": v, "name": mib.enum_name(subsystem, p.get("enum"), v)}
    if t == "FLOAT" and len(raw) == 4:
        d = struct.unpack(">f" if be else "<f", raw)[0]
        return d * scale if scale else d
    if t == "DOUBLE" and len(raw) == 8:
        d = struct.unpack(">d" if be else "<d", raw)[0]
        return d * scale if scale else d

    u = _get_int(raw, be)
    if t == "INT":
        v = _sign_extend(u, len(raw) * 8)
        return v * scale if scale else v
    return u * scale if scale else u


def decode_bit_field(p: dict, raw: int, mib: Mib, subsystem: str):
    """Bit-packed parameter (ADCS, XLINK). `raw` is already LSB-first assembled."""
    t, bits, scale = p["data_type"], p["length"], p.get("scale")

    if t == "BOOL":
        return bool(raw & 1)
    if t == "ENUM":
        return {"value": raw, "name": mib.enum_name(subsystem, p.get("enum"), raw)}
    if t in ("CHAR", "STRING"):
        b = raw.to_bytes(max(bits // 8, 1), "little")
        return b.split(b"\x00")[0].decode("ascii", "replace")
    if t == "FLOAT" and bits == 32:
        d = struct.unpack("<f", (raw & 0xFFFFFFFF).to_bytes(4, "little"))[0]
        return d * scale if scale else d
    if t == "DOUBLE" and bits == 64:
        d = struct.unpack("<d", raw.to_bytes(8, "little"))[0]
        return d * scale if scale else d
    if t == "INT":
        v = _sign_extend(raw, bits)
        return v * scale if scale else v
    return raw * scale if scale else raw


# ── entry decoding ──────────────────────────────────────────────────────────


# Parameters the ground station's own decoder strips before applying offsets.
# See PREFIX NOTE at the bottom of this file — the LUT builder does NOT strip
# them, so the two halves of the GS disagree by exactly this many bits.
_STRIPPED_PREFIX = ("product_key", "service_event")


def _prefix_bits(msg: dict) -> int:
    """Bit length of the leading product_key/service_event prefix, if any."""
    total = 0
    for p in msg.get("parameters", []):
        if p["name"] in _STRIPPED_PREFIX and p.get("offset") == total:
            total += p["length"]
        else:
            break
    return total


class Decoder:
    def __init__(self, twin_path: Path, mib_dir: Path, compat: str = "gs"):
        self.twin = json.loads(Path(twin_path).read_text())
        self.mib = Mib(Path(mib_dir))
        self.crc16 = self.twin.get("crc16")
        self.compat = compat          # "gs" = match the GS display, "mib" = match the picks
        self.shifted: set[str] = set()  # entry ids where the two disagree

    def picked_from_block(self, entry: dict, block: bytes) -> bytes:
        """
        Gather an entry's bytes out of an assembled tier block. Each pick
        carries its own absolute `block` offset, so this never depends on
        entries being laid out contiguously or on pick array order --
        the same indexing the GS uses (src/cli/master_frame_store.cpp).
        """
        out = bytearray()
        for pk in entry["picks"]:
            b = pk["block"]
            out += block[b : b + pk["size"]].ljust(pk["size"], b"\x00")
        return bytes(out)

    def decode_entry(self, entry: dict, picked: bytes) -> list[tuple[str, object, dict]]:
        """
        `picked` is the entry's pick runs concatenated in pick order — exactly
        what the tier block holds. Scatter them back to their positions in the
        original response, then decode with MIB offsets.
        """
        subsystem = entry["subsystem"]
        msg = self.mib.message(subsystem, entry["rsp"])
        if msg is None:
            return []

        raw_len = entry["tm_raw_len"]
        buf = bytearray(raw_len)
        have = bytearray(raw_len)  # 1 where a pick supplied the byte
        cursor = 0
        wanted: list[str] = []
        for pk in entry["picks"]:
            run = picked[cursor : cursor + pk["size"]]
            cursor += pk["size"]
            start = pk["src"]
            buf[start : start + len(run)] = run
            have[start : start + len(run)] = b"\x01" * len(run)
            wanted.extend(pk["params"])

        by_name = {p["name"]: p for p in msg.get("parameters", [])}
        bit_mode = msg.get("_offset_unit") == "bits"
        shift = _prefix_bits(msg) if self.compat == "gs" else 0
        if _prefix_bits(msg):
            self.shifted.add(entry["id"])
        if not bit_mode:
            shift //= 8
        out = []
        for name in wanted:
            p = by_name.get(name)
            if p is None or p.get("offset") is None:
                out.append((name, None, {}))
                continue

            if bit_mode:
                lo, hi = p["offset"] - shift, p["offset"] + p["length"] - shift
                b0, b1 = lo // 8, (hi + 7) // 8
                unpicked = lo < 0 or b1 > raw_len or not all(have[b0:b1])
                if unpicked and (self.compat != "gs" or lo < 0 or b1 > raw_len):
                    out.append((name, None, {"missing": True}))
                    continue
                ff = all(x == 0xFF for x in buf[b0:b1])
                if ff and self.compat != "gs":
                    out.append((name, None, {"no_answer": True}))
                    continue
                v = 0
                for i in range(p["length"]):
                    bi = lo + i
                    if (buf[bi >> 3] >> (bi & 7)) & 1:
                        v |= 1 << i
                value = decode_bit_field(p, v, self.mib, subsystem)
            else:
                b0, b1 = p["offset"] - shift, p["offset"] + p["length"] - shift
                unpicked = b0 < 0 or b1 > raw_len or not all(have[b0:b1])
                if unpicked and (self.compat != "gs" or b0 < 0 or b1 > raw_len):
                    out.append((name, None, {"missing": True}))
                    continue
                ff = all(x == 0xFF for x in buf[b0:b1])
                if ff and self.compat != "gs":
                    out.append((name, None, {"no_answer": True}))
                    continue
                value = decode_byte_field(p, bytes(buf[b0:b1]), self.mib, subsystem)

            out.append((name, value, {"unit": p.get("unit"), "type": p["data_type"],
                                      "unpicked": unpicked, "ff": ff}))
        return out


def format_value(value, meta: dict) -> str:
    """Render the way the GS operator display does (src/cli/value_format.cpp)."""
    if meta.get("no_answer"):
        return "-- (no answer)"
    if meta.get("missing"):
        return "-- (block incomplete)"
    if value is None:
        return "--"
    unit = meta.get("unit")
    t = meta.get("type")
    if isinstance(value, dict):  # ENUM
        return f"{value['value']} ({value['name'] or '?'})"
    if t in ("FLOAT", "DOUBLE"):
        return f"{value:.3f}" + (f" {unit}" if unit else "")
    if isinstance(value, float):
        return f"{value:.2f}" + (f" {unit}" if unit else "")
    if isinstance(value, bool):
        return "true" if value else "false"
    return f"{value}" + (f" {unit}" if unit else "")


def format_value_marked(value, meta: dict) -> str:
    s = format_value(value, meta)
    if meta.get("unpicked"):
        return s + "   [NOT DOWNLINKED - zero fill]"
    if meta.get("ff"):
        return s + "   [all 0xFF - subsystem did not answer]"
    return s


# ── the XMCC feed ───────────────────────────────────────────────────────────


class Subframe:
    __slots__ = ("master_frame", "subframe", "time", "payload")

    def __init__(self, master_frame, subframe, time, payload):
        self.master_frame = master_frame
        self.subframe = subframe
        self.time = time
        self.payload = payload

    def tier(self, name: str) -> bytes:
        lo, hi = TIERS[name][0], TIERS[name][1]
        return self.payload[lo:hi]


def parse_header(h: bytes) -> tuple[int, int, str, int]:
    if h[:4] != MAGIC:
        raise ValueError("out of sync: bad magic")
    if h[4] != VERSION:
        raise ValueError(f"unsupported XMCC version {h[4]} — refusing to parse")
    mf, sf, year = struct.unpack(">IHH", h[6:14])
    month, day, hour, minute, second = h[14], h[15], h[16], h[17], h[18]
    ms, _rsv, length = struct.unpack(">HBI", h[19:26])
    stamp = f"{year:04d}-{month:02d}-{day:02d} {hour:02d}:{minute:02d}:{second:02d}.{ms:03d}"
    return mf, sf, stamp, length


def read_stream(readexact):
    while True:
        mf, sf, stamp, length = parse_header(readexact(HDR_LEN))
        yield Subframe(mf, sf, stamp, readexact(length))


def socket_reader(sock):
    def readexact(n: int) -> bytes:
        buf = b""
        while len(buf) < n:
            chunk = sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("GS closed the connection")
            buf += chunk
        return buf

    return readexact


def file_reader(fh):
    def readexact(n: int) -> bytes:
        buf = fh.read(n)
        if len(buf) < n:
            raise EOFError
        return buf

    return readexact


class TierAssembler:
    """Collects the slices of each tier block until the block is complete."""

    def __init__(self):
        self.slices: dict[tuple, dict[int, bytes]] = {}

    def feed(self, tier: str, f: Subframe) -> bytes | None:
        span = TIERS[tier][2]
        idx = (f.subframe - 1) % span
        key = (tier, f.master_frame, (f.subframe - 1) // span)
        got = self.slices.setdefault(key, {})
        got[idx] = f.tier(tier)
        if len(got) == span:
            del self.slices[key]
            return b"".join(got[i] for i in range(span))
        return None


def run_feed(dec: Decoder, readexact, quiet_tiers: set[str]):
    asm = TierAssembler()
    warmup_mf = None
    for f in read_stream(readexact):
        if warmup_mf is None:
            warmup_mf = f.master_frame
        print(f"\n==== MF#{f.master_frame} SF {f.subframe}/16  {f.time}  {len(f.payload)} B ====")
        if f.master_frame == warmup_mf:
            print("  (first master frame after connect — warm-up, values may be 0xFF)")
        for tier in ("VH", "H", "M", "L"):
            if tier in quiet_tiers:
                continue
            block = asm.feed(tier, f)
            if block is None:
                continue
            for entry in dec.twin["tiers"].get(tier, []):
                picked = dec.picked_from_block(entry, block)
                print(f"  [{tier}] {entry['id']}  {entry['subsystem']}  {entry['rsp']}")
                for name, value, meta in dec.decode_entry(entry, picked):
                    print(f"      {name:<44} = {format_value_marked(value, meta)}")


# ── self-test against the ground station's own decode ───────────────────────


PREFIX_NOTE = """
PREFIX NOTE — XLINK offsets disagree inside the ground station
--------------------------------------------------------------
Every XLINK-S TM response begins with product_key (16 B) + service_event (4 B).
The GS *display* path strips those 20 bytes before applying MIB offsets; the
*LUT builder* does not, so the picks were computed 20 bytes further in.
Verified against logs/master_frame_decoded.txt: 78/78 XL-010 values match only
when the 20-byte prefix is stripped, 51/78 without.

Affected: XL-008, XL-010, XL-012 - 127 parameters, 112 bytes of tier budget.
ADCS / EPS / GPS / UHF are unaffected (no such prefix).

Until the wire format is settled on the bench, --compat gs makes the MCC agree
with what the operator sees. It does NOT mean the bytes are the right ones.
"""


def selftest(dec: Decoder, log_path: Path) -> int:
    """
    Replays every entry in a tm_view `master_frame_decoded.txt` through this
    decoder and compares against the GS's own rendering of the same bytes.
    """
    import re

    text = Path(log_path).read_text(errors="replace")
    by_id = {}
    for tier in dec.twin["tiers"].values():
        for e in tier:
            by_id[e["id"]] = e

    entry_re = re.compile(r"^\s*\[(VH|H|M|L)\]\s+(\S+)\s+(\S+)\s+(\S+)\s*$")
    hex_re = re.compile(r"^\s*HEX:\s*([0-9a-fA-F ]+)$")
    val_re = re.compile(r"^\s{6}(\S.*?)\s*=\s*(.*?)\s*$")

    checked = passed = 0
    mismatches: list[str] = []
    cur = None
    for line in text.splitlines():
        m = entry_re.match(line)
        if m:
            cur = {"id": m.group(2), "hex": None, "vals": []}
            continue
        if cur is None:
            continue
        m = hex_re.match(line)
        if m:
            cur["hex"] = bytes.fromhex(m.group(1).replace(" ", ""))
            continue
        m = val_re.match(line)
        if m and cur.get("hex") is not None:
            cur["vals"].append((m.group(1), m.group(2)))
            continue
        if cur.get("hex") is not None and cur["vals"]:
            entry = by_id.get(cur["id"])
            if entry:
                got = dec.decode_entry(entry, cur["hex"])
                gotmap = {n: format_value(v, meta) for n, v, meta in got}
                for name, expect in cur["vals"]:
                    checked += 1
                    mine = gotmap.get(name, "<missing>")
                    if _same(mine, expect):
                        passed += 1
                    elif len(mismatches) < 25:
                        mismatches.append(f"  {cur['id']:8} {name:<42} GS={expect!r}  mine={mine!r}")
            cur = None

    print(f"self-test: {passed}/{checked} values match the ground station's own decode")
    if mismatches:
        print("\nfirst mismatches:")
        print("\n".join(mismatches))
    return 0 if checked and passed == checked else 1


def _same(a: str, b: str) -> bool:
    if a == b:
        return True
    # tolerate float formatting width, and "(?)" vs unnamed enum
    a2, b2 = a.replace(" (?)", ""), b.replace(" (?)", "")
    try:
        fa = float(a2.split()[0])
        fb = float(b2.split()[0])
        ua = a2.split()[1:]
        ub = b2.split()[1:]
        return abs(fa - fb) <= max(1e-6, abs(fb) * 1e-4) and ua == ub
    except (ValueError, IndexError):
        return a2 == b2


# ── entry point ─────────────────────────────────────────────────────────────


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--connect", metavar="HOST:PORT", help="dial the GS (GS in --mcc-listen mode)")
    src.add_argument("--listen", metavar="PORT", type=int, help="accept from the GS (--mcc-connect mode)")
    src.add_argument("--file", metavar="PATH", help="replay a captured XMCC byte stream")
    src.add_argument("--selftest", metavar="DECODED_LOG", help="verify against tm_view's own decode")
    ap.add_argument("--twin", default="config/rate_group_lut_bin_gs.json")
    ap.add_argument("--mib", default="mib")
    ap.add_argument("--expect-crc", type=int, help="fail unless the twin's CRC-16 equals this")
    ap.add_argument("--compat", choices=("gs", "mib"), default="gs",
                    help="gs (default): reproduce the ground station display, stripping the "
                         "product_key/service_event prefix on XLINK. mib: use MIB offsets as-is, "
                         "which is the coordinate system the LUT picks were built in. "
                         "The two disagree by 20 bytes on XLINK — see PREFIX NOTE in this file.")
    ap.add_argument("--only", default="", help="comma-separated tiers to show, e.g. VH,H")
    args = ap.parse_args()

    dec = Decoder(Path(args.twin), Path(args.mib), compat=args.compat)
    print(f"twin {args.twin}  CRC-16 = {dec.crc16}", file=sys.stderr)
    if args.expect_crc is not None and dec.crc16 != args.expect_crc:
        print(f"FATAL: twin CRC {dec.crc16} != expected {args.expect_crc}. "
              f"Decoding with the wrong twin yields plausible but wrong values.", file=sys.stderr)
        return 2

    if args.selftest:
        return selftest(dec, Path(args.selftest))

    quiet = set(TIERS) - {t.strip().upper() for t in args.only.split(",") if t.strip()} if args.only else set()

    if args.file:
        with open(args.file, "rb") as fh:
            try:
                run_feed(dec, file_reader(fh), quiet)
            except EOFError:
                pass
        return 0

    if args.connect:
        host, _, port = args.connect.rpartition(":")
        sock = socket.create_connection((host, int(port)))
    else:
        srv = socket.socket()
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("", args.listen))
        srv.listen(1)
        print(f"waiting for the GS on :{args.listen} ...", file=sys.stderr)
        sock, peer = srv.accept()
        print(f"GS connected from {peer}", file=sys.stderr)

    with sock:
        try:
            run_feed(dec, socket_reader(sock), quiet)
        except (ConnectionError, KeyboardInterrupt) as e:
            print(f"\nfeed ended: {e}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
