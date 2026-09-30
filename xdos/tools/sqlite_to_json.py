#!/usr/bin/env python3
"""
sqlite_to_json.py — export the unified MIB as three per-subsystem JSON files.

Reads the SQLite MIB built by the extractors and writes:
    mib/adcs.json
    mib/eps.json
    mib/gps.json

Schema (one file per subsystem):
    {
      "subsystem":   "EPS",
      "protocol":    "eps_rs485",
      "description": "...",
      "nodes":       [{"name":"PDU","address":1,"description":"..."}, ...],
      "enums":       [{"name":"...","doc_reference":"...","values":[...]}, ...],
      "messages":    [{
            "direction":          "TC"|"TM"|"EVENT",
            "msg_id":             <int>,
            "wire_id":            <int>,
            "name":               "...",
            "node":               "PDU"|null,
            "length_bytes":       <int>|null,
            "is_variable_length": <bool>,
            "is_config":          <bool>,
            "request_via":        "LOG"|null,
            "offset_unit":        "bits"|"bytes",
            "doc_reference":      "...",
            "description":        "...",
            "parameters": [{
                "seq":          <int>,
                "offset":       <int>|null,
                "length":       <int>,
                "name":         "...",
                "data_type":    "UINT"|"INT"|"FLOAT"|"DOUBLE"|"BOOL"
                                |"ENUM"|"CHAR"|"HEX"|"RESERVED",
                "enum":         "EnumName"|null,
                "unit":         "..."|null,
                "scale":        <number>|null,
                "description":  "...",
                "is_array":     <bool>,
                "array_count":  <int>|null
            }, ...]
        }, ...]
    }
"""

from __future__ import annotations
import argparse
import json
import sqlite3
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_DB = HERE / 'xdos_mib.sqlite'
DEFAULT_OUT = HERE.parent / 'mib'


def export(db_path: Path, out_dir: Path) -> dict:
    conn = sqlite3.connect(str(db_path))
    conn.row_factory = sqlite3.Row
    cur = conn.cursor()

    written: dict[str, int] = {}

    for s in cur.execute("SELECT * FROM subsystem ORDER BY name").fetchall():
        sub_id = s['id']

        nodes = [
            {'name': n['name'], 'address': n['address'],
             'description': n['description']}
            for n in cur.execute(
                "SELECT * FROM node WHERE subsystem_id=? ORDER BY name",
                (sub_id,)).fetchall()
        ]

        enums = []
        for et in cur.execute(
                "SELECT * FROM enum_type WHERE subsystem_id=? ORDER BY name",
                (sub_id,)).fetchall():
            vals = [
                {'value': v['numeric_value'], 'name': v['name'],
                 'description': v['description'] or ''}
                for v in cur.execute(
                    "SELECT * FROM enum_value WHERE enum_type_id=? "
                    "ORDER BY numeric_value", (et['id'],)).fetchall()
            ]
            enums.append({'name': et['name'],
                          'doc_reference': et['doc_reference'] or '',
                          'values': vals})

        messages = []
        for m in cur.execute(
                "SELECT m.*, n.name AS node_name "
                "FROM message m LEFT JOIN node n ON n.id=m.node_id "
                "WHERE m.subsystem_id=? "
                "ORDER BY m.direction, m.msg_id", (sub_id,)).fetchall():
            params = []
            for p in cur.execute(
                    "SELECT p.*, e.name AS enum_name "
                    "FROM parameter p LEFT JOIN enum_type e "
                    "  ON e.id=p.enum_type_id "
                    "WHERE p.message_id=? ORDER BY p.seq",
                    (m['id'],)).fetchall():
                params.append({
                    'seq':         p['seq'],
                    'offset':      p['offset'],
                    'length':      p['length'],
                    'name':        p['name'],
                    'data_type':   p['data_type'],
                    'enum':        p['enum_name'],
                    'unit':        p['unit'],
                    'scale':       p['scale'],
                    'description': p['description'] or '',
                    'is_array':    bool(p['is_array']),
                    'array_count': p['array_count'],
                })
            messages.append({
                'direction':          m['direction'],
                'msg_id':             m['msg_id'],
                'wire_id':            m['wire_id'],
                'name':               m['name'],
                'node':               m['node_name'],
                'length_bytes':       m['length_bytes'],
                'is_variable_length': bool(m['is_variable_length']),
                'is_config':          bool(m['is_config']),
                'request_via':        m['request_via'],
                'offset_unit':        m['offset_unit'],
                'doc_reference':      m['doc_reference'] or '',
                'description':        m['description'] or '',
                'parameters':         params,
            })

        out_obj = {
            'subsystem':   s['name'],
            'protocol':    s['protocol'],
            'description': s['description'] or '',
            'nodes':       nodes,
            'enums':       enums,
            'messages':    messages,
        }
        out_path = out_dir / f"{s['name'].lower()}.json"
        out_dir.mkdir(parents=True, exist_ok=True)
        with open(out_path, 'w') as f:
            json.dump(out_obj, f, indent=2)
        written[s['name']] = len(messages)
        print(f"  {s['name']:6} -> {out_path}   ({len(messages)} messages, "
              f"{len(nodes)} nodes, {len(enums)} enums)")

    conn.close()
    return written


def main(argv) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--db', type=Path, default=DEFAULT_DB,
                    help=f'SQLite MIB path (default: {DEFAULT_DB})')
    ap.add_argument('--out', type=Path, default=DEFAULT_OUT,
                    help=f'output directory (default: {DEFAULT_OUT})')
    args = ap.parse_args(argv[1:])
    if not args.db.exists():
        sys.exit(f"SQLite MIB not found: {args.db}")
    print(f"Reading: {args.db}")
    print(f"Writing: {args.out}/")
    export(args.db, args.out)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
