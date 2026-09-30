#!/usr/bin/env python3
"""
patch_eps_enums.py — fill the missing ENUM layer in mib/eps.json.

The EPS workbook (EPS_subsystem_TC_TM_colored_.xlsx) carries the enum
value maps inline in its Value column; the original extractor emitted
'enums': [] and left every ENUM-typed parameter without an 'enum' ref.
This script is the deterministic fix: it adds the enum TYPES extracted
from the sheet (idempotent — existing types are kept untouched) and
attaches 'enum' refs to ENUM-typed parameters by exact parameter name.

Value maps below are transcribed from these sheet rows:
  EP-001  TM_EPS_GET_FW_VER_RSP        EPS Model / Execution Mode
  EP-009  TC_SET_PDU_OBC_PARAMETERS    Primary OBC selection (1/2)
  EP-013  TC_PDU_FLASH_DATA_SAVE_RESET Save/Reset (0/1) + RSP Status
  EP-015  TC_GET_PDU_ALTR_FLAG_RSP     EPS Mode (0..4) + HDRM (0/1)
  EP-020  BMS heaters                  Batt Heating Mode (0/1)
  EP-025  TC_SET_RTC_TIME              Weekday (0=Monday .. 6=Sunday)

Usage: python3 tools/patch_eps_enums.py [mib/eps.json]
"""
from __future__ import annotations
import json, sys
from pathlib import Path

def E(name, doc, vals):
    return {"name": name, "doc_reference": doc,
            "values": [{"value": v, "name": n, "description": d} for v, n, d in vals]}

SHEET_ENUMS = [
    E("SuccessFail", "EPS sheet — response Status fields (EP-009/013/025/027)", [
        (0, "Failure", "Command rejected / failed"),
        (1, "Success", "Command accepted / applied"),
    ]),
    E("ObcSelect", "EPS sheet EP-009 — Primary OBC selection", [
        (1, "OBC1", "Select / active OBC 1"),
        (2, "OBC2", "Select / active OBC 2"),
    ]),
    E("SaveReset", "EPS sheet EP-013/EP-027 — flash Save/Reset", [
        (0, "Reset", "Reset stored flash data"),
        (1, "Save", "Save current values to flash"),
    ]),
    E("EpsMode", "EPS sheet EP-015 — EPS operating mode", [
        (0, "All_OP_Off", "All outputs off"),
        (1, "Critical",   "Critical mode"),
        (2, "Safe",       "Safe mode"),
        (3, "Normal",     "Normal mode"),
        (4, "Full",       "Full mode"),
    ]),
    E("HeaterMode", "EPS sheet EP-020 — battery heating mode", [
        (0, "Automatic", "Thermostatic control"),
        (1, "Manual",    "Manual heater flags"),
    ]),
    E("Weekday", "EPS sheet EP-025 — RTC weekday", [
        (0, "Monday", ""), (1, "Tuesday", ""), (2, "Wednesday", ""),
        (3, "Thursday", ""), (4, "Friday", ""), (5, "Saturday", ""),
        (6, "Sunday", ""),
    ]),
    E("HdrmStatus", "EPS sheet EP-015 — HDRM release status", [
        (0, "Disconnected", "HDRM disconnected"),
        (1, "Connected",    "HDRM connected"),
    ]),
]

# exact parameter name → enum type   (applied only where data_type==ENUM
# and no ref is present, so re-running is a no-op)
ATTACH = {
    "EPS Model/Satellite platform": "EPSModel",
    "Execution Mode":               "ExecutionMode",
    "Status":                       "SuccessFail",
    "Primary OBC selection":        "ObcSelect",
    "OBC_Primary_Channel":          "ObcSelect",
    "Active OBC":                   "ObcSelect",
    "Save/Reset":                   "SaveReset",
    "EPS Mode":                     "EpsMode",
    "Batt Heating Mode":            "HeaterMode",
    "Weekday":                      "Weekday",
    "RTC_Week":                     "Weekday",
    "HDRM Release Status":          "HdrmStatus",
}

def main():
    path = Path(sys.argv[1] if len(sys.argv) > 1 else "mib/eps.json")
    m = json.loads(path.read_text())

    have = {e["name"] for e in m.get("enums", [])}
    added_types = []
    for e in SHEET_ENUMS:
        if e["name"] not in have:
            m.setdefault("enums", []).append(e)
            added_types.append(e["name"])

    known = {e["name"] for e in m["enums"]}
    attached, leftover = [], []
    for msg in m["messages"]:
        for p in msg["parameters"]:
            if p.get("data_type") != "ENUM" or p.get("enum"):
                continue
            ref = ATTACH.get(p["name"].strip())
            if ref and ref in known:
                p["enum"] = ref
                attached.append((msg["name"], p["name"], ref))
            else:
                leftover.append((msg["name"], p["name"]))

    path.write_text(json.dumps(m, indent=1) + "\n")
    print(f"enum types added : {added_types or '(none — already present)'}")
    print(f"refs attached    : {len(attached)}")
    for mn, pn, ref in attached:
        print(f"   {mn:34s} :: {pn:32s} → {ref}")
    if leftover:
        print(f"STILL UNMAPPED   : {len(leftover)}")
        for mn, pn in leftover:
            print(f"   !! {mn} :: {pn}")
    else:
        print("STILL UNMAPPED   : 0  — every ENUM parameter now has a value map")

if __name__ == "__main__":
    main()
