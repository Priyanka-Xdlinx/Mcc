// Pure helpers for the dashboard widgets (features/telemetry/Widget*.tsx): thresholds, gauge geometry,
// bit displays, state durations, sparklines and data freshness. Kept apart so component files only export
// components, and so the maths is unit-tested.
import type { Widget, WidgetSettings } from './layout';
import type { Level } from '../types';

// ---------- thresholds

/** crit beats warn; 'ok' when thresholds exist and the value is inside them; null without thresholds. */
export function levelOf(v: number | null, s: WidgetSettings): Level | null {
  const has = [s.warnLo, s.warnHi, s.critLo, s.critHi].some((x) => x !== undefined);
  if (!has || v === null || !Number.isFinite(v)) return null;
  if ((s.critLo !== undefined && v < s.critLo) || (s.critHi !== undefined && v > s.critHi)) return 'crit';
  if ((s.warnLo !== undefined && v < s.warnLo) || (s.warnHi !== undefined && v > s.warnHi)) return 'warn';
  return 'ok';
}

export interface Threshold {
  value: number;
  level: 'warn' | 'crit';
}

/** The threshold lines a plot draws. */
export function thresholdsOf(s: WidgetSettings): Threshold[] {
  const out: Threshold[] = [];
  for (const [v, level] of [
    [s.warnLo, 'warn'],
    [s.warnHi, 'warn'],
    [s.critLo, 'crit'],
    [s.critHi, 'crit'],
  ] as const)
    if (v !== undefined && Number.isFinite(v)) out.push({ value: v, level });
  return out;
}

/** Problems with the thresholds as typed, for the settings panel ('' when consistent). */
export function thresholdError(s: WidgetSettings): string {
  if (s.warnLo !== undefined && s.warnHi !== undefined && s.warnLo >= s.warnHi)
    return 'Warn low must be below warn high.';
  if (s.critLo !== undefined && s.critHi !== undefined && s.critLo >= s.critHi)
    return 'Crit low must be below crit high.';
  if (s.critLo !== undefined && s.warnLo !== undefined && s.critLo > s.warnLo)
    return 'Crit low is usually below warn low.';
  if (s.critHi !== undefined && s.warnHi !== undefined && s.critHi < s.warnHi)
    return 'Crit high is usually above warn high.';
  if (s.min !== undefined && s.max !== undefined && s.min >= s.max) return 'Gauge min must be below max.';
  return '';
}

/** A number with fixed decimals when set; otherwise the caller's usual format. */
export function fmtDecimals(v: number, dec: number | undefined, fallback: (v: number) => string): string {
  return dec === undefined ? fallback(v) : v.toFixed(dec);
}

// ---------- gauge

export interface GaugeScale {
  min: number;
  max: number;
  source: 'setting' | 'table1' | 'observed' | 'none';
}

/**
 * Gauge scale: the widget's own min/max, else the TC Table 1 range of the channel (when it echoes a TC
 * field), else the observed min/max in view (padded so the needle is not pinned), else 0…1.
 */
export function gaugeScale(
  w: Pick<Widget, 'min' | 'max'>,
  table1: { min: number; max: number } | null,
  observed: { min: number | null; max: number | null } | null,
): GaugeScale {
  if (w.min !== undefined && w.max !== undefined && w.max > w.min)
    return { min: w.min, max: w.max, source: 'setting' };
  if (table1 && table1.max > table1.min) {
    return {
      min: w.min ?? table1.min,
      max: w.max ?? table1.max,
      source: w.min !== undefined || w.max !== undefined ? 'setting' : 'table1',
    };
  }
  const lo = observed?.min ?? null;
  const hi = observed?.max ?? null;
  if (lo !== null && hi !== null && Number.isFinite(lo) && Number.isFinite(hi)) {
    // Round, readable limits: a non-negative signal starts at 0; the top is the next 1/2/2.5/5 × 10ⁿ above
    // the highest value seen (so 19.75 V reads on a 0–25 V dial, not −1.975…21.73).
    const top = niceCeil(Math.max(Math.abs(hi), Math.abs(lo)) * 1.1 || 1);
    const min = w.min ?? (lo >= 0 ? 0 : -top);
    const max = w.max ?? (hi <= 0 ? 0 : top);
    if (max > min) return { min, max, source: 'observed' };
  }
  return { min: w.min ?? 0, max: w.max ?? (w.min ?? 0) + 1, source: 'none' };
}

/** Smallest 1, 2, 2.5 or 5 × 10ⁿ that is ≥ x (x > 0). */
export function niceCeil(x: number): number {
  const p = Math.pow(10, Math.floor(Math.log10(x)));
  return [1, 2, 2.5, 5, 10].map((m) => m * p).find((n) => n >= x - 1e-12) ?? 10 * p;
}

/** Position of v on the scale, clamped to 0…1 (null for a missing value). */
export function gaugeFraction(v: number | null, min: number, max: number): number | null {
  if (v === null || !Number.isFinite(v) || !(max > min)) return null;
  return Math.min(1, Math.max(0, (v - min) / (max - min)));
}

export interface Band {
  from: number; // fraction 0…1
  to: number;
  level: 'warn' | 'crit';
}

/** Coloured arc bands for the thresholds: crit outside critLo/critHi, warn between crit and warn. */
export function gaugeBands(s: WidgetSettings, min: number, max: number): Band[] {
  const f = (v: number) => Math.min(1, Math.max(0, (v - min) / (max - min)));
  if (!(max > min)) return [];
  const out: Band[] = [];
  const lowCrit = s.critLo !== undefined ? f(s.critLo) : 0;
  const highCrit = s.critHi !== undefined ? f(s.critHi) : 1;
  if (s.critLo !== undefined && lowCrit > 0) out.push({ from: 0, to: lowCrit, level: 'crit' });
  if (s.warnLo !== undefined && f(s.warnLo) > lowCrit)
    out.push({ from: lowCrit, to: f(s.warnLo), level: 'warn' });
  if (s.warnHi !== undefined && f(s.warnHi) < highCrit)
    out.push({ from: f(s.warnHi), to: highCrit, level: 'warn' });
  if (s.critHi !== undefined && highCrit < 1) out.push({ from: highCrit, to: 1, level: 'crit' });
  return out.filter((b) => b.to > b.from);
}

/** The gauge sweeps 240°, from lower left (fraction 0) over the top to lower right (fraction 1). */
export const GAUGE_SWEEP = 240;

/** Point on the gauge arc for a fraction, in SVG coordinates (y down). */
export function arcPoint(cx: number, cy: number, r: number, frac: number): [number, number] {
  const deg = 210 - frac * GAUGE_SWEEP; // 210° = lower left, −30° = lower right (maths angles)
  const rad = (deg * Math.PI) / 180;
  return [cx + r * Math.cos(rad), cy - r * Math.sin(rad)];
}

/** SVG path of the arc between two fractions. */
export function arcPath(cx: number, cy: number, r: number, f0: number, f1: number): string {
  const [x0, y0] = arcPoint(cx, cy, r, f0);
  const [x1, y1] = arcPoint(cx, cy, r, f1);
  const large = (f1 - f0) * GAUGE_SWEEP > 180 ? 1 : 0;
  const n = (x: number) => +x.toFixed(2);
  return `M ${n(x0)} ${n(y0)} A ${r} ${r} 0 ${large} 1 ${n(x1)} ${n(y1)}`;
}

// ---------- bits

/** Bytes a bitmap/hex value spans: from the channel's hex digits, else from the value itself. */
export function byteCount(hexDigits: number | null | undefined, v: number | null): number {
  if (hexDigits && hexDigits > 0) return Math.min(8, Math.ceil(hexDigits / 2));
  if (v !== null && v > 0xffff) return 4;
  if (v !== null && v > 0xff) return 2;
  return 1;
}

/** Bit b (0 = least significant) of a non-negative integer, for up to 53 bits. */
export function bitAt(v: number, b: number): boolean {
  return Math.floor(Math.trunc(v) / 2 ** b) % 2 === 1;
}

/** The bits of v, most significant first: [{ bit, on }], 8 per byte. */
export function bitsOf(v: number | null, bytes: number): { bit: number; on: boolean | null }[] {
  const n = bytes * 8;
  return Array.from({ length: n }, (_, i) => {
    const bit = n - 1 - i;
    return { bit, on: v === null || !Number.isFinite(v) || v < 0 ? null : bitAt(v, bit) };
  });
}

/** Bits that changed at least once between consecutive samples in view (missing samples are skipped). */
export function toggledBits(ys: readonly (number | null)[], bytes: number): Set<number> {
  const out = new Set<number>();
  let prev: number | null = null;
  for (const v of ys) {
    if (v === null || !Number.isFinite(v) || v < 0) continue;
    if (prev !== null && prev !== v)
      for (let b = 0; b < bytes * 8; b++) if (bitAt(prev, b) !== bitAt(v, b)) out.add(b);
    prev = v;
  }
  return out;
}

// ---------- state durations

/**
 * When the latest value started: the time of the first sample of the trailing run of equal values (missing
 * samples do not end a run), and the value before it. `sinceStart` means it has held for the whole view.
 */
export function stateSince(
  x: readonly number[],
  y: readonly (number | null)[],
): { since: number; sinceStart: boolean; previous: number | null } | null {
  let i = y.length - 1;
  while (i >= 0 && y[i] === null) i--;
  if (i < 0) return null;
  const cur = y[i]!;
  let start = i;
  let previous: number | null = null;
  for (let j = i - 1; j >= 0; j--) {
    const v = y[j];
    if (v === null || v === undefined) continue;
    if (v !== cur) {
      previous = v;
      break;
    }
    start = j;
  }
  return { since: x[start]!, sinceStart: previous === null, previous };
}

// ---------- sparklines and deltas

/** SVG path for a sparkline in a w×h box; a missing sample breaks the line. '' without data. */
export function sparkPath(
  x: readonly number[],
  y: readonly (number | null)[],
  w: number,
  h: number,
  pad = 2,
): string {
  const pts: [number, number | null][] = x.map((t, i) => [t, y[i] ?? null]);
  const vals = pts.map((p) => p[1]).filter((v): v is number => v !== null && Number.isFinite(v));
  if (!vals.length || x.length < 2) return '';
  const lo = Math.min(...vals);
  const hi = Math.max(...vals);
  const x0 = x[0]!;
  const span = x[x.length - 1]! - x0 || 1;
  const sy = hi - lo || 1;
  const n = (v: number) => +v.toFixed(1);
  let d = '';
  let pen = false;
  for (const [t, v] of pts) {
    if (v === null || !Number.isFinite(v)) {
      pen = false;
      continue;
    }
    const px = pad + ((t - x0) / span) * (w - 2 * pad);
    const py = hi === lo ? h / 2 : pad + (1 - (v - lo) / sy) * (h - 2 * pad);
    d += `${pen ? 'L' : 'M'}${n(px)} ${n(py)} `;
    pen = true;
  }
  return d.trim();
}

/** Latest minus the first value in view (both non-missing); null when there are fewer than two. */
export function deltaInView(y: readonly (number | null)[]): number | null {
  const first = y.find((v) => v !== null && Number.isFinite(v));
  let last: number | null = null;
  for (let i = y.length - 1; i >= 0; i--)
    if (y[i] !== null && Number.isFinite(y[i]!)) {
      last = y[i]!;
      break;
    }
  const count = y.filter((v) => v !== null).length;
  return first === undefined || first === null || last === null || count < 2 ? null : last - first;
}

// ---------- freshness

/** The telemetry period: the median step between the last samples (1 s when unknown). */
export function tmPeriodMs(times: readonly number[], lookback = 30): number {
  const t = times.slice(-lookback - 1);
  const d: number[] = [];
  for (let i = 1; i < t.length; i++) if (t[i]! > t[i - 1]!) d.push(t[i]! - t[i - 1]!);
  if (!d.length) return 1000;
  d.sort((a, b) => a - b);
  return d[Math.floor(d.length / 2)]!;
}

/** A value older than 3 telemetry periods on a live run is stale. */
export function isStale(lastValueTs: number | null, now: number, periodMs: number, live: boolean): boolean {
  return live && lastValueTs !== null && now - lastValueTs > 3 * Math.max(periodMs, 250);
}

/** "12 s", "4 min", "2 h 5 min". */
export function fmtAge(ms: number): string {
  const s = Math.max(0, Math.round(ms / 1000));
  if (s < 60) return `${s} s`;
  const m = Math.floor(s / 60);
  if (m < 60) return `${m} min`;
  const h = Math.floor(m / 60);
  return `${h} h${m % 60 ? ` ${m % 60} min` : ''}`;
}
