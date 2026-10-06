// Pure chart maths (no uPlot import, so it loads in tests). Times are seconds since epoch, UTC.

/** Stroke colours for series 1–4. */
export const SERIES_COLORS = ["--plot", "--plot-2", "--plot-3", "--plot-4"];

/** Approximate width of one character of the 10 px axis font (IBM Plex Mono), in CSS px. */
export const AXIS_CHAR_PX = 6.1;

const pad = (n) => String(n).padStart(2, "0");

function parts(sec) {
  const d = new Date(sec * 1000);
  return {
    day: `${d.getUTCFullYear()}-${pad(d.getUTCMonth() + 1)}-${pad(d.getUTCDate())}`,
    md: `${pad(d.getUTCMonth() + 1)}-${pad(d.getUTCDate())}`,
    hm: `${pad(d.getUTCHours())}:${pad(d.getUTCMinutes())}`,
    s: pad(d.getUTCSeconds()),
  };
}

/** `2026-09-26 06:30:16` (UTC). */
export const fmtFull = (sec) => {
  const p = parts(sec);
  return `${p.day} ${p.hm}:${p.s}`;
};

/** Axis tick labels: clock time within a day, date + time across days, date only over weeks. */
export function timeTicks(vals) {
  const span = vals.length > 1 ? vals[vals.length - 1] - vals[0] : 0;
  const crossesDay = vals.length > 1 && parts(vals[0]).day !== parts(vals[vals.length - 1]).day;
  return vals.map((v) => {
    const p = parts(v);
    if (span > 6 * 86400) return p.day;
    return crossesDay ? `${p.md} ${p.hm}` : `${p.hm}:${p.s}`;
  });
}

/** Minimum pixels between x ticks so their labels never touch. */
export function xTickSpace(spanSec, crossesDay) {
  const chars = spanSec > 6 * 86400 ? 10 : crossesDay ? 11 : 8;
  return Math.ceil(chars * AXIS_CHAR_PX + 18);
}

/** New x range after zooming by `factor` (<1 in, >1 out) around the centre, clamped to [lo, hi]. */
export function zoomRange(min, max, factor, lo, hi) {
  const c = (min + max) / 2;
  const half = Math.max(((max - min) * factor) / 2, 0.5);
  if (half * 2 >= hi - lo) return [lo, hi];
  let a = c - half;
  let b = c + half;
  if (a < lo) [a, b] = [lo, lo + half * 2];
  if (b > hi) [a, b] = [hi - half * 2, hi];
  return [a, b];
}

/**
 * Y range for display: the data widened to at least 10 % of the value's
 * magnitude, so a steady rail (12.00 V ± 0.02) draws as a calm line instead of
 * full-height sensor jitter, then padded 5 %.
 */
export function displayRange(values) {
  const vals = values.filter((v) => v !== null && v !== undefined && Number.isFinite(v));
  if (!vals.length) return [0, 1];
  let lo = Math.min(...vals);
  let hi = Math.max(...vals);
  const minSpan = Math.max(Math.abs(lo), Math.abs(hi)) * 0.1 || 1;
  if (hi - lo < minSpan) {
    const mid = (lo + hi) / 2;
    [lo, hi] = [mid - minSpan / 2, mid + minSpan / 2];
    // a non-negative signal never gets a negative axis just from the widening
    if (Math.min(...vals) >= 0 && lo < 0) [lo, hi] = [0, hi - lo];
  }
  const p = (hi - lo) * 0.05;
  return [lo === 0 ? 0 : lo - p, hi + p];
}

/** Engineering number with precision by magnitude. */
export function fmtNumber(x) {
  if (x === null || x === undefined || !Number.isFinite(x)) return "—";
  const a = Math.abs(x);
  return a >= 100 ? x.toFixed(1) : a >= 10 ? x.toFixed(2) : x.toFixed(3);
}
