#!/usr/bin/env python3
"""
extract_gnss.py — extend mib/gps.json from the GNSS workbook
(GNSS_LOG_TC_once_TM_16commands_breakdown.xlsx).

Design
------
The 7 pre-existing logs (VERSION, BESTPOS, RXSTATUS, BESTVEL, TIME,
PSRDOP, HWMONITOR) and the 3 TCs (LOG, UNLOG, UNLOGALL) were transcribed
from the verified C++ implementation and are exercised by the test
suite and the simulator.  They are kept VERBATIM as the base; the
workbook is used to
  1. ADD the 9 new logs      (BESTXYZ, RTKPOS, PSRPOS, TIMESYNC,
                              PSRDOP2, RTKDOP, RXSTATUSEVENT, SATVIS2,
                              RANGE)
  2. CROSS-CHECK the 7 existing logs field-by-field (report only —
     discrepancies are printed, never silently applied)
  3. FILL the enum layer     (value tables + 'enum' refs on parameters)

Enum value tables
-----------------
The workbook marks fields as data-type "Enum" but does not enumerate
the values (Source_Notes points at OEM7 manual pages).  The tables
below are therefore the standard NovAtel OEM7 tables from those manual
sections.  They affect DISPLAY only — wire encoding is unchanged.

Usage:  python3 tools/extract_gnss.py <workbook.xlsx> [mib/gps.json]
"""
from __future__ import annotations
import json, re, sys, copy
from pathlib import Path

import openpyxl

# ── OEM7 standard enum tables (display layer; flag to operator) ─────────────
def E(name, doc, vals):
    return {"name": name, "doc_reference": doc,
            "values": [{"value": v, "name": n, "description": d} for v, n, d in vals]}

OEM7_ENUMS = [
    E("Port", "OEM7 detailed port identifiers (subset), p.41-43", [
        (0, "NO_PORTS", "No ports"), (1, "COM1", "COM port 1"),
        (2, "COM2", "COM port 2"), (3, "COM3", "COM port 3"),
        (6, "THISPORT", "Port this command was received on"),
        (8, "ALL", "All ports"), (13, "USB1", "USB port 1"),
        (14, "USB2", "USB port 2"), (15, "USB3", "USB port 3"),
    ]),
    E("LogTrigger", "OEM7 LOG command trigger, p.242-246", [
        (0, "ONNEW", "New or changed data"), (1, "ONCHANGED", "Changed data"),
        (2, "ONTIME", "Periodic, every 'period' seconds"),
        (3, "ONNEXT", "Next occurrence"), (4, "ONCE", "Immediately, once"),
        (5, "ONMARK", "On mark input trigger"),
    ]),
    E("HoldFlag", "OEM7 LOG command hold field", [
        (0, "NOHOLD", "Cleared by UNLOGALL"),
        (1, "HOLD", "Survives UNLOGALL (needs held=1 to clear)"),
    ]),
    E("SolStatus", "OEM7 solution status, BESTPOS p.512", [
        (0, "SOL_COMPUTED", "Solution computed"),
        (1, "INSUFFICIENT_OBS", "Insufficient observations"),
        (2, "NO_CONVERGENCE", "No convergence"),
        (3, "SINGULARITY", "Singularity at parameters matrix"),
        (4, "COV_TRACE", "Covariance trace exceeds maximum"),
        (5, "TEST_DIST", "Test distance exceeded"),
        (6, "COLD_START", "Not converged from cold start"),
        (7, "V_H_LIMIT", "Height or velocity limit exceeded"),
        (8, "VARIANCE", "Variance exceeds limits"),
        (9, "RESIDUALS", "Residuals too large"),
        (13, "INTEGRITY_WARNING", "Large residuals — solution unreliable"),
        (18, "PENDING", "FIX POSITION entered, not yet valid"),
        (19, "INVALID_FIX", "Invalid fixed position"),
        (20, "UNAUTHORIZED", "Position type unauthorized"),
        (22, "INVALID_RATE", "Selected logging rate not supported"),
    ]),
    E("PosVelType", "OEM7 position/velocity type, BESTPOS p.512", [
        (0, "NONE", "No solution"), (1, "FIXEDPOS", "Fixed position"),
        (2, "FIXEDHEIGHT", "Fixed height"),
        (8, "DOPPLER_VELOCITY", "Velocity from instantaneous Doppler"),
        (16, "SINGLE", "Single point"), (17, "PSRDIFF", "Pseudorange differential"),
        (18, "WAAS", "SBAS solution"), (19, "PROPAGATED", "Propagated by Kalman filter"),
        (32, "L1_FLOAT", "Float L1 ambiguity"), (34, "NARROW_FLOAT", "Float narrow-lane"),
        (48, "L1_INT", "Integer L1 ambiguity"), (50, "NARROW_INT", "Integer narrow-lane"),
        (51, "RTK_DIRECT_INS", "RTK seeded by INS"),
        (52, "INS_SBAS", "INS + SBAS"), (53, "INS_PSRSP", "INS + single point"),
        (54, "INS_PSRDIFF", "INS + pseudorange differential"),
        (55, "INS_RTKFLOAT", "INS + RTK float"), (56, "INS_RTKFIXED", "INS + RTK fixed"),
        (68, "PPP_CONVERGING", "PPP converging"), (69, "PPP", "PPP converged"),
        (70, "OPERATIONAL", "Solution operational"),
        (71, "WARNING", "Solution warning"), (72, "OUT_OF_BOUNDS", "Solution out of bounds"),
        (74, "PPP_BASIC_CONVERGING", "PPP basic converging"),
        (75, "PPP_BASIC", "PPP basic converged"),
    ]),
    E("ClockStatus", "OEM7 clock model status, TIME p.955", [
        (0, "VALID", "Clock model is valid"),
        (1, "CONVERGING", "Clock model is nearly valid"),
        (2, "ITERATING", "Clock model is iterating to valid"),
        (3, "INVALID", "Clock model is not valid"),
    ]),
    E("UtcStatus", "OEM7 UTC status, TIME p.955", [
        (0, "INVALID", "UTC time invalid"),
        (1, "VALID", "UTC time valid"),
        (2, "WARNING", "UTC valid but offset may be wrong (no UTC model)"),
    ]),
    E("ComponentType", "OEM7 VERSION component type (subset), p.977", [
        (1, "GPSCARD", "OEM7 receiver card"), (2, "CONTROLLER", "Controller board"),
        (3, "ENCLOSURE", "Enclosure"), (8, "IMUCARD", "IMU card"),
        (15, "USERINFO", "User-set application info"),
        (981, "OEM7FPGA", "OEM7 FPGA version"),
    ]),
    E("SatelliteSystem", "OEM7 satellite system, SATVIS2 p.897", [
        (0, "GPS", "GPS"), (1, "GLONASS", "GLONASS"), (2, "SBAS", "SBAS"),
        (5, "GALILEO", "Galileo"), (6, "BEIDOU", "BeiDou"), (7, "QZSS", "QZSS"),
    ]),
    E("ValidFlag", "Generic OEM7 validity flag", [
        (0, "INVALID", "Not valid / false"), (1, "VALID", "Valid / true"),
    ]),
    E("DatumId", "OEM7 datum (subset), BESTPOS p.512", [
        (61, "WGS84", "World Geodetic System 1984"),
    ]),
    E("TimeStatus", "OEM7 GPS reference time status, p.51", [
        (20, "UNKNOWN", "Time validity unknown"),
        (60, "APPROXIMATE", "Time is set approximately"),
        (80, "COARSEADJUSTING", "Time is approaching coarse precision"),
        (100, "COARSE", "Time valid to coarse precision"),
        (120, "COARSESTEERING", "Time is coarse-set, being steered"),
        (130, "FREEWHEELING", "Position is lost, range bias cannot be calculated"),
        (140, "FINEADJUSTING", "Time is adjusting to fine precision"),
        (160, "FINE", "Time is fine-set"),
        (170, "FINEBACKUPSTEERING", "Time is fine-set, steered by backup system"),
        (180, "FINESTEERING", "Time is fine-set, being steered"),
        (200, "SATTIME", "Time from satellite (only almanac/ephemeris)"),
    ]),
    E("StatusWordSrc", "OEM7 RXSTATUSEVENT status word source, p.887", [
        (0, "ERROR",  "Receiver error word"),
        (1, "STATUS", "Receiver status word"),
        (2, "AUX1",   "Auxiliary 1 status word"),
        (3, "AUX2",   "Auxiliary 2 status word"),
        (4, "AUX3",   "Auxiliary 3 status word"),
        (5, "AUX4",   "Auxiliary 4 status word"),
    ]),
    E("EventType", "OEM7 RXSTATUSEVENT event type, p.887", [
        (0, "CLEAR", "Status bit was cleared"),
        (1, "SET",   "Status bit was set"),
    ]),
]

# parameter-name → enum-type attachment (applied to every GPS message)
ATTACH = {
    "port": "Port", "trigger": "LogTrigger", "hold": "HoldFlag",
    "held": "HoldFlag", "sol_status": "SolStatus", "pos_type": "PosVelType",
    "vel_type": "PosVelType", "clock_status": "ClockStatus",
    "utc_status": "UtcStatus", "comp_type": "ComponentType",
    "component_type": "ComponentType", "datum_id": "DatumId",
    "satellite_system": "SatelliteSystem", "satvis_valid_flag": "ValidFlag",
    "almanac_flag": "ValidFlag",
    # names used by the workbook's new-log sheets
    "position_solution_status": "SolStatus", "position_type": "PosVelType",
    "velocity_solution_status": "SolStatus", "velocity_type": "PosVelType",
    "time_status": "TimeStatus", "system": "SatelliteSystem",
    "status_word": "StatusWordSrc", "event_type": "EventType",
}

# 9 logs to import from the workbook: sheet → (message name, doc page)
NEW_LOGS = {
    "TM_BESTXYZB":  ("BESTXYZ",       "OEM7 BESTXYZ p.530"),
    "TM_RTKPOSB":   ("RTKPOS",        "OEM7 RTKPOS p.859"),
    "TM_PSRPOSB":   ("PSRPOS",        "OEM7 PSRPOS p.767"),
    "TM_TIMESYNCB": ("TIMESYNC",      "OEM7 TIMESYNC p.958"),
    "TM_PSRDOP2B":  ("PSRDOP2",       "OEM7 PSRDOP2 p.765"),
    "TM_RTKDOPB":   ("RTKDOP",        "OEM7 RTKDOP p.855"),
    "TM_RXSTSEVTB": ("RXSTATUSEVENT", "OEM7 RXSTATUSEVENT p.887"),
    "TM_SATVIS2B":  ("SATVIS2",       "OEM7 SATVIS2 p.897"),
    "TM_RANGEB":    ("RANGE",         "OEM7 RANGE p.799"),
}
# existing logs to cross-check: sheet → message name
CHECK_LOGS = {
    "TM_BESTPOSB":   "BESTPOS",  "TM_BESTVELB": "BESTVEL",
    "TM_TIMEB":      "TIME",     "TM_PSRDOPB":  "PSRDOP",
    "TM_RXSTATUSB":  "RXSTATUS", "TM_HWMONITORB": "HWMONITOR",
    "TM_VERSIONB":   "VERSION",
}

RE_OFF = re.compile(r"H\+(\d+)")
RE_CHARN = re.compile(r"Char\[(\d+)\]", re.I)

def map_type(t: str):
    t = (t or "").strip()
    if RE_CHARN.match(t):                      return "STRING"
    tl = t.lower()
    if tl.startswith("enum"):                  return "ENUM"
    if "double" in tl:                         return "DOUBLE"
    if "float" in tl:                          return "FLOAT"
    if "hex" in tl:                            return "HEX"
    if tl.startswith(("ulong", "ushort", "uchar", "gpsec")): return "UINT"
    if tl.startswith(("long", "short")):       return "INT"
    if tl.startswith("char"):                  return "INT"
    return "UINT"

def parse_payload_rows(ws):
    """Yield (payload_offset, name, dtype_raw, size, desc) from the
    'TM Payload'/'TC Payload' section until the Tail/CRC section."""
    rows = list(ws.iter_rows(values_only=True))
    in_pay = False
    for row in rows:
        a = str(row[0] or "")
        if a.startswith(("TM Payload", "TC Payload")):
            in_pay = True; continue
        if in_pay and a.startswith(("TM Tail", "TC Tail")):
            break
        if not in_pay: continue
        if a.strip() in ("", "Frame Offset"): continue
        m = RE_OFF.search(a)
        if not m:
            # TC sheets use absolute offsets (e.g. '28')
            if a.strip().isdigit():
                off = int(a.strip()) - 28
            else:
                continue
        else:
            off = int(m.group(1))
        name = str(row[1] or "").strip()
        dtype = str(row[2] or "").strip()
        size_s = str(row[3] or "").strip()
        sm = re.search(r"(\d+)", size_s)
        size = int(sm.group(1)) if sm else 0
        desc = str(row[6] or "").strip()
        if name:
            yield off, name, dtype, size, desc

def build_message(ws, name, wire_id, variable, docref, descr):
    params, seq = [], 0
    count_seen = False
    for off, pname, dtype_raw, size, desc in parse_payload_rows(ws):
        seq += 1
        is_arr = False
        if variable:
            if count_seen:
                is_arr = True
            if pname.startswith("num_"):
                count_seen = True
        if dtype_raw.endswith("[]"):
            is_arr = True
        params.append({
            "seq": seq, "offset": off, "length": size, "name": pname,
            "data_type": map_type(dtype_raw), "enum": None, "unit": None,
            "scale": None, "description": desc,
            "is_array": is_arr, "array_count": None,
        })
    return {
        "direction": "TM", "msg_id": wire_id, "wire_id": wire_id,
        "name": name, "node": "GPS", "length_bytes": None,
        "is_variable_length": bool(variable), "is_config": False,
        "request_via": "LOG", "offset_unit": "bytes",
        "doc_reference": docref, "description": descr, "parameters": params,
    }

def main():
    wb_path  = sys.argv[1] if len(sys.argv) > 1 else \
        "/mnt/user-data/uploads/GNSS_LOG_TC_once_TM_16commands_breakdown.xlsx"
    mib_path = Path(sys.argv[2] if len(sys.argv) > 2 else "mib/gps.json")

    wb = openpyxl.load_workbook(wb_path, data_only=True)
    base = json.loads(mib_path.read_text())

    # TM_Summary: name → (wire_id, payload_len, variable)
    summary = {}
    for row in wb["TM_Summary"].iter_rows(values_only=True):
        n = str(row[0] or "")
        if not n.endswith("B") or n.startswith(("Actual", "Telemetry")): continue
        idb = str(row[2] or "")           # 'AA BB' little-endian bytes
        parts = idb.split()
        if len(parts) != 2: continue
        wid = int(parts[0], 16) | (int(parts[1], 16) << 8)
        variable = "Yes" in str(row[8] or "")
        summary[n] = (wid, int(row[4] or 0), variable)

    existing = {m["name"]: m for m in base["messages"]}

    # sheet tab names that differ from their TM_Summary row name
    SHEET2SUMMARY = {"TM_RXSTSEVTB": "RXSTATUSEVENTB"}

    # 1. add the 9 new logs (skip any already present — safe to re-run)
    added = []
    for sheet, (mname, docref) in NEW_LOGS.items():
        if mname in existing:
            print(f"  (skip {mname}: already in Database)")
            continue
        wid, plen, variable = summary[SHEET2SUMMARY.get(sheet, sheet.replace("TM_", ""))]
        ws = wb[sheet]
        descr = str(list(ws.iter_rows(values_only=True))[1][0] or "").strip()
        msg = build_message(ws, mname, wid, variable, docref, descr)
        base["messages"].append(msg)
        added.append((mname, wid, len(msg["parameters"]), variable))

    # 2. cross-check the 7 verified logs (report only)
    print("── cross-check: verified logs vs workbook ──")
    for sheet, mname in CHECK_LOGS.items():
        ex = existing.get(mname)
        sheet_fields = list(parse_payload_rows(wb[sheet]))
        exp = [(p["offset"], p["length"]) for p in ex["parameters"]]
        got = [(o, s) for o, _, _, s, _ in sheet_fields]
        status = "MATCH" if exp == got else "DIFFERS"
        print(f"  {mname:10s} db={len(exp):2d} fields, sheet={len(got):2d} → {status}")
        if status == "DIFFERS":
            for i, (e, g) in enumerate(zip(exp, got)):
                if e != g:
                    print(f"      field {i+1}: db off/len={e}  sheet={g}  "
                          f"(db '{ex['parameters'][i]['name']}' vs sheet "
                          f"'{sheet_fields[i][1]}')")
            if len(exp) != len(got):
                print(f"      field-count mismatch: db={len(exp)} sheet={len(got)}")

    # 3. enum layer: merge tables, attach refs
    have = {e["name"] for e in base.get("enums", [])}
    for e in OEM7_ENUMS:
        if e["name"] not in have:
            base.setdefault("enums", []).append(e)
    attached, left = 0, 0
    for m in base["messages"]:
        for p in m["parameters"]:
            if p.get("data_type") == "ENUM" and not p.get("enum"):
                ref = ATTACH.get(p["name"].strip().lower())
                if ref: p["enum"] = ref; attached += 1
                else:   left += 1; print(f"  !! no enum map for {m['name']}::{p['name']}")

    # TM block sorted by wire_id, TC block first (preserve original TC order)
    tcs = [m for m in base["messages"] if m["direction"] == "TC"]
    tms = sorted([m for m in base["messages"] if m["direction"] == "TM"],
                 key=lambda m: m["wire_id"])
    base["messages"] = tcs + tms

    mib_path.write_text(json.dumps(base, indent=1) + "\n")
    print("── added logs ──")
    for n, w, np, var in added:
        print(f"  {n:14s} wire_id={w:5d} params={np:2d} variable={var}")
    print(f"── enums: {len(base['enums'])} types, {attached} refs attached, "
          f"{left} unmapped ──")
    print(f"wrote {mib_path}")

if __name__ == "__main__":
    main()
