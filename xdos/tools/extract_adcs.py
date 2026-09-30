#!/usr/bin/env python3
"""
extract_adcs.py — Extract ADCS (CubeComputer 5 / Control Program 8)
telecommands, telemetry frames, events and enumerations from the
API reference manual.

Input  : layout text from `pdftotext -layout <api-ref>.pdf adcs.txt`
Output : a dict of records consumed by build_mib.py

The CubeComputer document uses a consistent table layout:

    Table N: <Name> Command Format        -> TC
    Table N: <Name> Telemetry Format      -> TM
    Table N: <Name> Event Format          -> EVENT
    Table N: <Name> Enumeration Values    -> enum

Command tables carry  "ID  n   Parameters Length (bytes)  n"
Telemetry tables carry "ID  n   Frame Length (bytes)  n"
Event tables carry     "ID  n   Class  <Info|Error|...>"
Parameter/Channel rows: Offset(bits) Length(bits) Name DataType Description
"""

from __future__ import annotations
import re
from dataclasses import dataclass, field
from typing import Optional


# --- table headers -------------------------------------------------------
RE_TABLE_HDR = re.compile(
    r'^\s*Table\s+(?P<num>\d+):\s+(?P<name>.+?)\s+'
    r'(?P<kind>Command Format|Telemetry Format|Event Format|Enumeration Values)\s*$',
    re.MULTILINE,
)
RE_ANY_TABLE = re.compile(r'^\s*Table\s+\d+:\s', re.MULTILINE)

# --- ID lines ------------------------------------------------------------
RE_ID_TC = re.compile(
    r'^\s*ID\s+(?P<id>\d+)\s+Parameters\s+Length\s*\(bytes\)\s+(?P<len>\d+)\s*$')
RE_ID_TM = re.compile(
    r'^\s*ID\s+(?P<id>\d+)\s+Frame\s+Length\s*\(bytes\)\s+(?P<len>\d+)\s*$')
RE_ID_EVENT = re.compile(
    r'^\s*ID\s+(?P<id>\d+)\s+Class\s+(?P<cls>\S+)\s*$')
RE_DESC = re.compile(r'^\s*Description\s+(?P<desc>.+?)\s*$')

# parameter / channel row
RE_PARAM_ROW = re.compile(
    r'^\s+(?P<off>\d+)\s+(?P<len>\d+)\s+(?P<rest>\S.*?)\s+'
    r'(?P<type>UINT|INT|BOOL|ENUM|FLOAT|DOUBLE|STRING)\s+(?P<desc>.*?)\s*$')

RE_ENUM_ROW = re.compile(
    r'^\s+(?P<val>-?\d+)\s+(?P<name>\S.*?)(?:\s{2,}(?P<desc>.+?))?\s*$')
# ADCS parameters reference an enum by TABLE NUMBER, e.g.
#   "... Possible values are in Table 3"
# so capture the table number — that is the reliable link key.
RE_ENUM_REF = re.compile(r'Table\s+(\d+)')
RE_UNIT = re.compile(r'\(Unit of measure is\s*\[(?P<unit>[^\]]+)\]\)')

# page-header / footer noise to skip inside multi-line tables
SKIP_LINE = re.compile(r'^\s*(file:///|25/06/2025|CubeComputer|Control Program)')


@dataclass
class Param:
    seq: int
    offset_bits: int
    length_bits: int
    name: str
    data_type: str
    enum_ref: Optional[str]
    unit: Optional[str]
    description: str
    name_col: int = 0
    desc_col: int = 0


@dataclass
class Message:
    table_num: int
    name: str
    direction: str          # 'TC' | 'TM' | 'EVENT'
    msg_id: int
    length_bytes: Optional[int]
    event_class: Optional[str]
    description: str = ''
    params: list = field(default_factory=list)


@dataclass
class EnumType:
    table_num: int
    name: str
    values: list = field(default_factory=list)   # (num, name, desc)


def _wrap_join(text: str) -> str:
    """Re-join column-wrapped 'Parameters/Frame Length (bytes)' lines."""
    text = re.sub(r'(Parameters|Frame)\s+Length\s+(\d+)\n\s+\(bytes\)',
                  r'\1 Length (bytes) \2', text)
    text = re.sub(r'(Parameters|Frame)\s+(\d+)\n\s+Length\s*\(bytes\)',
                  r'\1 Length (bytes) \2', text)
    return text


def _parse_params(lines: list) -> list:
    params, seq, last, in_block = [], 0, None, False
    for ln in lines:
        if SKIP_LINE.match(ln):
            continue
        if ('Parameters' in ln or 'Channels' in ln) and 'Offset' in ln:
            in_block = True
            continue
        if not in_block:
            continue
        if RE_ANY_TABLE.match(ln):
            break
        m = RE_PARAM_ROW.match(ln)
        if m:
            seq += 1
            desc = m.group('desc').strip()
            dtype = m.group('type')
            er = RE_ENUM_REF.search(desc) if dtype == 'ENUM' else None
            um = RE_UNIT.search(desc)
            last = Param(seq, int(m.group('off')), int(m.group('len')),
                         m.group('rest').strip(), dtype,
                         int(er.group(1)) if er else None,
                         um.group('unit').strip() if um else None,
                         desc, m.start('rest'), m.start('desc'))
            params.append(last)
            continue
        # continuation line: split by column position
        if last and ln.strip():
            namep = ln[last.name_col:last.desc_col].strip() if last.desc_col > last.name_col else ''
            descp = ln[last.desc_col:].strip() if last.desc_col > 0 else ln.strip()
            if not namep and not descp:
                namep = ln.strip()
            if namep:
                last.name = (last.name + ' ' + namep).strip()
            if descp:
                last.description = (last.description + ' ' + descp).strip()
            if last.enum_ref is None and last.data_type == 'ENUM':
                er = RE_ENUM_REF.search(last.description)
                if er:
                    last.enum_ref = int(er.group(1))
            if not last.unit:
                um = RE_UNIT.search(last.description)
                if um:
                    last.unit = um.group('unit').strip()
    return params


def _parse_message(num, name, kind, lines):
    direction = {'Command Format': 'TC', 'Telemetry Format': 'TM',
                 'Event Format': 'EVENT'}[kind]
    msg = Message(num, name.strip(), direction, -1, None, None)
    id_idx = -1
    for i, ln in enumerate(lines):
        if direction == 'TC':
            m = RE_ID_TC.match(ln)
            if m:
                msg.msg_id, msg.length_bytes = int(m.group('id')), int(m.group('len'))
                id_idx = i
                break
        elif direction == 'TM':
            m = RE_ID_TM.match(ln)
            if m:
                msg.msg_id, msg.length_bytes = int(m.group('id')), int(m.group('len'))
                id_idx = i
                break
        else:  # EVENT
            m = RE_ID_EVENT.match(ln)
            if m:
                msg.msg_id, msg.event_class = int(m.group('id')), m.group('cls')
                id_idx = i
                break
    if id_idx < 0:
        return None
    # description
    for ln in lines[id_idx + 1:]:
        d = RE_DESC.match(ln)
        if d:
            msg.description = d.group('desc').strip()
            break
    msg.params = _parse_params(lines)
    return msg


def _parse_enum(num, name, lines):
    et = EnumType(num, name.strip())
    seen, last = False, None
    for ln in lines:
        s = ln.strip()
        if SKIP_LINE.match(ln):
            continue
        if not seen:
            if 'Numeric Value' in ln and 'Name' in ln:
                seen = True
            continue
        if RE_ANY_TABLE.match(ln):
            break
        m = RE_ENUM_ROW.match(ln)
        if m:
            last = (int(m.group('val')), m.group('name').strip(),
                    (m.group('desc') or '').strip())
            et.values.append(last)
        elif last and s:
            i = len(et.values) - 1
            v, nm, ds = et.values[i]
            et.values[i] = (v, nm, (ds + ' ' + s).strip())
    return et


def extract(text_path: str) -> dict:
    text = _wrap_join(open(text_path, encoding='utf-8', errors='replace').read())
    # slice boundaries: every "Table N:" of any kind
    stops = [m.start() for m in RE_ANY_TABLE.finditer(text)] + [len(text)]
    messages, enums, skipped = [], [], []
    for m in RE_TABLE_HDR.finditer(text):
        start = m.start()
        end = next(s for s in stops if s > start)
        lines = text[start:end].splitlines()
        num, name, kind = int(m.group('num')), m.group('name'), m.group('kind')
        if kind == 'Enumeration Values':
            enums.append(_parse_enum(num, name, lines))
        else:
            msg = _parse_message(num, name, kind, lines)
            (messages if msg else skipped).append(msg or (num, name, kind))
    return {'subsystem': 'ADCS', 'protocol': 'cubespace_native',
            'offset_unit': 'bits', 'messages': messages,
            'enums': enums, 'skipped': skipped}


if __name__ == '__main__':
    import sys
    r = extract(sys.argv[1] if len(sys.argv) > 1 else 'adcs.txt')
    tc = sum(1 for m in r['messages'] if m.direction == 'TC')
    tm = sum(1 for m in r['messages'] if m.direction == 'TM')
    ev = sum(1 for m in r['messages'] if m.direction == 'EVENT')
    np = sum(len(m.params) for m in r['messages'])
    print(f"ADCS: {tc} TC, {tm} TM, {ev} EVENT, {len(r['enums'])} enums, "
          f"{np} params, {len(r['skipped'])} skipped")
    for s in r['skipped'][:8]:
        print('   skipped:', s)
