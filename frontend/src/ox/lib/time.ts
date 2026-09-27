// All times are stored and entered in UTC; IST (UTC+05:30, no DST) is shown alongside (A13).
const IST_OFFSET_MS = 330 * 60_000;

export function toDate(v: string | number | Date): Date {
  return v instanceof Date ? v : new Date(v);
}

/** "2026-09-23 06:40:10" (UTC). */
export function fmtUtc(v: string | number | Date | null | undefined): string {
  if (v === null || v === undefined || v === '') return '—';
  const d = toDate(v);
  if (Number.isNaN(d.getTime())) return '—';
  return d.toISOString().replace('T', ' ').slice(0, 19);
}

/** "06:40:10" (UTC). */
export function fmtUtcTime(v: string | number | Date | null | undefined): string {
  const s = fmtUtc(v);
  return s === '—' ? s : s.slice(11);
}

/** "2026-09-23 12:10:10" (IST). */
export function fmtIst(v: string | number | Date | null | undefined): string {
  if (v === null || v === undefined || v === '') return '—';
  const d = toDate(v);
  if (Number.isNaN(d.getTime())) return '—';
  return new Date(d.getTime() + IST_OFFSET_MS).toISOString().replace('T', ' ').slice(0, 19);
}

/**
 * A `datetime-local` value typed in UTC ("2026-09-23T10:00" or with seconds) to an RFC 3339 UTC string.
 * Returns null for blank input and undefined for input that is not a valid time.
 */
export function utcInputToIso(value: string): string | null | undefined {
  const v = value.trim();
  if (!v) return null;
  if (!/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}(:\d{2}(\.\d{1,3})?)?$/.test(v)) return undefined;
  const d = new Date(v + 'Z');
  return Number.isNaN(d.getTime()) ? undefined : d.toISOString();
}

/** RFC 3339 to the `datetime-local` value, in UTC, with seconds. */
export function isoToUtcInput(iso: string | null | undefined): string {
  if (!iso) return '';
  const d = new Date(iso);
  return Number.isNaN(d.getTime()) ? '' : d.toISOString().slice(0, 19);
}

/** "1 h 04 min" style duration for spans such as run length. */
export function fmtDuration(ms: number): string {
  const s = Math.max(0, Math.round(ms / 1000));
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const x = s % 60;
  if (h) return `${h} h ${String(m).padStart(2, '0')} min`;
  if (m) return `${m} min ${String(x).padStart(2, '0')} s`;
  return `${x} s`;
}
