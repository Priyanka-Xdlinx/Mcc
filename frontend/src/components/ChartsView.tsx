import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { Chart, type ChartSeries } from './Chart'
import type { Row } from '../types'
import type { Mover } from '../hooks/useTelemetryStream'
import { displayValue, plural } from '../format'

// Series colours, in fixed order, from the shared chart palette (index.css, per theme).
const SLOT_VARS = ['--s1', '--s2', '--s3', '--s4', '--s5', '--s6']
/** Past this many series an overlay is unreadable, so the widget grid takes over. */
const OVERLAY_MAX = SLOT_VARS.length
const SELECT_MAX = 48
/** Seconds between readings for each part of the payload. */
const PERIOD: Record<string, number> = { VH: 1, H: 2, M: 4, L: 8 }
const WINDOWS = [
  { id: '5m', label: 'Last 5 min', sec: 300 },
  { id: '15m', label: 'Last 15 min', sec: 900 },
  { id: 'all', label: 'All stored', sec: 0 },
] as const

interface HistoryRow {
  master_frame: number
  subframe: number
  spacecraft_time: string | null
  received_at: string | null
  value_num: number | null
}
interface Pt { t: number; y: number }

/** Spacecraft time is UTC calendar time ("2026-09-26 18:52:56.883"); fall back to receive time. */
function timeOf(h: HistoryRow): number {
  if (h.spacecraft_time) {
    const t = Date.parse(h.spacecraft_time.replace(' ', 'T') + 'Z')
    if (Number.isFinite(t)) return t
  }
  return h.received_at ? Date.parse(h.received_at) : NaN
}

/** Put several series on one time axis: union of timestamps, null where a series has no reading. */
function align(list: Pt[][]): { x: number[]; ys: (number | null)[][] } {
  const x = Array.from(new Set(list.flatMap((pts) => pts.map((p) => p.t)))).sort((a, b) => a - b)
  const index = new Map(x.map((t, i) => [t, i]))
  const ys = list.map((pts) => {
    const y: (number | null)[] = new Array(x.length).fill(null)
    for (const p of pts) y[index.get(p.t)!] = p.y
    return y
  })
  return { x, ys }
}

type View = 'grid' | 'overlay'

export function ChartsView({
  rows,
  live,
  movers,
  initial,
}: {
  rows: Row[]
  live: boolean
  movers: Mover[]
  initial: number[]
}) {
  const [selected, setSelected] = useState<number[]>(initial)
  const [data, setData] = useState<Record<number, Pt[]>>({})
  const [q, setQ] = useState('')
  const [autoRefresh, setAutoRefresh] = useState(true)
  const [view, setView] = useState<View>('grid')
  const [windowId, setWindowId] = useState<(typeof WINDOWS)[number]['id']>('15m')

  const rowById = useMemo(() => new Map(rows.map((r) => [r.parameter_id, r])), [rows])

  const candidates = useMemo(() => {
    const needle = q.trim().toLowerCase()
    return rows
      .filter((r) => r.value_num !== null)
      .filter(
        (r) =>
          !needle ||
          r.name.toLowerCase().includes(needle) ||
          r.subsystem.toLowerCase().includes(needle) ||
          r.entry.toLowerCase().includes(needle),
      )
      // ticked parameters first, so a selection is never buried in the list
      .sort((a, b) => Number(selected.includes(b.parameter_id)) - Number(selected.includes(a.parameter_id)))
      .slice(0, 600)
  }, [rows, q, selected])

  // Only the newest request may write: an older, slower one would otherwise
  // overwrite the data for a selection that has since changed.
  const requestNo = useRef(0)
  const load = useCallback(
    async (ids: number[]) => {
      const mine = ++requestNo.current
      const win = WINDOWS.find((w) => w.id === windowId)!
      let pairs: [number, Pt[]][]
      try {
        pairs = await Promise.all(
          ids.map(async (id): Promise<[number, Pt[]]> => {
            const period = PERIOD[rowById.get(id)?.tier ?? 'VH'] ?? 1
            const limit = win.sec ? Math.min(5000, Math.ceil(win.sec / period) + 2) : 5000
            const r = await fetch(`/api/telemetry/${id}/history?limit=${limit}`)
            if (!r.ok) throw new Error(`history ${id}: HTTP ${r.status}`)
            const hist: HistoryRow[] = await r.json()
            const pts = hist
              .filter((h) => h.value_num !== null)
              .map((h) => ({ t: timeOf(h), y: h.value_num as number }))
              .filter((p) => Number.isFinite(p.t))
              .sort((a, b) => a.t - b.t)
            if (!win.sec || !pts.length) return [id, pts]
            const from = pts[pts.length - 1].t - win.sec * 1000
            return [id, pts.filter((p) => p.t >= from)]
          }),
        )
      } catch {
        return // backend unreachable: keep what is on screen; the next refresh retries
      }
      if (mine === requestNo.current) setData(Object.fromEntries(pairs))
    },
    [windowId, rowById],
  )

  useEffect(() => {
    if (selected.length) void load(selected)
    else setData({})
    // rows change every second; reload on selection or window change only
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [selected, windowId])

  useEffect(() => {
    if (!autoRefresh || !live || selected.length === 0) return
    const t = setInterval(() => void load(selected), 3000)
    return () => clearInterval(t)
  }, [autoRefresh, live, selected, load])

  const toggle = (id: number) =>
    setSelected((s) =>
      s.includes(id) ? s.filter((x) => x !== id) : s.length >= SELECT_MAX ? s : [...s, id],
    )

  // Colour follows the parameter, not its position: each series keeps its slot
  // when another is removed, and a new series takes the lowest free slot.
  // Past six series the widget grid takes over and slots repeat.
  const slots = useRef(new Map<number, number>())
  const colorOf = useMemo(() => {
    const m = slots.current
    for (const id of Array.from(m.keys())) if (!selected.includes(id)) m.delete(id)
    for (const id of selected) {
      if (m.has(id)) continue
      const used = new Set(m.values())
      let slot = 0
      while (used.has(slot)) slot++
      m.set(id, slot)
    }
    return (id: number) => SLOT_VARS[(m.get(id) ?? 0) % SLOT_VARS.length]
  }, [selected])

  const widgets = useMemo(
    () =>
      selected
        .map((id) => {
          const row = rowById.get(id)
          if (!row) return null
          const pts = data[id] ?? []
          const varies = pts.length > 1 && pts.some((p) => p.y !== pts[0].y)
          return { id, row, pts, varies, colorVar: colorOf(id) }
        })
        .filter((w): w is NonNullable<typeof w> => w !== null),
    [selected, rowById, data, colorOf],
  )

  // Overlay never puts two units on one axis: one chart per unit.
  const facets = useMemo(() => {
    const byUnit = new Map<string, typeof widgets>()
    for (const w of widgets) {
      const unit = w.row.unit ?? ''
      byUnit.set(unit, [...(byUnit.get(unit) ?? []), w])
    }
    return Array.from(byUnit.entries()).map(([unit, ws]) => ({ unit, ws, ...align(ws.map((w) => w.pts)) }))
  }, [widgets])

  const tooManyToOverlay = selected.length > OVERLAY_MAX
  const effectiveView: View = tooManyToOverlay ? 'grid' : view

  return (
    <div className="charts">
      <aside className="picker">
        <input
          type="search"
          placeholder="Filter parameters…"
          aria-label="Filter parameters"
          value={q}
          onChange={(e) => setQ(e.target.value)}
        />
        <div className="picker-actions">
          <span className="picker-note">
            {selected.length} selected{selected.length >= SELECT_MAX ? ` (max ${SELECT_MAX})` : ''}
          </span>
          <button
            onClick={() =>
              setSelected((s) =>
                Array.from(new Set([...s, ...candidates.map((c) => c.parameter_id)])).slice(0, SELECT_MAX),
              )
            }
            disabled={candidates.length === 0}
            title="Add every parameter matching the filter"
          >
            Add all
          </button>
          <button onClick={() => setSelected([])} disabled={!selected.length}>
            Clear
          </button>
        </div>
        <div className="picker-list">
          {candidates.map((r) => (
            <label key={r.parameter_id} className={selected.includes(r.parameter_id) ? 'on' : ''}>
              <input
                type="checkbox"
                checked={selected.includes(r.parameter_id)}
                disabled={!selected.includes(r.parameter_id) && selected.length >= SELECT_MAX}
                onChange={() => toggle(r.parameter_id)}
              />
              <span className="pname" title={`${r.entry} · ${r.message}`}>
                {r.name}
              </span>
              <span className="punit">{r.unit ?? ''}</span>
            </label>
          ))}
          {candidates.length === 0 && <div className="picker-note">no matches</div>}
        </div>
      </aside>

      <section className="plots">
        <div className="plots-bar">
          <div className="dv-group seg">
            <button
              className={effectiveView === 'grid' ? 'primary' : ''}
              onClick={() => setView('grid')}
              title="One chart per parameter"
            >
              Widgets
            </button>
            <button
              className={effectiveView === 'overlay' ? 'primary' : ''}
              onClick={() => setView('overlay')}
              disabled={tooManyToOverlay}
              title={
                tooManyToOverlay
                  ? `Overlay is unreadable past ${OVERLAY_MAX} series`
                  : 'Parameters with the same unit on shared axes'
              }
            >
              Overlay
            </button>
          </div>
          <select value={windowId} onChange={(e) => setWindowId(e.target.value as typeof windowId)} aria-label="Time window">
            {WINDOWS.map((w) => (
              <option key={w.id} value={w.id}>{w.label}</option>
            ))}
          </select>
          <label>
            <input type="checkbox" checked={autoRefresh} onChange={(e) => setAutoRefresh(e.target.checked)} />
            live refresh
          </label>
          <div className="count">
            {widgets.filter((w) => w.varies).length} of {widgets.length} moving · UTC · drag on a chart to zoom
          </div>
        </div>

        {widgets.length === 0 ? (
          <div className="empty">
            <div style={{ fontSize: 15, fontWeight: 600, color: 'var(--fg)', marginBottom: 6 }}>
              Choose parameters to chart
            </div>
            Tick parameters in the list, or press <b>Add all</b> to chart everything matching the filter.
            {movers.length > 0 ? (
              <>
                <div style={{ marginTop: 14 }}>{plural(movers.length, 'parameter')} changed since this page opened:</div>
                <div className="chip-row">
                  {movers.slice(0, 8).map((m) => {
                    const r = rowById.get(m.id)
                    return r ? (
                      <button key={m.id} className="chip-btn" onClick={() => toggle(m.id)}>
                        {r.name} <span style={{ color: 'var(--dimmer)' }}>{displayValue(r.value)}</span>
                      </button>
                    ) : null
                  })}
                </div>
                <div className="chip-row">
                  <button className="primary" onClick={() => setSelected(movers.slice(0, OVERLAY_MAX).map((m) => m.id))}>
                    Chart the changing parameters
                  </button>
                </div>
              </>
            ) : (
              <div style={{ marginTop: 10, color: 'var(--dimmer)' }}>
                Most parameters hold a constant value in the current feed; those that change will be offered here as
                soon as they move.
              </div>
            )}
          </div>
        ) : effectiveView === 'overlay' ? (
          facets.map((f) => (
            <div className="plot" key={f.unit || 'unitless'}>
              <div className="plot-title">
                {f.ws.length === 1 ? f.ws[0].row.name : plural(f.ws.length, 'parameter')}
                <span className="plot-unit"> · {f.unit || 'no unit'}</span>
              </div>
              <Chart
                x={f.x}
                ys={f.ys}
                series={f.ws.map((w): ChartSeries => ({ label: w.row.name, unit: w.row.unit ?? undefined, colorVar: w.colorVar, scale: 'y' }))}
                height={240}
                spanGaps
                ariaLabel={`${f.ws.map((w) => w.row.name).join(', ')} over time`}
              />
            </div>
          ))
        ) : (
          <div className="widgets">
            {widgets.map((w) => {
              const isState = w.row.data_type === 'ENUM' || w.row.data_type === 'BOOL'
              return (
                <div className={`widget ${w.varies ? '' : 'flat'}`} key={w.id}>
                  <div className="w-head">
                    <span className="swatch" style={{ background: `var(${w.colorVar})` }} />
                    <span className="w-name" title={`${w.row.entry} · ${w.row.message}`}>{w.row.name}</span>
                    <span className="w-tier">{w.row.tier}</span>
                    <button className="w-close" onClick={() => toggle(w.id)} aria-label={`Remove ${w.row.name}`} title="Remove">×</button>
                  </div>
                  <div className="w-value" title={w.row.value ?? undefined}>{displayValue(w.row.value)}</div>
                  <div className="w-meta">
                    {w.row.subsystem} · {w.row.data_type}
                    {isState ? ' · state' : ''}
                    {w.varies ? <span className="w-moving"> · moving</span> : <span> · constant over {plural(w.pts.length, 'reading')}</span>}
                  </div>
                  <Chart
                    x={w.pts.map((p) => p.t)}
                    ys={[w.pts.map((p) => p.y)]}
                    series={[{ label: w.row.name, unit: w.row.unit ?? undefined, colorVar: w.colorVar }]}
                    height={130}
                    ariaLabel={`${w.row.name} over time`}
                  />
                </div>
              )
            })}
          </div>
        )}
      </section>
    </div>
  )
}
