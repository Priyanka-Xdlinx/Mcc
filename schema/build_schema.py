#!/usr/bin/env python3
"""
build_schema.py - build mcc.db from the mission database.

Reads `xdos/mib/*.json` and `xdos/config/rate_group_lut_bin_gs.json`
and writes a SQLite database holding the decode schema plus the flown lookup table.

`xdos/` (the XDLINX core team's tree, vendored into this repository) is the authoritative source. The older top-level `mib/` copy is
stale: its adcs.json stores `scale` as the reciprocal (10000 where the current
copy says 0.0001), and two EPS BMS message ids are swapped. Building from it
would put every scaled ADCS reading 10^8 out.

It reads the JSON MIBs, NOT mission_parameter_dictionary.csv.  That matters:
the CSV is a flattened export with no `endian` column at all, and its `scale`
column is a divisor for ADCS rows but a multiplier for EPS/XLINK-S rows.  The
previously published contract was generated from the CSV, which is why it had
to hard-code little-endian everywhere and invent `scale if scale < 1 else
1/scale` to recover a usable factor.  Reading the JSON removes both problems at
the source: `endian` is carried per parameter, and `scale` is uniformly a
multiplier.

    python3 build_schema.py [--xdos DIR] [--mib DIR] [--out mcc.db]
"""

from __future__ import annotations

import argparse
import datetime as _dt
import json
import sqlite3
import sys
from pathlib import Path

# Tier layout of the 1280-byte subframe (src/lut/lut_format.hpp).
TIERS = [
    # name, seg_offset, seg_bytes, block_span, block_budget
    ("VH",    0, 400, 1,  400),
    ("H",   400, 400, 2,  800),
    ("M",   800, 300, 4, 1200),
    ("L",  1100, 175, 8, 1400),
]


def load_apid_map(mib_dir: Path) -> dict[str, int]:
    """APIDs live in the CCSDS profile, not in the subsystem MIBs."""
    out: dict[str, int] = {}
    for fn in ("ccsds_tc.json", "ccsds_tm.json"):
        p = mib_dir / fn
        if not p.exists():
            continue
        doc = json.loads(p.read_text())
        for key in ("apid_map", "apids", "apid"):
            m = doc.get(key)
            if isinstance(m, dict):
                for name, apid in m.items():
                    if isinstance(apid, int):
                        out[name] = apid
    return out


def resolve_apid(apid_map: dict[str, int], subsystem: str) -> int | None:
    """The profile spells XLINK-S as 'XLINK'; try the obvious aliases."""
    for key in (subsystem, subsystem.replace("-S", ""), subsystem.upper(),
                subsystem.replace("-S", "").upper()):
        if key in apid_map:
            return apid_map[key]
    return None


def main() -> int:
    ap = argparse.ArgumentParser()
    here = Path(__file__).resolve().parent
    root = here.parent                             # the mcc-app repository
    ap.add_argument("--xdos", default=str(root / "xdos"))
    ap.add_argument("--mib",  default=str(root / "xdos" / "mib"))
    ap.add_argument("--out",  default=str(here.parent / "mcc.db"))
    ap.add_argument("--all-directions", action="store_true",
                    help="also load TC and EVENT definitions (default: TM only)")
    args = ap.parse_args()

    mib_dir = Path(args.mib)
    twin_path = Path(args.xdos) / "config" / "rate_group_lut_bin_gs.json"
    out = Path(args.out)

    if out.exists():
        out.unlink()
    db = sqlite3.connect(out)
    db.executescript((here / "schema.sql").read_text())

    apid_map = load_apid_map(mib_dir)
    db.executemany("INSERT INTO tier VALUES (?,?,?,?,?)", TIERS)

    # Only telemetry is ever decoded from the feed: every LUT entry resolves to a
    # TM message, and TC/EVENT definitions describe what goes *up*, not what
    # comes down. Loading them would triple the table for nothing.
    wanted = {"TC", "TM", "EVENT"} if args.all_directions else {"TM"}

    sub_id: dict[str, int] = {}
    enum_id: dict[tuple[str, str], int] = {}
    msg_id: dict[tuple[str, str, str], int] = {}       # (subsystem, name, direction)
    param_id: dict[tuple[int, str], int] = {}          # (message_id, param name)
    stats = {"subsystems": 0, "enums": 0, "enum_values": 0, "messages": 0, "parameters": 0}

    for path in sorted(mib_dir.glob("*.json")):
        doc = json.loads(path.read_text())
        sub = doc.get("subsystem")
        if not sub or not doc.get("messages"):
            continue                                    # ccsds_tc/tm carry no messages

        cur = db.execute(
            "INSERT INTO subsystem (name, apid, protocol, source_file) VALUES (?,?,?,?)",
            (sub, resolve_apid(apid_map, sub), doc.get("protocol"), path.name))
        sid = cur.lastrowid
        sub_id[sub] = sid
        stats["subsystems"] += 1

        # enums: a list of {name, values:[{value,name,description}]}
        for en in doc.get("enums") or []:
            cur = db.execute("INSERT INTO enum_type (subsystem_id, name) VALUES (?,?)",
                             (sid, en["name"]))
            eid = cur.lastrowid
            enum_id[(sub, en["name"])] = eid
            stats["enums"] += 1
            seen = set()
            for v in en.get("values") or []:
                val = v.get("value")
                if val is None or val in seen:
                    continue                            # a few MIB enums repeat a value
                seen.add(val)
                db.execute("INSERT INTO enum_value VALUES (?,?,?,?)",
                           (eid, val, v.get("name"), v.get("description")))
                stats["enum_values"] += 1

        for m in doc["messages"]:
            if m["direction"] not in wanted:
                continue
            unit = m.get("offset_unit") or doc.get("offset_unit") or "bytes"
            key = (sub, m["name"], m["direction"])
            if key in msg_id:
                continue                                # defensive: never silently shadow
            cur = db.execute(
                """INSERT INTO message (subsystem_id, name, direction, msg_id, wire_id,
                                        node, offset_unit, length_bytes, is_variable_length)
                   VALUES (?,?,?,?,?,?,?,?,?)""",
                (sid, m["name"], m["direction"], m.get("msg_id"), m.get("wire_id"),
                 m.get("node"), unit, m.get("length_bytes"),
                 1 if m.get("is_variable_length") else 0))
            mid = cur.lastrowid
            msg_id[key] = mid
            stats["messages"] += 1

            for p in m.get("parameters") or []:
                off, ln = p.get("offset"), p.get("length")
                if unit == "bits":
                    bit_off = off
                    bit_len = ln
                else:
                    bit_off = None if off is None else off * 8
                    bit_len = None if ln is None else ln * 8
                aligned = 1 if (bit_off is not None and bit_len is not None
                                and bit_off % 8 == 0 and bit_len % 8 == 0) else 0
                # Prefer "enum"; the MIB also uses "enum_type" on 73 parameters.
                eref = p.get("enum") or p.get("enum_type")
                db.execute(
                    """INSERT INTO parameter (message_id, seq, name, offset, length,
                            bit_offset, bit_length, byte_aligned, data_type, endian,
                            scale, unit, enum_type_id, is_array, array_count, description)
                       VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)""",
                    (mid, p["seq"], p["name"], off, ln, bit_off, bit_len, aligned,
                     p["data_type"], p.get("endian", "little"),
                     p.get("scale"), p.get("unit"),
                     enum_id.get((sub, eref)) if eref else None,
                     1 if p.get("is_array") else 0, p.get("array_count"),
                     p.get("description")))
                param_id[(mid, p["name"])] = db.execute(
                    "SELECT last_insert_rowid()").fetchone()[0]
                stats["parameters"] += 1

    # ── Layer 1: the flown lookup table ─────────────────────────────────────
    twin = json.loads(twin_path.read_text())
    db.execute("INSERT INTO lut VALUES (1,?,?,?,?,?)",
               (twin["crc16"], twin["format_version"], twin["subframe_size"],
                str(twin_path), _dt.datetime.now().isoformat(timespec="seconds")))

    unresolved: list[str] = []
    n_entries = n_picks = n_slots = 0
    for tier_name, *_ in TIERS:
        for idx, e in enumerate(twin["tiers"].get(tier_name, [])):
            sub = e["subsystem"]
            mid = msg_id.get((sub, e["rsp"], "TM"))
            if mid is None:
                unresolved.append(f"{e['id']} {sub}/{e['rsp']}")
            cur = db.execute(
                """INSERT INTO lut_entry (tier, entry_index, tc_id, subsystem_id,
                                          rsp, apid, tm_raw_len, message_id)
                   VALUES (?,?,?,?,?,?,?,?)""",
                (tier_name, idx, e["id"], sub_id[sub], e["rsp"], e.get("apid"),
                 e["tm_raw_len"], mid))
            eid = cur.lastrowid
            n_entries += 1
            for pseq, pk in enumerate(e["picks"]):
                cur = db.execute(
                    "INSERT INTO pick (entry_id, seq, src, size, block) VALUES (?,?,?,?,?)",
                    (eid, pseq, pk["src"], pk["size"], pk["block"]))
                pid = cur.lastrowid
                n_picks += 1
                for nseq, pname in enumerate(pk["params"]):
                    db.execute("INSERT INTO pick_param VALUES (?,?,?,?)",
                               (pid, nseq, param_id.get((mid, pname)) if mid else None,
                                pname))
                    n_slots += 1

    db.commit()

    orphans = db.execute(
        "SELECT COUNT(*) FROM pick_param WHERE parameter_id IS NULL").fetchone()[0]

    print(f"wrote {out}")
    print(f"  directions loaded: {', '.join(sorted(wanted))}")
    print(f"  subsystems {stats['subsystems']}  messages {stats['messages']}  "
          f"parameters {stats['parameters']}")
    print(f"  enums {stats['enums']} ({stats['enum_values']} values)")
    print(f"  LUT crc16 {twin['crc16']}  entries {n_entries}  picks {n_picks}  "
          f"slots {n_slots}")
    tm = db.execute("SELECT COUNT(*) FROM parameter p JOIN message m ON p.message_id=m.id"
                    " WHERE m.direction='TM'").fetchone()[0]
    nba = db.execute("SELECT COUNT(*) FROM parameter WHERE byte_aligned=0").fetchone()[0]
    be = db.execute("SELECT COUNT(*) FROM parameter WHERE endian='big'").fetchone()[0]
    sc = db.execute("SELECT COUNT(*) FROM parameter WHERE scale IS NOT NULL").fetchone()[0]
    print(f"  TM parameters {tm}  not byte-aligned {nba}  big-endian {be}  with scale {sc}")

    if unresolved:
        print(f"  !! {len(unresolved)} LUT entries did not resolve to a TM message:",
              file=sys.stderr)
        for u in unresolved:
            print(f"     {u}", file=sys.stderr)
    if orphans:
        print(f"  !! {orphans} pick_param rows did not resolve to a parameter",
              file=sys.stderr)
    return 1 if (unresolved or orphans) else 0


if __name__ == "__main__":
    sys.exit(main())
