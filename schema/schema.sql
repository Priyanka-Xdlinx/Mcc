-- MCC decode schema.
--
-- Generated into SQLite by schema/build_schema.py, which reads mib/*.json
-- directly.  It deliberately does NOT read mission_parameter_dictionary.csv:
-- that flattened export has no `endian` column at all, and its `scale` column
-- is a divisor for ADCS but a multiplier for EPS/XLINK-S.  Both facts are
-- unrecoverable from the CSV, and both are why the previous hand-generated
-- contract had to invent a scale heuristic and hard-code little-endian.
--
-- Layout: Layer 2 (what a message means) + Layer 1 (which bytes on the wire
-- belong to which message) + the decoded telemetry itself.

PRAGMA foreign_keys = ON;

-- ── Layer 2 — the mission database ──────────────────────────────────────────

CREATE TABLE subsystem (
    id          INTEGER PRIMARY KEY,
    name        TEXT    NOT NULL UNIQUE,   -- exact MIB spelling: 'XLINK-S', 'Payload', 'SENSOR'
    apid        INTEGER,
    protocol    TEXT,
    source_file TEXT
);

CREATE TABLE enum_type (
    id           INTEGER PRIMARY KEY,
    subsystem_id INTEGER NOT NULL REFERENCES subsystem(id),
    name         TEXT    NOT NULL,
    -- Enums are scoped per subsystem; two subsystems may reuse a name.
    UNIQUE (subsystem_id, name)
);

CREATE TABLE enum_value (
    enum_type_id INTEGER NOT NULL REFERENCES enum_type(id),
    value        INTEGER NOT NULL,
    label        TEXT,
    description  TEXT,
    PRIMARY KEY (enum_type_id, value)
);

CREATE TABLE message (
    id                 INTEGER PRIMARY KEY,
    subsystem_id       INTEGER NOT NULL REFERENCES subsystem(id),
    name               TEXT    NOT NULL,
    direction          TEXT    NOT NULL CHECK (direction IN ('TC','TM','EVENT')),
    msg_id             INTEGER,
    wire_id            INTEGER,
    node               TEXT,
    offset_unit        TEXT    NOT NULL CHECK (offset_unit IN ('bits','bytes')),
    length_bytes       INTEGER,            -- NULL = variable length
    is_variable_length INTEGER NOT NULL DEFAULT 0,
    -- direction MUST be part of the key: 41 ADCS names exist as both TC and TM,
    -- and a lookup that drops it silently resolves to the wrong message.
    UNIQUE (subsystem_id, name, direction)
);

CREATE TABLE parameter (
    id           INTEGER PRIMARY KEY,
    message_id   INTEGER NOT NULL REFERENCES message(id),
    seq          INTEGER NOT NULL,
    name         TEXT    NOT NULL,
    offset       INTEGER,                  -- in the message's offset_unit
    length       INTEGER NOT NULL,         -- in the message's offset_unit
    bit_offset   INTEGER,                  -- normalised: absolute bits from payload start
    bit_length   INTEGER,                  -- normalised: width in bits
    byte_aligned INTEGER NOT NULL,
    data_type    TEXT    NOT NULL,
    endian       TEXT    NOT NULL DEFAULT 'little' CHECK (endian IN ('little','big')),
    scale        REAL,                     -- ALWAYS a multiplier: engineering = raw * scale
    unit         TEXT,
    enum_type_id INTEGER REFERENCES enum_type(id),
    is_array     INTEGER NOT NULL DEFAULT 0,
    array_count  INTEGER,
    description  TEXT,
    UNIQUE (message_id, seq)
);

-- Parameter names are NOT unique on their own: 755 downlinked slots use only
-- 712 distinct names ('Time integer seconds' appears in 13 messages).
CREATE INDEX ix_param_name ON parameter(name);
CREATE INDEX ix_param_msg  ON parameter(message_id, seq);

-- ── Layer 1 — the wire: which bytes belong to which message ─────────────────

CREATE TABLE tier (
    name         TEXT    PRIMARY KEY,      -- VH | H | M | L
    seg_offset   INTEGER NOT NULL,         -- byte offset inside the 1280-byte subframe
    seg_bytes    INTEGER NOT NULL,
    block_span   INTEGER NOT NULL,         -- subframes needed to complete the block
    block_budget INTEGER NOT NULL
);

CREATE TABLE lut (
    id             INTEGER PRIMARY KEY CHECK (id = 1),
    crc16          INTEGER NOT NULL,       -- must match the table the OBC is flying
    format_version INTEGER NOT NULL,
    subframe_size  INTEGER NOT NULL,
    source_file    TEXT,
    loaded_at      TEXT
);

CREATE TABLE lut_entry (
    id           INTEGER PRIMARY KEY,
    tier         TEXT    NOT NULL REFERENCES tier(name),
    entry_index  INTEGER NOT NULL,         -- position within the tier: the only OBC link
    tc_id        TEXT    NOT NULL,         -- 'AD-150'
    subsystem_id INTEGER NOT NULL REFERENCES subsystem(id),
    rsp          TEXT    NOT NULL,         -- response message name
    apid         INTEGER,
    tm_raw_len   INTEGER NOT NULL,
    message_id   INTEGER REFERENCES message(id),   -- resolved TM message
    UNIQUE (tier, entry_index)
);

CREATE TABLE pick (
    id       INTEGER PRIMARY KEY,
    entry_id INTEGER NOT NULL REFERENCES lut_entry(id),
    seq      INTEGER NOT NULL,
    src      INTEGER NOT NULL,             -- byte offset inside the response
    size     INTEGER NOT NULL,
    block    INTEGER NOT NULL,             -- ABSOLUTE byte offset inside the tier block
    UNIQUE (entry_id, seq)
);

CREATE TABLE pick_param (
    pick_id      INTEGER NOT NULL REFERENCES pick(id),
    seq          INTEGER NOT NULL,
    parameter_id INTEGER REFERENCES parameter(id),
    name         TEXT    NOT NULL,
    PRIMARY KEY (pick_id, seq)
);

-- ── Decoded telemetry ───────────────────────────────────────────────────────
-- quality is first-class because the wire has sentinels the old schema could
-- not express:
--   OK               decoded normally
--   NO_ANSWER        every byte of the field was 0xFF - the subsystem did not reply
--   NOT_DOWNLINKED   no pick claimed these bytes; they are zero fill, not data
--   BLOCK_INCOMPLETE a subframe slice of this tier block never arrived
--   WARMUP           first master frame after boot - values are not trustworthy

CREATE TABLE telemetry_latest (
    parameter_id    INTEGER PRIMARY KEY REFERENCES parameter(id),
    entry_id        INTEGER REFERENCES lut_entry(id),
    master_frame    INTEGER,
    subframe        INTEGER,
    spacecraft_time TEXT,                  -- CCS time copied from the TM header, verbatim
    received_at     TEXT,
    raw_hex         TEXT,
    value_num       REAL,
    value_text      TEXT,
    quality         TEXT NOT NULL
);

-- `session` separates runs. The OBC's master-frame counter restarts at 0 on
-- every boot, so without it two runs interleave and a chart plots a sawtooth
-- across a discontinuity that never happened.
CREATE TABLE telemetry_history (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    session         TEXT    NOT NULL DEFAULT '',
    parameter_id    INTEGER NOT NULL REFERENCES parameter(id),
    master_frame    INTEGER,
    subframe        INTEGER,
    spacecraft_time TEXT,
    received_at     TEXT,
    value_num       REAL,
    value_text      TEXT,
    quality         TEXT NOT NULL
);

CREATE INDEX ix_hist_param ON telemetry_history(parameter_id, id DESC);
CREATE INDEX ix_hist_frame ON telemetry_history(master_frame, subframe);
CREATE INDEX ix_hist_session ON telemetry_history(session, parameter_id, id);

-- Display pages. Operator-owned, not derived from the MIB: the backend also
-- creates these at startup (store/DisplayRepo) so an existing mcc.db gains them
-- without a rebuild that would throw away telemetry history.
CREATE TABLE IF NOT EXISTS app_user (
  id         INTEGER PRIMARY KEY,
  name       TEXT NOT NULL UNIQUE COLLATE NOCASE,
  created_at TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS display_page (
  id         INTEGER PRIMARY KEY,
  owner_id   INTEGER NOT NULL REFERENCES app_user(id),
  name       TEXT NOT NULL,
  params     TEXT NOT NULL,     -- JSON array, exactly 20 entries, each a parameter id (int) or null
  created_at TEXT NOT NULL,
  updated_at TEXT NOT NULL,
  UNIQUE (owner_id, name)
);
CREATE TABLE IF NOT EXISTS user_workspace (
  user_id    INTEGER PRIMARY KEY REFERENCES app_user(id),
  layout     TEXT NOT NULL,     -- JSON, opaque to the backend (validated only as JSON, <= 64 KB)
  updated_at TEXT NOT NULL
);
