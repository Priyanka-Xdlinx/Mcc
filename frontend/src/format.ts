/**
 * Display-only formatting. The decoder's own text is kept as the source of
 * truth; this only shortens values too long to read in a table cell (the
 * simulator emits pattern-filled floats such as -2503167248092704615…000).
 */
const NUM = /^(-?\d+(?:\.\d+)?)(\s.*)?$/

export function displayValue(v: string | null | undefined): string {
  if (v == null) return '—'
  if (v.length <= 16) return v
  const m = NUM.exec(v)
  if (!m) return v
  const n = Number(m[1])
  if (!Number.isFinite(n) || Math.abs(n) < 1e12) return v
  return `${n.toExponential(3)}${m[2] ?? ''}`
}

/** Seconds since a timestamp, as "0.4 s ago" / "12 s ago" / "3 min ago". */
export function ago(ms: number | null, now: number): string {
  if (ms == null) return 'never'
  const s = Math.max(0, (now - ms) / 1000)
  if (s < 10) return `${s.toFixed(1)} s ago`
  if (s < 90) return `${Math.round(s)} s ago`
  return `${Math.round(s / 60)} min ago`
}
