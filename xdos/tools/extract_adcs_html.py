#!/usr/bin/env python3
"""
extract_adcs_html.py — Rebuild mib/adcs.json from a CubeSpace CubeComputer
API-reference **HTML** export (Word "Web Page" export, MsoNormalTable style).

Written for Control Program 9; works on any export using the same layout:

    <p>Table N: <Name> Command Format</p>      -> TC
    <p>Table N: <Name> Telemetry Format</p>    -> TM
    <p>Table N: <Name> Event Format</p>        -> EVENT
    <p>Table N: <Name> Enumeration Values</p>  -> enum
    <p>Table N: List of Configuration Messages -> is_config flags

Each message table:
    row: ID | <id> | Parameters Length (bytes) | <len>     (TC)
    row: ID | <id> | Frame Length (bytes) | <len>          (TM)
    row: ID | <id> | Class | <Info|Error|...>              (EVENT)
    row: Description | <text>
    row: Parameters|Channels | Offset (bits) | Length (bits) | Name | Data Type | Description
    rows: '' | off | len | name | type | desc

Output schema is byte-compatible with tools/sqlite_to_json.py (the schema the
C++ mib loader consumes).  This extractor writes mib/adcs.json DIRECTLY and
does not touch tools/xdos_mib.sqlite — the sqlite DB predates the audited
EPS/GPS JSONs and re-exporting from it would clobber them.

Fidelity upgrades over the old PDF extractor (extract_adcs.py):
  * scale      — "Formatted value ... = RAWVAL*k" is captured as the codec's
                 counts-per-unit factor (scale = 1/k, since the codec decodes
                 engineering = raw / scale).  Disable with --no-scale.
  * PADDING    — kept as data_type RESERVED (codec-supported) instead of
                 being dropped; unnamed rows become "Reserved", uniquified
                 per message.
  * ARRAY      — kept as data_type HEX with is_array=true and
                 array_count = length_bits/8; the codec skips is_array
                 params, so large blobs stay inert.
  * is_config  — set from the "List of Configuration Messages" index table.

Usage:
    python3 tools/extract_adcs_html.py <api-ref.html> \
        [-o mib/adcs.json] [--no-scale] [--diff-old <old_adcs.json>]
"""

from __future__ import annotations
import argparse
import json
import re
import sys
from collections import Counter
from pathlib import Path

from bs4 import BeautifulSoup

DIR_ORDER = {"EVENT": 0, "TC": 1, "TM": 2}
KIND_SUFFIX = {
    "Command Format": "TC",
    "Telemetry Format": "TM",
    "Event Format": "EVENT",
    "Enumeration Values": "ENUM",
}
KNOWN_TYPES = {"UINT", "INT", "BOOL", "ENUM", "FLOAT", "DOUBLE", "STRING",
               "CHAR", "HEX", "RESERVED", "PADDING", "ARRAY"}

RE_TITLE = re.compile(r"^Table\s+(\d+)\s*:\s*(.+?)\s*$")
RE_ENUM_REF = re.compile(r"Possible values are in Table\s+(\d+)")
RE_UNIT = re.compile(r"\(Unit of measure is\s*\[([^\]]+)\]\)")
RE_SCALE = re.compile(
    r"formula:\s*\(formatted value\)\s*(?:\[[^\]]*\])?\s*=\s*"
    r"RAWVAL\s*([*/])\s*([0-9]+(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)f?")


def norm(s: str) -> str:
    return re.sub(r"\s+", " ", s).strip()


def table_rows(tb) -> list[list[str]]:
    return [[norm(c.get_text(" ")) for c in tr.find_all(["td", "th"])]
            for tr in tb.find_all("tr")]


def title_for(tb):
    """Nearest preceding non-empty paragraph, iff it is a 'Table N:' caption."""
    for prev in tb.find_all_previous(["p", "table"], limit=10):
        if prev.name == "table":
            return None, None
        txt = norm(prev.get_text(" "))
        if not txt:
            continue
        m = RE_TITLE.match(txt)
        return (int(m.group(1)), m.group(2)) if m else (None, None)
    return None, None


def scale_from_desc(desc: str):
    """Doc formula factor -> codec counts-per-unit (codec: eng = raw/scale)."""
    m = RE_SCALE.search(desc)
    if not m:
        return None
    op, num = m.group(1), float(m.group(2))
    if num == 0:
        return None
    factor = num if op == "*" else 1.0 / num   # eng = raw * factor
    scale = 1.0 / factor                       # codec scale = 1/factor
    r = round(scale)
    return r if abs(scale - r) < 1e-6 else scale


def parse_params(rows, warnings, ctx, capture_scale=True):
    params, seq, used_names = [], 0, Counter()
    for cells in rows:
        # normalise away the rowspan label column
        if len(cells) == 6 and cells[0] in ("", "Parameters", "Channels"):
            cells = cells[1:]
        if len(cells) != 5 or not (cells[0].isdigit() and cells[1].isdigit()):
            continue
        off, ln, name, dtype, desc = (int(cells[0]), int(cells[1]),
                                      cells[2], cells[3], cells[4])
        if dtype not in KNOWN_TYPES:
            warnings.append(f"{ctx}: unknown data type {dtype!r} for {name!r}")
            continue
        seq += 1
        enum_name, unit, scale = None, None, None
        is_array, array_count = False, None

        if dtype == "ENUM":
            m = RE_ENUM_REF.search(desc)
            if m:
                enum_name = int(m.group(1))        # table number; resolved later
            else:
                warnings.append(f"{ctx}: ENUM param {name!r} has no table ref")
        if dtype == "PADDING":
            dtype = "RESERVED"
            name = name or "Reserved"
        if dtype == "ARRAY":
            dtype, is_array, array_count = "HEX", True, ln // 8

        mu = RE_UNIT.search(desc)
        if mu:
            unit = mu.group(1).strip()
        if capture_scale and dtype in ("UINT", "INT"):
            scale = scale_from_desc(desc)
        elif capture_scale and RE_SCALE.search(desc):
            warnings.append(f"{ctx}: formula on non-integer {dtype} param "
                            f"{name!r} — scale not applied")

        # uniquify duplicate names inside one message (decode maps by name)
        used_names[name] += 1
        if used_names[name] > 1:
            name = f"{name} ({used_names[name]})"

        params.append({
            "seq": seq, "offset": off, "length": ln, "name": name,
            "data_type": dtype, "enum": enum_name, "unit": unit,
            "scale": scale, "description": desc,
            "is_array": is_array, "array_count": array_count,
        })
    return params


def extract(html_path: Path, capture_scale=True):
    soup = BeautifulSoup(html_path.read_text(encoding="utf-8",
                                             errors="replace"), "lxml")
    messages, enums, index_tables, warnings = [], {}, {}, []

    for tb in soup.find_all("table"):
        num, full = title_for(tb)
        if num is None:
            continue
        kind = next((k for k in KIND_SUFFIX if full.endswith(k)), None)
        if kind is None:
            index_tables[full] = tb
            continue
        name = full[: -len(kind)].strip()
        rows = table_rows(tb)
        ctx = f"Table {num} ({name})"

        if kind == "Enumeration Values":
            vals, started = [], False
            for c in rows:
                if len(c) >= 2 and c[0] == "Numeric Value":
                    started = True
                    continue
                if started and len(c) >= 2 and re.fullmatch(r"-?\d+", c[0]):
                    vals.append({"value": int(c[0]), "name": c[1],
                                 "description": c[2] if len(c) > 2 else ""})
            if not vals:
                warnings.append(f"{ctx}: enum table with no values")
            enums[num] = {"name": name, "doc_reference": f"Table {num}",
                          "values": vals}
            continue

        direction = KIND_SUFFIX[kind]
        msg_id = length_bytes = None
        description = ""
        head = rows[0] if rows else []
        if len(head) >= 4 and head[0] == "ID":
            if direction in ("TC", "TM") and "Length" in head[2]:
                msg_id, length_bytes = int(head[1]), int(head[3])
            elif direction == "EVENT" and head[2] == "Class":
                msg_id = int(head[1])          # length_bytes stays null
        if msg_id is None:
            warnings.append(f"{ctx}: no ID row — table skipped")
            continue
        for c in rows[1:3]:
            if len(c) >= 2 and c[0] == "Description":
                description = c[1]
                break

        messages.append({
            "direction": direction, "msg_id": msg_id, "wire_id": msg_id,
            "name": name, "node": "ADCS", "length_bytes": length_bytes,
            "is_variable_length": False, "is_config": False,
            "request_via": None, "offset_unit": "bits",
            "doc_reference": f"Table {num}", "description": description,
            "parameters": parse_params(rows, warnings, ctx, capture_scale),
        })

    # resolve enum refs: table number -> enum name
    for m in messages:
        for p in m["parameters"]:
            if isinstance(p["enum"], int):
                et = enums.get(p["enum"])
                if et:
                    p["enum"] = et["name"]
                else:
                    warnings.append(f"{m['doc_reference']} ({m['name']}): "
                                    f"param {p['name']!r} references missing "
                                    f"enum Table {p['enum']}")
                    p["enum"] = None

    # is_config from the configuration index table (rows: ID | Name | ...)
    cfg_ids = set()
    for full, tb in index_tables.items():
        if "List of Configuration Messages" in full:
            for c in table_rows(tb):
                if c and re.fullmatch(r"\d+", c[0]):
                    cfg_ids.add(int(c[0]))
    if cfg_ids:
        for m in messages:
            if m["msg_id"] in cfg_ids and m["direction"] in ("TC", "TM"):
                m["is_config"] = True

    messages.sort(key=lambda m: (DIR_ORDER[m["direction"]], m["msg_id"]))
    enum_list = sorted(enums.values(), key=lambda e: e["name"])
    dup = [k for k, n in Counter((m["direction"], m["msg_id"])
                                 for m in messages).items() if n > 1]
    if dup:
        warnings.append(f"duplicate (direction,id) keys: {dup}")

    mib = {
        "subsystem": "ADCS",
        "protocol": "cubespace_native",
        "description": "CubeComputer 5 / Control Program 9 (CubeSpace Gen2)",
        "nodes": [{"name": "ADCS", "address": None,
                   "description": "CubeComputer node"}],
        "enums": enum_list,
        "messages": messages,
    }
    return mib, index_tables, warnings


def diff_report(old_path: Path, new: dict) -> str:
    old = json.loads(old_path.read_text())
    key = lambda m: (m["direction"], m["msg_id"])
    om = {key(m): m for m in old["messages"]}
    nm = {key(m): m for m in new["messages"]}
    lines = []
    added = sorted(set(nm) - set(om))
    removed = sorted(set(om) - set(nm))
    lines.append(f"added   ({len(added)}): "
                 + ", ".join(f"{d} {i} {nm[(d,i)]['name']!r}" for d, i in added))
    lines.append(f"removed ({len(removed)}): "
                 + (", ".join(f"{d} {i} {om[(d,i)]['name']!r}"
                              for d, i in removed) or "none"))
    len_chg, name_chg = [], []
    for k in sorted(set(om) & set(nm)):
        o, n = om[k], nm[k]
        if o["length_bytes"] != n["length_bytes"]:
            len_chg.append(f"{k[0]} {k[1]} {n['name']!r}: "
                           f"{o['length_bytes']} -> {n['length_bytes']} B")
        if o["name"] != n["name"]:
            name_chg.append(f"{k[0]} {k[1]}: {o['name']!r} -> {n['name']!r}")
    lines.append(f"length changed ({len(len_chg)}):")
    lines += [f"    {s}" for s in len_chg]
    lines.append(f"renamed ({len(name_chg)}):")
    lines += [f"    {s}" for s in name_chg] or ["    none"]
    oe = {e["name"] for e in old["enums"]}
    ne = {e["name"] for e in new["enums"]}
    lines.append(f"enums: +{sorted(ne - oe)}  -{sorted(oe - ne)}")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("html", type=Path)
    ap.add_argument("-o", "--out", type=Path,
                    default=Path(__file__).resolve().parent.parent
                    / "mib" / "adcs.json")
    ap.add_argument("--no-scale", action="store_true",
                    help="do not capture Formatted-value scale factors")
    ap.add_argument("--diff-old", type=Path,
                    help="print a delta report against an older adcs.json")
    args = ap.parse_args()

    mib, index_tables, warnings = extract(args.html,
                                          capture_scale=not args.no_scale)

    d = Counter(m["direction"] for m in mib["messages"])
    npar = sum(len(m["parameters"]) for m in mib["messages"])
    nsc = sum(1 for m in mib["messages"] for p in m["parameters"]
              if p["scale"] is not None)
    print(f"ADCS: {d['TC']} TC, {d['TM']} TM, {d['EVENT']} EVENT, "
          f"{len(mib['enums'])} enums, {npar} params "
          f"({nsc} with scale), "
          f"{sum(1 for m in mib['messages'] if m['is_config'])} config msgs")
    print("index tables:", ", ".join(sorted(index_tables)) or "none")
    for w in warnings:
        print("  WARN:", w)

    if args.diff_old and args.diff_old.exists():
        print("\n── delta vs", args.diff_old, "──")
        print(diff_report(args.diff_old, mib))

    args.out.write_text(json.dumps(mib, indent=2, ensure_ascii=False) + "\n",
                        encoding="utf-8")
    print("\nwrote", args.out)
    return 0 if not any("skipped" in w for w in warnings) else 1


if __name__ == "__main__":
    sys.exit(main())
