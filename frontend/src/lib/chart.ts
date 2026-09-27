// Adapted from propulsion/orblinx_platform/web/src/lib/chart.ts (same look across both products).

/** Stroke colours for series 1–4 (series with their own `colorVar` keep it). */
export const SERIES_COLORS = ['--plot', '--plot-2', '--plot-3', '--plot-4'];

const pad = (n: number) => String(n).padStart(2, '0');
/** Seconds since epoch → UTC parts (pure; no uPlot, so this module loads in tests). */
function parts(sec: number) {
  const d = new Date(sec * 1000);
  return {
    day: `${d.getUTCFullYear()}-${pad(d.getUTCMonth() + 1)}-${pad(d.getUTCDate())}`,
    md: `${pad(d.getUTCMonth() + 1)}-${pad(d.getUTCDate())}`,
    hm: `${pad(d.getUTCHours())}:${pad(d.getUTCMinutes())}`,
    s: pad(d.getUTCSeconds()),
  };
}
const fmtClock = (sec: number) => {
  const p = parts(sec);
  return `${p.hm}:${p.s}`;
};
const fmtDayMin = (sec: number) => {
  const p = parts(sec);
  return `${p.md} ${p.hm}`;
};
const fmtDay = (sec: number) => parts(sec).day;
/** `2026-09-26 06:30:16` (UTC). */
export const fmtFull = (sec: number) => {
  const p = parts(sec);
  return `${p.day} ${p.hm}:${p.s}`;
};

/** Axis tick labels: clock time within a day, date + time over longer spans, date only over weeks. */
export function timeTicks(vals: readonly number[]): string[] {
  const span = vals.length > 1 ? vals[vals.length - 1]! - vals[0]! : 0;
  const crossesDay = vals.length > 1 && fmtDay(vals[0]!) !== fmtDay(vals[vals.length - 1]!);
  const f = span > 6 * 86400 ? fmtDay : crossesDay ? fmtDayMin : fmtClock;
  return vals.map(f);
}

/** New x range after zooming by `factor` (<1 in, >1 out) around the centre, clamped to [lo, hi]. */
export function zoomRange(
  min: number,
  max: number,
  factor: number,
  lo: number,
  hi: number,
): [number, number] {
  const c = (min + max) / 2;
  let half = ((max - min) * factor) / 2;
  half = Math.max(half, 0.5);
  if (half * 2 >= hi - lo) return [lo, hi];
  let a = c - half;
  let b = c + half;
  if (a < lo) [a, b] = [lo, lo + half * 2];
  if (b > hi) [a, b] = [hi - half * 2, hi];
  return [a, b];
}

/** Approximate width of one character of the 10 px axis font (IBM Plex Mono), in CSS px. */
export const AXIS_CHAR_PX = 6.1;

/**
 * Minimum pixels between x ticks so their labels never touch: the widest label the axis can show for this
 * span (see `timeTicks`) plus a gap. uPlot then picks fewer ticks on narrow charts.
 */
export function xTickSpace(spanSec: number, elapsed: boolean, crossesDay: boolean): number {
  const chars = elapsed
    ? spanSec >= 3600
      ? 7 // "1:02:03"
      : 5 // "12:34"
    : spanSec > 6 * 86400
      ? 10 // "2026-09-26"
      : crossesDay
        ? 11 // "09-26 18:12"
        : 8; // "18:12:27"
  return Math.ceil(chars * AXIS_CHAR_PX + 18);
}

export interface LabelBox {
  /** Pixel x of the marker line. */
  px: number;
  /** Measured label width in the same pixel unit. */
  width: number;
}

/**
 * Where to draw each marker label so none overlap or leave the plot: to the right of its line, flipped to
 * the left when it would run past the right edge, clamped to [left, right], and skipped (null) when it
 * would overlap the previous label. Input must be in ascending px order; returns the label's left x.
 */
export function layoutMarkerLabels(
  boxes: readonly LabelBox[],
  left: number,
  right: number,
  pad: number,
  gap: number,
): (number | null)[] {
  let lastEnd = -Infinity;
  return boxes.map(({ px, width }) => {
    if (width > right - left) return null;
    let x = px + pad;
    if (x + width > right) x = px - pad - width;
    x = Math.max(left, Math.min(x, right - width));
    if (x < lastEnd + gap) return null;
    lastEnd = x + width;
    return x;
  });
}

/**
 * Y range for display: the data (plus any threshold lines), widened to at least 10 % of the value's magnitude so
 * a steady rail (12.00 V ± 0.02) draws as a calm line instead of full-height sensor jitter, then padded 5 %.
 */
export function displayRange(values: readonly (number | null | undefined)[]): [number, number] {
  const vals = values.filter((v): v is number => v !== null && v !== undefined && Number.isFinite(v));
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
  const pad = (hi - lo) * 0.05;
  return [lo === 0 ? 0 : lo - pad, hi + pad];
}
