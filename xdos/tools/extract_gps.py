#!/usr/bin/env python3
"""
extract_gps.py — GPS (NovAtel OEM7 / OEM719) log and command
definitions for the operational subset used by the ground station.

Source of truth
---------------
These definitions are transcribed from the EXISTING, VERIFIED C++
reference implementation (gps_types.hpp + gps_parser.cpp), which was
itself derived from the OEM7 Commands and Logs Reference Manual and
proven against real receiver output. Per the project decision to use
the C++ codebase as the authoritative protocol reference, this is the
reliable source for GPS — far more so than positionally parsing the
~900-entry OEM7 PDF.

Scope: the 7 logs the ground station actually uses, plus the LOG
command used to request them. Adding more OEM7 logs later is a matter
of appending to LOGS below.

OEM7 binary notes carried into the MIB:
  - offsets are bytes, relative to the end of the 28-byte log header
  - all multi-byte fields are little-endian
  - logs are requested via the LOG command, not sent as direct TCs
  - HWMONITOR / PSRDOP / VERSION have trailing variable-length arrays
"""

from __future__ import annotations
from dataclasses import dataclass, field
from typing import Optional


@dataclass
class Param:
    seq: int
    offset: Optional[int]   # bytes from header end; None if it follows a var array
    length: int
    name: str
    data_type: str
    description: str = ''
    is_array: int = 0
    array_count: Optional[int] = None


@dataclass
class Message:
    name: str
    direction: str          # 'TC' | 'TM'
    msg_id: int
    length_bytes: Optional[int]
    is_variable_length: int
    request_via: Optional[str]
    description: str = ''
    doc_reference: str = ''
    params: list = field(default_factory=list)


def _p(seq, off, ln, name, dt, desc='', arr=0, ac=None):
    return Param(seq, off, ln, name, dt, desc, arr, ac)


# ---------------------------------------------------------------------------
#  LOGS (telemetry) — fixed and variable-length
# ---------------------------------------------------------------------------

BESTPOS = Message(
    'BESTPOS', 'TM', 42, 72, 0, 'LOG',
    'Best position computed by the receiver', 'OEM7 3.21', params=[
        _p(1,  0, 4, 'sol_status',     'ENUM',   'Solution status'),
        _p(2,  4, 4, 'pos_type',       'ENUM',   'Position type'),
        _p(3,  8, 8, 'lat',            'DOUBLE', 'Latitude (degrees)'),
        _p(4, 16, 8, 'lon',            'DOUBLE', 'Longitude (degrees)'),
        _p(5, 24, 8, 'hgt',            'DOUBLE', 'Height above MSL (m)'),
        _p(6, 32, 4, 'undulation',     'FLOAT',  'Geoid undulation (m)'),
        _p(7, 36, 4, 'datum_id',       'ENUM',   'Datum ID (61 = WGS84)'),
        _p(8, 40, 4, 'lat_std',        'FLOAT',  'Latitude std deviation (m)'),
        _p(9, 44, 4, 'lon_std',        'FLOAT',  'Longitude std deviation (m)'),
        _p(10,48, 4, 'hgt_std',        'FLOAT',  'Height std deviation (m)'),
        _p(11,52, 4, 'stn_id',         'CHAR',   'Base station ID'),
        _p(12,56, 4, 'diff_age',       'FLOAT',  'Differential age (s)'),
        _p(13,60, 4, 'sol_age',        'FLOAT',  'Solution age (s)'),
        _p(14,64, 1, 'num_svs',        'UINT',   'Satellites tracked'),
        _p(15,65, 1, 'num_soln_svs',   'UINT',   'Satellites used in solution'),
        _p(16,66, 1, 'num_soln_l1',    'UINT',   'Sats with L1/E1/B1 in solution'),
        _p(17,67, 1, 'num_soln_multi', 'UINT',   'Sats with multi-freq in solution'),
        _p(18,68, 1, 'reserved',       'HEX',    'Reserved'),
        _p(19,69, 1, 'ext_sol_stat',   'HEX',    'Extended solution status'),
        _p(20,70, 1, 'galileo_beidou_mask', 'HEX', 'Galileo/BeiDou signal-used mask'),
        _p(21,71, 1, 'gps_glonass_mask',    'HEX', 'GPS/GLONASS signal-used mask'),
    ])

BESTVEL = Message(
    'BESTVEL', 'TM', 99, 44, 0, 'LOG',
    'Best available velocity data', 'OEM7 3.24', params=[
        _p(1,  0, 4, 'sol_status', 'ENUM',   'Solution status'),
        _p(2,  4, 4, 'vel_type',   'ENUM',   'Velocity type'),
        _p(3,  8, 4, 'latency',    'FLOAT',  'Velocity latency (s)'),
        _p(4, 12, 4, 'age',        'FLOAT',  'Differential age (s)'),
        _p(5, 16, 8, 'hor_spd',    'DOUBLE', 'Horizontal speed (m/s)'),
        _p(6, 24, 8, 'trk_gnd',    'DOUBLE', 'Track over ground (deg, True North)'),
        _p(7, 32, 8, 'vert_spd',   'DOUBLE', 'Vertical speed (m/s)'),
        _p(8, 40, 4, 'reserved',   'FLOAT',  'Reserved'),
    ])

TIME = Message(
    'TIME', 'TM', 101, 44, 0, 'LOG',
    'Receiver time information', 'OEM7 3.211', params=[
        _p(1,  0, 4, 'clock_status', 'ENUM',   'Clock model status'),
        _p(2,  4, 8, 'offset',       'DOUBLE', 'Receiver clock offset (s)'),
        _p(3, 12, 8, 'offset_std',   'DOUBLE', 'Clock offset std deviation (s)'),
        _p(4, 20, 8, 'utc_offset',   'DOUBLE', 'GPS-to-UTC offset (s)'),
        _p(5, 28, 4, 'utc_year',     'UINT',   'UTC year'),
        _p(6, 32, 1, 'utc_month',    'UINT',   'UTC month'),
        _p(7, 33, 1, 'utc_day',      'UINT',   'UTC day'),
        _p(8, 34, 1, 'utc_hour',     'UINT',   'UTC hour'),
        _p(9, 35, 1, 'utc_min',      'UINT',   'UTC minute'),
        _p(10,36, 4, 'utc_ms',       'UINT',   'UTC millisecond'),
        _p(11,40, 4, 'utc_status',   'ENUM',   'UTC status (0=Invalid,1=Valid,2=Warning)'),
    ])

RXSTATUS = Message(
    'RXSTATUS', 'TM', 93, 88, 0, 'LOG',
    'Receiver status word and error word', 'OEM7 3.178', params=[
        _p(1,  0, 4, 'error',        'HEX',  'Receiver error word (0 = healthy)'),
        _p(2,  4, 4, 'num_stats',    'UINT', 'Number of status blocks'),
        _p(3,  8, 4, 'rx_stat',      'HEX',  'Receiver status word'),
        _p(4, 12, 4, 'rx_stat_pri',  'HEX',  'Receiver status priority mask'),
        _p(5, 16, 4, 'rx_stat_set',  'HEX',  'Receiver status event-set mask'),
        _p(6, 20, 4, 'rx_stat_clr',  'HEX',  'Receiver status event-clear mask'),
        _p(7, 24, 4, 'aux1_stat',    'HEX',  'Aux1 status word'),
        _p(8, 28, 4, 'aux1_pri',     'HEX',  'Aux1 priority mask'),
        _p(9, 32, 4, 'aux1_set',     'HEX',  'Aux1 event-set mask'),
        _p(10,36, 4, 'aux1_clr',     'HEX',  'Aux1 event-clear mask'),
        _p(11,40, 4, 'aux2_stat',    'HEX',  'Aux2 status word'),
        _p(12,44, 4, 'aux2_pri',     'HEX',  'Aux2 priority mask'),
        _p(13,48, 4, 'aux2_set',     'HEX',  'Aux2 event-set mask'),
        _p(14,52, 4, 'aux2_clr',     'HEX',  'Aux2 event-clear mask'),
        _p(15,56, 4, 'aux3_stat',    'HEX',  'Aux3 status word'),
        _p(16,60, 4, 'aux3_pri',     'HEX',  'Aux3 priority mask'),
        _p(17,64, 4, 'aux3_set',     'HEX',  'Aux3 event-set mask'),
        _p(18,68, 4, 'aux3_clr',     'HEX',  'Aux3 event-clear mask'),
        _p(19,72, 4, 'aux4_stat',    'HEX',  'Aux4 status word'),
        _p(20,76, 4, 'aux4_pri',     'HEX',  'Aux4 priority mask'),
        _p(21,80, 4, 'aux4_set',     'HEX',  'Aux4 event-set mask'),
        _p(22,84, 4, 'aux4_clr',     'HEX',  'Aux4 event-clear mask'),
    ])

# Variable-length: 4-byte count, then `num_measurements` x 8-byte records
HWMONITOR = Message(
    'HWMONITOR', 'TM', 963, None, 1, 'LOG',
    'Monitored hardware values (temperatures, voltages, currents)',
    'OEM7 3.81', params=[
        _p(1, 0, 4, 'num_measurements', 'UINT', 'Number of readings that follow'),
        _p(2, 4, 4, 'reading_value',  'FLOAT', 'Measurement value', arr=1),
        _p(3, 8, 4, 'reading_status', 'HEX',
           'Status: bits[8..15]=type, bits[0..7]=limit status', arr=1),
    ])

# Variable-length: 28 fixed bytes, 4-byte count, then `num_prn` x 4-byte PRNs
PSRDOP = Message(
    'PSRDOP', 'TM', 174, None, 1, 'LOG',
    'Pseudorange dilution of precision', 'OEM7 3.135', params=[
        _p(1,  0, 4, 'gdop',    'FLOAT', 'Geometric DOP'),
        _p(2,  4, 4, 'pdop',    'FLOAT', 'Position DOP'),
        _p(3,  8, 4, 'hdop',    'FLOAT', 'Horizontal DOP'),
        _p(4, 12, 4, 'htdop',   'FLOAT', 'Horizontal + time DOP'),
        _p(5, 16, 4, 'tdop',    'FLOAT', 'Time DOP'),
        _p(6, 20, 4, 'cutoff',  'FLOAT', 'Elevation cut-off angle (deg)'),
        _p(7, 24, 4, 'num_prn', 'UINT',  'Number of PRNs that follow'),
        _p(8, 28, 4, 'prn',     'UINT',  'PRN of satellite in solution', arr=1),
    ])

# Variable-length: 4-byte count, then `num_components` x 108-byte records
VERSION = Message(
    'VERSION', 'TM', 37, None, 1, 'LOG',
    'Receiver hardware and firmware version information',
    'OEM7 3.223', params=[
        _p(1,   0, 4,  'num_components', 'INT',  'Number of components that follow'),
        _p(2,   4, 4,  'comp_type',     'ENUM', 'Component type', arr=1),
        _p(3,   8, 16, 'model',         'CHAR', 'Firmware model number', arr=1),
        _p(4,  24, 16, 'psn',           'CHAR', 'Product serial number', arr=1),
        _p(5,  40, 16, 'hw_version',    'CHAR', 'Hardware version string', arr=1),
        _p(6,  56, 16, 'sw_version',    'CHAR', 'Firmware/software version', arr=1),
        _p(7,  72, 16, 'boot_version',  'CHAR', 'Boot code version', arr=1),
        _p(8,  88, 12, 'comp_date',     'CHAR', 'Compile date (YYYY/Mmm/DD)', arr=1),
        _p(9, 100, 12, 'comp_time',     'CHAR', 'Compile time (HH:MM:SS)', arr=1),
    ])

LOGS = [BESTPOS, BESTVEL, TIME, RXSTATUS, HWMONITOR, PSRDOP, VERSION]

# ---------------------------------------------------------------------------
#  COMMANDS — the operational subset
# ---------------------------------------------------------------------------

LOG_CMD = Message(
    'LOG', 'TC', 1, None, 1, None,
    'Request a log; specifies which log, port and trigger/rate',
    'OEM7 2.82', params=[
        _p(1, 0, 4, 'port',     'ENUM',  'Output port for the log'),
        _p(2, 4, 2, 'message_id','UINT', 'Message ID of the log to output'),
        _p(3, 6, 1, 'message_type','HEX','Message type (binary/ascii, response bit)'),
        _p(4, 7, 1, 'reserved', 'HEX',   'Reserved'),
        _p(5, 8, 4, 'trigger',  'ENUM',  'Trigger type (ONTIME, ONNEW, ONCE, ...)'),
        _p(6,12, 8, 'period',   'DOUBLE','Logging period in seconds (for ONTIME)'),
        _p(7,20, 8, 'offset',   'DOUBLE','Offset within the period (s)'),
        _p(8,28, 4, 'hold',     'ENUM',  'HOLD flag (prevents UNLOGALL clearing it)'),
    ])

UNLOG = Message(
    'UNLOG', 'TC', 36, None, 0, None,
    'Stop a specific log from being output', 'OEM7 2.206', params=[
        _p(1, 0, 4, 'port',       'ENUM', 'Port the log is on'),
        _p(2, 4, 2, 'message_id', 'UINT', 'Message ID of the log to stop'),
        _p(3, 6, 1, 'message_type','HEX', 'Message type'),
        _p(4, 7, 1, 'reserved',   'HEX',  'Reserved'),
    ])

UNLOGALL = Message(
    'UNLOGALL', 'TC', 38, None, 0, None,
    'Stop all logs that are currently being output', 'OEM7 2.207', params=[
        _p(1, 0, 4, 'port', 'ENUM', 'Port to clear (or ALL_PORTS)'),
        _p(2, 4, 4, 'held', 'ENUM', 'Also clear logs with the HOLD flag set'),
    ])

COMMANDS = [LOG_CMD, UNLOG, UNLOGALL]


def extract() -> dict:
    return {'subsystem': 'GPS', 'protocol': 'oem7_binary',
            'offset_unit': 'bytes',
            'messages': LOGS + COMMANDS, 'enums': [], 'skipped': []}


if __name__ == '__main__':
    r = extract()
    tc = sum(1 for m in r['messages'] if m.direction == 'TC')
    tm = sum(1 for m in r['messages'] if m.direction == 'TM')
    npar = sum(len(m.params) for m in r['messages'])
    nvar = sum(1 for m in r['messages'] if m.is_variable_length)
    print(f"GPS: {tc} TC, {tm} TM ({nvar} variable-length), {npar} params")
    for m in r['messages']:
        flag = ' [var]' if m.is_variable_length else ''
        print(f"  [{m.direction}] {m.name} id={m.msg_id} "
              f"len={m.length_bytes} params={len(m.params)}{flag}")
