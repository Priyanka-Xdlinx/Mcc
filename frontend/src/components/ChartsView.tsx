import { useCallback, useEffect, useMemo, useState } from 'react'
import { LineChart, type Point, type Series } from './LineChart'
import type { Row } from '../types'

// Categorical slots in fixed order, stepped for this dark surface and validated
// as a set (lightness band, chroma floor, CVD separation, normal-vision floor,
// contrast >= 3:1 against #0d1117). Assigned in order and never cycled.
const SERIES_COLORS = [
  '#3987e5', // blue
  '#d95926', // orange
  '#199e70', // aqua
  '#c98500', // yellow
  '#d55181', // magenta
  '#008300', // green
]
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

export function ChartsView({ rows, live }: { rows: Row[]; live: boolean }) {
  const [selected, setSelected] = useState<number[]>([])
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

  const load = useCallback(async (ids: number[]) => {
    const pairs = await Promise.all(
      ids.map(async (id): Promise<[number, Point[]]> => {
        const r = await fetch(`/api/telemetry/${id}/history?limit=400`)
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
    setData(Object.fromEntries(pairs))
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

  // Colour follows the parameter, not its position: a series keeps its hue when
  // another is removed.
  const colorOf = useCallback(
    (id: number) => SERIES_COLORS[Math.max(0, selected.indexOf(id)) % SERIES_COLORS.length],
    [selected],
  )

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
            Pick parameters on the left — or press <b>Add all</b> to chart everything
            matching the filter.
            <br />
            <span style={{ color: 'var(--dimmer)' }}>
              In this simulator only 8 of 755 parameters actually change, so most
              widgets will sit flat. Try <code>lat</code>, <code>lon</code>,{' '}
              <code>Uptime</code> or <code>card_run_counter</code>.
            </span>
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
                  <div className="w-value">
                    {w.row.value ?? (last ? String(last.y) : '—')}
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
