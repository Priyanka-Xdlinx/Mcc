import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { LineChart, type Point, type Series } from './LineChart'
import type { Row } from '../types'
import type { Mover } from '../hooks/useTelemetryStream'
import { displayValue, plural } from '../format'

// Categorical slots in fixed order: blue, orange, aqua, yellow, magenta, green.
// Each theme defines its own step of these hues (--s1..--s6 in index.css) so
// every series keeps >= 3:1 contrast on its surface. Assigned in order and
// never cycled.
const SERIES_COLORS = ['var(--s1)', 'var(--s2)', 'var(--s3)', 'var(--s4)', 'var(--s5)', 'var(--s6)']
/** Past this many series an overlay is unreadable, so the grid takes over. */
const OVERLAY_MAX = SERIES_COLORS.length
const SELECT_MAX = 48

interface HistoryRow {
  master_frame: number
  subframe: number
  spacecraft_time: string | null
  value_num: number | null
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
  const [data, setData] = useState<Record<number, Point[]>>({})
  const [q, setQ] = useState('')
  const [autoRefresh, setAutoRefresh] = useState(true)
  const [view, setView] = useState<View>('grid')

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
      .slice(0, 600)
  }, [rows, q])

  // Only the newest request may write: an older, slower one would otherwise
  // overwrite the data for a selection that has since changed.
  const requestNo = useRef(0)
  const load = useCallback(async (ids: number[]) => {
    const mine = ++requestNo.current
    let pairs: [number, Point[]][]
    try {
      pairs = await Promise.all(
      ids.map(async (id): Promise<[number, Point[]]> => {
        const r = await fetch(`/api/telemetry/${id}/history?limit=400`)
        if (!r.ok) throw new Error(`history ${id}: HTTP ${r.status}`)
        const hist: HistoryRow[] = await r.json()
        return [
          id,
          hist
            .filter((h) => h.value_num !== null)
            .map((h) => ({
              x: h.master_frame * 16 + (h.subframe - 1),
              y: h.value_num as number,
              label: h.spacecraft_time ?? `MF${h.master_frame} SF${h.subframe}`,
            }))
            .sort((a, b) => a.x - b.x),
        ]
      }),
      )
    } catch {
      return // backend unreachable: keep what is on screen; the next refresh retries
    }
    if (mine === requestNo.current) setData(Object.fromEntries(pairs))
  }, [])

  useEffect(() => {
    if (selected.length) void load(selected)
    else setData({})
  }, [selected, load])

  useEffect(() => {
    if (!autoRefresh || !live || selected.length === 0) return
    const t = setInterval(() => void load(selected), 4000)
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
    return (id: number) => SERIES_COLORS[(m.get(id) ?? 0) % SERIES_COLORS.length]
  }, [selected])

  const widgets = useMemo(
    () =>
      selected
        .map((id) => {
          const row = rows.find((r) => r.parameter_id === id)
          if (!row) return null
          const pts = data[id] ?? []
          const varies = pts.length > 1 && pts.some((p) => p.y !== pts[0].y)
          return { id, row, pts, varies, color: colorOf(id) }
        })
        .filter((w): w is NonNullable<typeof w> => w !== null),
    [selected, rows, data, colorOf],
  )

  // Overlay mode still refuses to put two units on one axis.
  const facets = useMemo(() => {
    const byUnit = new Map<string, { series: Series[]; step: boolean }>()
    for (const w of widgets) {
      const unit = w.row.unit ?? ''
      const isState = w.row.data_type === 'ENUM' || w.row.data_type === 'BOOL'
      const s: Series = { id: w.id, name: w.row.name, color: w.color, points: w.pts }
      const f = byUnit.get(unit)
      if (f) {
        f.series.push(s)
        f.step = f.step && isState
      } else byUnit.set(unit, { series: [s], step: isState })
    }
    return Array.from(byUnit.entries())
  }, [widgets])

  const tooManyToOverlay = selected.length > OVERLAY_MAX
  const effectiveView: View = tooManyToOverlay ? 'grid' : view

  return (
    <div className="charts">
      <aside className="picker">
        <input
          type="search"
          placeholder="Filter parameters…"
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
                Array.from(
                  new Set([...s, ...candidates.map((c) => c.parameter_id)]),
                ).slice(0, SELECT_MAX),
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
              title="One widget per parameter"
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
                  : 'Draw the selected series on shared axes'
              }
            >
              Overlay
            </button>
          </div>
          <label>
            <input
              type="checkbox"
              checked={autoRefresh}
              onChange={(e) => setAutoRefresh(e.target.checked)}
            />
            auto-refresh while live
          </label>
          <button onClick={() => void load(selected)} disabled={!selected.length}>
            Refresh now
          </button>
          <div className="count">
            {widgets.filter((w) => w.varies).length} of {widgets.length} moving · last 400 samples
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
                <div style={{ marginTop: 14 }}>
                  {plural(movers.length, 'parameter')} changed since this page opened:
                </div>
                <div className="chip-row">
                  {movers.slice(0, 8).map((m) => {
                    const r = rows.find((x) => x.parameter_id === m.id)
                    return r ? (
                      <button key={m.id} className="chip-btn" onClick={() => toggle(m.id)}>
                        {r.name} <span style={{ color: 'var(--dimmer)' }}>{displayValue(r.value)}</span>
                      </button>
                    ) : null
                  })}
                </div>
                <div className="chip-row">
                  <button className="primary"
                          onClick={() => setSelected(movers.slice(0, OVERLAY_MAX).map((m) => m.id))}>
                    Chart the changing parameters
                  </button>
                </div>
              </>
            ) : (
              <div style={{ marginTop: 10, color: 'var(--dimmer)' }}>
                Most parameters hold a constant value in the current feed; those that change will be
                offered here as soon as they move.
              </div>
            )}
          </div>
        ) : effectiveView === 'overlay' ? (
          facets.map(([unit, f]) => (
            <div className="plot" key={unit || 'unitless'}>
              <div className="plot-title">
                {f.series.length === 1 ? f.series[0].name : `${f.series.length} parameters`}
                {unit ? <span className="plot-unit"> · {unit}</span> : null}
                <span className="plot-form">{f.step ? 'step' : 'line'}</span>
              </div>
              <LineChart
                series={f.series}
                unit={unit || null}
                step={f.step}
                area={f.series.length === 1 && !f.step}
              />
            </div>
          ))
        ) : (
          <div className="widgets">
            {widgets.map((w) => {
              const last = w.pts[w.pts.length - 1]
              const isState = w.row.data_type === 'ENUM' || w.row.data_type === 'BOOL'
              return (
                <div className={`widget ${w.varies ? '' : 'flat'}`} key={w.id}>
                  <div className="w-head">
                    <span className="swatch" style={{ background: w.color }} />
                    <span className="w-name" title={`${w.row.entry} · ${w.row.message}`}>
                      {w.row.name}
                    </span>
                    <span className="w-tier">{w.row.tier}</span>
                  </div>
                  <div className="w-value" title={w.row.value ?? undefined}>
                    {displayValue(w.row.value ?? (last ? String(last.y) : null))}
                  </div>
                  <div className="w-meta">
                    {w.row.subsystem} · {w.row.data_type}
                    {w.varies ? (
                      <span className="w-moving"> · moving</span>
                    ) : (
                      <span> · flat over {w.pts.length} samples</span>
                    )}
                  </div>
                  <LineChart
                    series={[
                      { id: w.id, name: w.row.name, color: w.color, points: w.pts },
                    ]}
                    unit={w.row.unit}
                    height={112}
                    step={isState}
                    area={!isState && w.varies}
                    compact
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
