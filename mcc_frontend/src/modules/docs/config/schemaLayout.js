/**
 * How the live schema is grouped and drawn on the Docs page, as
 * GET /api/schema returns it: two SQLite files (mcc.db, mcc-auth.db), shown as
 * one list of flat table names. Tables not listed here are neither drawn nor
 * carded.
 */
export const GROUPS = [
  {
    title: "Mission database (MIB)",
    note: "What each message and field means",
    tables: ["subsystem", "enum_type", "enum_value", "message", "parameter"],
  },
  {
    title: "Downlink map (LUT)",
    note: "Which bytes belong to which message",
    tables: ["tier", "lut", "lut_entry", "pick", "pick_param"],
  },
  { title: "Telemetry", note: "The decoded values", tables: ["telemetry_latest", "telemetry_history"] },
  { title: "Accounts", note: "Who can sign in (mcc-auth.db)", tables: ["account", "auth_session", "auth_key"] },
  { title: "Display pages", note: "Saved pages and window layouts", tables: ["display_page", "account_workspace"] },
];

export const TABLE_NOTES = {
  subsystem: "One row per subsystem in the mission database, with its exact MIB spelling (ADCS, EPS, GPS, Payload, SENSOR, UHF, XLINK-S).",
  enum_type: "Named enumerations, scoped per subsystem (two subsystems may reuse a name).",
  enum_value: 'The labels of each enumeration, e.g. 3 → "Aborted".',
  message: "Telemetry messages from the MIB. Direction is part of the key: 41 ADCS names exist as both TC and TM.",
  parameter: "Every field of every message: offset and length normalised to bits, data type, endianness, scale (always a multiplier), unit, enum.",
  tier: "The four fixed parts of the payload: byte offset, size and how many subframes make one block.",
  lut: "The lookup table in flight: its CRC-16 must match the one the OBC uses, or the backend refuses to decode.",
  lut_entry: "The 64 LUT entries: which subsystem reply goes into which tier, and at which position.",
  pick: "Byte runs copied from a reply into the tier block, at absolute block offsets.",
  pick_param: "The parameters inside each pick: the 755 downlinked parameter slots.",
  telemetry_latest: "The newest reading of each parameter, overwritten in place.",
  telemetry_history: "Readings over time, tagged with the run (session). Capped at 500,000 rows and pruned every 30 s.",
  account: "Sign-in accounts, in mcc-auth.db: BCrypt password hash, role, lockout state and the token version that ends every session at once.",
  auth_session: "One row per sign-in. A token is honoured only while its session is live, so sign-out takes effect at once.",
  auth_key: "The secret keys that sign the access and refresh tokens, created on first start.",
  display_page: "Display pages: 20 parameter slots each, owned by an account. A deleted account's pages stay, as a former user's.",
  account_workspace: "Each account's window layout on the Display page.",
};

/** The diagram's columns: a heading and the x of its boxes (1060 × 380 viewBox). */
export const COLUMNS = [
  { title: "Mission database", x: 20 },
  { title: "Downlink map", x: 280 },
  { title: "Telemetry", x: 540 },
  { title: "Accounts and display", x: 800 },
];

/** Top-left of each drawn table's box. */
export const POS = {
  subsystem: [20, 34],
  enum_type: [20, 104],
  enum_value: [20, 174],
  message: [20, 244],
  parameter: [20, 314],
  tier: [280, 34],
  lut: [280, 104],
  lut_entry: [280, 174],
  pick: [280, 244],
  pick_param: [280, 314],
  telemetry_latest: [540, 174],
  telemetry_history: [540, 314],
  account: [800, 34],
  auth_session: [800, 104],
  auth_key: [800, 174],
  display_page: [800, 244],
  account_workspace: [800, 314],
};
export const BW = 220;
export const BH = 44;
export const VIEW_W = 1060;
export const VIEW_H = 380;
