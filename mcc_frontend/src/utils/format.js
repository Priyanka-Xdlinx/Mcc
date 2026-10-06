/**
 * Display-only formatting. The decoder's own text is the source of truth; this
 * only shortens values too long to read in a table cell (the simulator emits
 * pattern-filled floats such as -2503167248092704615…000).
 */
const NUM = /^(-?\d+(?:\.\d+)?)(\s.*)?$/;

/** A telemetry message within this many ms counts as "receiving now". */
export const FRESH_MS = 3000;

/** Seconds between readings per downlink part. */
export const PERIOD_S = { VH: 1, H: 2, M: 4, L: 8 };

/** The parts in the order operators read them, fastest first. */
export const TIERS = ["VH", "H", "M", "L"];

export function displayValue(v) {
  if (v == null) return "—";
  if (v.length <= 16) return v;
  const m = NUM.exec(v);
  if (!m) return v;
  const n = Number(m[1]);
  if (!Number.isFinite(n) || Math.abs(n) < 1e12) return v;
  return `${n.toExponential(3)}${m[2] ?? ""}`;
}

/** Time since a timestamp, as "0.4 s ago" / "12 s ago" / "3 min ago". */
export function ago(ms, now) {
  if (ms == null) return "never";
  const s = Math.max(0, (now - ms) / 1000);
  if (s < 10) return `${s.toFixed(1)} s ago`;
  if (s < 90) return `${Math.round(s)} s ago`;
  return `${Math.round(s / 60)} min ago`;
}

/** CRC-16 as XDOS prints it (0x%04X), so the two consoles match by eye. */
export function crcHex(n) {
  if (n == null || n < 0) return "—";
  return `0x${n.toString(16).toUpperCase().padStart(4, "0")}`;
}

export function plural(n, one, many = `${one}s`) {
  return `${n.toLocaleString()} ${n === 1 ? one : many}`;
}

/** Spacecraft time is UTC calendar time ("2026-09-26 18:52:56.883"); falls back to an ISO instant. */
export function spacecraftMs(s, fallback) {
  if (s) {
    const t = Date.parse(s.replace(" ", "T") + "Z");
    if (Number.isFinite(t)) return t;
  }
  return fallback ? Date.parse(fallback) : NaN;
}

/** "18:52:56.883 UTC" from an ISO instant. */
export function utcClock(iso) {
  if (!iso) return null;
  const m = /T(\d{2}:\d{2}:\d{2})(\.\d{1,3})?/.exec(iso);
  return m ? `${m[1]}${m[2] ?? ""} UTC` : iso;
}
