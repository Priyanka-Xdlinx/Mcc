// The Charts page: the Orblinx platform's widget dashboard (sections, "+ Add widget", widget types,
// extra series, A − B compare, thresholds, resize, drag to reorder, derived parameters and the A∘B
// calculation builder), fed from mcc-app telemetry instead of Orblinx runs.
import {
  DndContext,
  KeyboardSensor,
  PointerSensor,
  closestCenter,
  useSensor,
  useSensors,
  type DragEndEvent,
} from '@dnd-kit/core'
import { SortableContext, rectSortingStrategy, sortableKeyboardCoordinates } from '@dnd-kit/sortable'
import { useCallback, useEffect, useMemo, useRef, useState, useSyncExternalStore } from 'react'
import type { Row } from '../types'
import { Section } from './components/Section'
import { DERIVED_ID, compile, resolveId, type Compiled, type DerivedDef } from './lib/derived'
import { NO_DATA, displayUnit, fmtNumber, formatValue, isPlottable, type Dtype } from './lib/format'
import { useNow } from './lib/hooks'
import {
  defaultDashboard,
  defaultTypeFor,
  isChartType,
  loadDashboard,
  loadDerived,
  moveWidget,
  newUid,
  saveDashboard,
  saveDerived,
  seriesIds,
  type Dashboard,
  type Layout,
  type Widget as LayoutWidget,
} from './lib/layout'
import { clientStat } from './lib/series'
import { tmPeriodMs } from './lib/widgets'
import { DerivedEditor } from './telemetry/DerivedEditor'
import { LiveBuffer } from './telemetry/liveBuffer'
import { Widget, type SeriesOption, type WidgetData, type WidgetMeta } from './telemetry/Widget'
import type { Channel, RoleSection } from './types'
import { Icon } from '../components/Icon'
import './ox.css'

const WINDOWS = [
  { min: 5, label: '5 min' },
  { min: 15, label: '15 min' },
  { min: 30, label: 'All stored (~24 min)' },
]
/** Seconds between readings per downlink part. */
const PERIOD_S: Record<string, number> = { VH: 1, H: 2, M: 4, L: 8 }
const NEEDS_SERIES = new Set(['plot', 'bar', 'gauge', 'state', 'bits', 'stat'])
const USER = 'local'
/** Calculations hold each input's last reading this long (the slowest part, L, repeats every 8 s). */
const HOLD_MS = 10_000

const keyOf = (id: number) => `P${id}`
const idOf = (key: string) => (/^P\d+$/.test(key) ? Number(key.slice(1)) : null)

function dtypeOf(t: string): Dtype {
  switch (t) {
    case 'FLOAT':
    case 'DOUBLE':
      return 'float'
    case 'INT':
    case 'UINT':
      return 'int'
    case 'ENUM':
      return 'enum'
    case 'BOOL':
      return 'bool'
    case 'HEX':
      return 'hex'
    default:
      return 'version' // text: shown as a value, never plotted
  }
}

/** Spacecraft time is UTC calendar time ("2026-09-26 18:52:56.883"). */
function timeOf(s: string | null | undefined, fallback?: string | null): number {
  if (s) {
    const t = Date.parse(s.replace(' ', 'T') + 'Z')
    if (Number.isFinite(t)) return t
  }
  return fallback ? Date.parse(fallback) : NaN
}

/** The default layout: a few meaningful sections, found by name so they follow the MIB. */
function defaultSections(rows: Row[]): RoleSection[] {
  const find = (sub: string, name: string) => rows.find((r) => r.subsystem === sub && r.name === name)
  const w = (sub: string, name: string, type: string): [string, string] | null => {
    const r = find(sub, name)
    return r ? [keyOf(r.parameter_id), type] : null
  }
  const clean = (l: ([string, string] | null)[]) => l.filter((x): x is [string, string] => x !== null)
  return [
    {
      key: 'gps',
      title: 'Position (GPS)',
      widgets: clean([w('GPS', 'lat', 'plot'), w('GPS', 'lon', 'plot'), w('GPS', 'hgt', 'value'), w('GPS', 'num_svs', 'stat'), w('GPS', 'pdop', 'gauge')]),
    },
    {
      key: 'eps',
      title: 'Power (EPS)',
      widgets: clean([
        w('EPS', 'Conv1 OP Voltage Reading', 'plot'),
        w('EPS', 'Total Battery Voltage Reading', 'gauge'),
        w('EPS', 'Total Battery Current Reading', 'stat'),
        w('EPS', 'EPS Mode', 'state'),
      ]),
    },
    {
      key: 'uhf',
      title: 'Radio (UHF)',
      widgets: clean([w('UHF', 'Last RSSI', 'plot'), w('UHF', 'Last LQI', 'stat'), w('UHF', 'Uptime', 'stat'), w('UHF', 'UART1 Rx Count', 'value')]),
    },
    {
      key: 'adcs',
      title: 'Attitude (ADCS)',
      widgets: clean([w('ADCS', 'Control mode', 'state'), w('ADCS', 'Estimated roll angle', 'value'), w('ADCS', 'Estimated pitch angle', 'value'), w('ADCS', 'Estimated yaw angle', 'value')]),
    },
  ]
}

/** One starter calculation: battery power = voltage × current, when both are downlinked. */
function defaultDerived(rows: Row[]): DerivedDef[] {
  const v = rows.find((r) => r.subsystem === 'EPS' && r.name === 'Total Battery Voltage Reading')
  const i = rows.find((r) => r.subsystem === 'EPS' && r.name === 'Total Battery Current Reading')
  return v && i ? [{ id: 'D1', name: 'Battery power', expr: `${keyOf(v.parameter_id)}*${keyOf(i.parameter_id)}`, unit: 'W' }] : []
}

export function ChartsDashboard({ rows, lutCrc, pick }: { rows: Row[]; lutCrc: number | null; pick: number[] }) {
  // Wait for the parameter list: the default layout and saved layouts are keyed by it.
  if (!rows.length || lutCrc === null) return <div className="empty">Loading parameters…</div>
  return <Dash key={lutCrc} rows={rows} sha={`lut${lutCrc}`} pick={pick} />
}

function Dash({ rows, sha, pick }: { rows: Row[]; sha: string; pick: number[] }) {
  // Channels: fixed metadata per parameter; enum labels are learned from the decoded text ("3 (Aborted)").
  const enumLabels = useRef(new Map<string, Record<string, string>>())
  for (const r of rows) {
    if (r.data_type !== 'ENUM' || !r.value) continue
    const m = /^(-?\d+) \((.*)\)$/.exec(r.value)
    if (m && m[2] !== '?') {
      const k = keyOf(r.parameter_id)
      const cur = enumLabels.current.get(k) ?? {}
      if (cur[m[1]!] !== m[2]) enumLabels.current.set(k, { ...cur, [m[1]!]: m[2]! })
    }
  }
  const channels: Channel[] = useMemo(
    () =>
      rows.map((r) => ({
        key: keyOf(r.parameter_id),
        name: r.name,
        unit: r.unit,
        dtype: dtypeOf(r.data_type),
        group: r.subsystem,
        hidden: false,
        enumLabels: null,
        hexDigits: null,
      })),
    // metadata never changes while the page is open; values do
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [rows.length],
  )
  const byKey = useMemo(() => new Map(channels.map((c) => [c.key, c])), [channels])
  const rowByKey = useMemo(() => new Map(rows.map((r) => [keyOf(r.parameter_id), r])), [rows])

  const [derived, setDerivedState] = useState<DerivedDef[]>(() => loadDerived(USER, sha) ?? defaultDerived(rows))
  const [dash, setDash] = useState<Dashboard>(
    () => loadDashboard(USER, sha) ?? defaultDashboard(defaultSections(rows), channels, derived),
  )
  const layout = dash.widgets
  const setLayout = useCallback((f: (l: Layout) => Layout) => setDash((d) => ({ ...d, widgets: f(d.widgets) })), [])
  const [windowMin, setWindowMin] = useState(15)
  const [confirmReset, setConfirmReset] = useState(false)
  useEffect(() => void saveDashboard(USER, sha, dash), [sha, dash])
  const setDerived = (next: DerivedDef[]) => {
    setDerivedState(next)
    saveDerived(USER, sha, next)
  }

  // Parameters sent here from the dashboard ("Plot selected") land in their own section.
  useEffect(() => {
    if (!pick.length) return
    setDash((d) => {
      const cur = d.widgets.picked ?? []
      const have = new Set(cur.map((w) => w.id))
      const add = pick.map(keyOf).filter((k) => byKey.has(k) && !have.has(k))
      if (!add.length) return d
      const sections = d.sections.some((s) => s.key === 'picked') ? d.sections : [{ key: 'picked', title: 'Picked from the dashboard' }, ...d.sections]
      return {
        sections,
        widgets: { ...d.widgets, picked: [...cur, ...add.map((id) => ({ uid: newUid(), id, type: defaultTypeFor(byKey.get(id)!.dtype) }))] },
      }
    })
  }, [pick, byKey])

  const compiled = useMemo(() => {
    const m = new Map<string, Compiled>()
    for (const d of derived) {
      if (byKey.has(d.id)) continue
      try {
        m.set(d.id, compile(d.expr, byKey.keys()))
      } catch {
        /* invalid stored formula: its widgets show No data */
      }
    }
    return m
  }, [derived, byKey])

  // Parameters the buffer must hold: every widget's ids and the inputs of derived ones.
  const keys = useMemo(() => {
    const s = new Set<string>()
    const add = (id: string) => {
      if (byKey.has(id)) s.add(id)
      else compiled.get(id)?.refs.forEach((r) => byKey.has(r) && s.add(r))
    }
    for (const list of Object.values(layout)) for (const w of list) seriesIds(w).forEach(add)
    return [...s].sort()
  }, [layout, compiled, byKey])

  // ---- data: a rolling column buffer, back-filled from history and fed from the live rows
  const buffer = useMemo(() => new LiveBuffer(4000), [])
  const version = useSyncExternalStore(buffer.subscribe, buffer.getVersion)
  const loaded = useRef(new Set<string>())
  const lastSeen = useRef(new Map<string, string>())
  const windowMs = windowMin * 60_000

  useEffect(() => {
    const need = keys.filter((k) => !loaded.current.has(k))
    if (!need.length) return
    need.forEach((k) => loaded.current.add(k))
    void Promise.all(
      need.map(async (k) => {
        const id = idOf(k)
        if (id === null) return
        const period = PERIOD_S[rowByKey.get(k)?.tier ?? 'VH'] ?? 1
        const limit = Math.min(5000, Math.ceil((30 * 60) / period) + 5)
        try {
          const r = await fetch(`/api/telemetry/${id}/history?limit=${limit}`)
          if (!r.ok) throw new Error(String(r.status))
          const h: { spacecraft_time: string | null; received_at: string | null; value_num: number | null }[] = await r.json()
          const pts = h
            .map((x) => [timeOf(x.spacecraft_time, x.received_at), x.value_num] as const)
            .filter(([t]) => Number.isFinite(t)) as [number, number | null][]
          buffer.merge({ [k]: pts })
        } catch {
          loaded.current.delete(k) // retry on the next change
        }
      }),
    )
  }, [keys, buffer, rowByKey])

  useEffect(() => {
    for (const k of keys) {
      const r = rowByKey.get(k)
      if (!r || !r.spacecraft_time) continue
      if (lastSeen.current.get(k) === r.spacecraft_time) continue
      lastSeen.current.set(k, r.spacecraft_time)
      const t = timeOf(r.spacecraft_time)
      if (!Number.isFinite(t)) continue
      buffer.upsert(t, { [k]: r.quality === 'OK' || r.quality === 'WARMUP' ? r.value_num : null })
    }
  }, [rows, keys, rowByKey, buffer])

  const now = useNow(1000)
  const end = buffer.lastTs() ?? now
  const fromMs = end - windowMs
  const framePeriodMs = tmPeriodMs(buffer.times)
  // A widget's expected spacing is its slowest parameter's downlink period (VH 1 s ... L 8 s).
  const periodOf = (id: string): number => {
    const r = rowByKey.get(id)
    if (r) return (PERIOD_S[r.tier ?? 'VH'] ?? 1) * 1000
    const cd = compiled.get(id)
    return cd ? Math.max(framePeriodMs, ...cd.refs.map(periodOf)) : framePeriodMs
  }
  const widgetPeriod = (w: LayoutWidget) => Math.max(framePeriodMs, ...seriesIds(w).map(periodOf))
  void version

  const meta = useCallback(
    (id: string): WidgetMeta => {
      const r = resolveId(id, byKey, derived)
      if (r.kind === 'derived')
        return { name: r.def.name, unit: r.def.unit, kind: 'derived', format: (v) => (v === null ? NO_DATA : fmtNumber(v)) }
      if (r.kind === 'missing')
        return { name: DERIVED_ID.test(id) ? `${id} (removed)` : id, unit: '', kind: 'missing', format: () => NO_DATA, missing: true }
      const c = r.channel
      const labels = enumLabels.current.get(id) ?? null
      return {
        name: c.name,
        unit: displayUnit(c.unit),
        kind: c.dtype,
        format: (v) => (c.dtype === 'enum' && v !== null && !labels?.[String(Math.trunc(v))] ? `${v}` : formatValue({ ...c, enumLabels: labels }, v)),
        state: c.dtype === 'enum',
        hexDigits: c.hexDigits,
        level: () => null,
        table1: null,
      }
    },
    [byKey, derived],
  )

  const seriesData = (id: string, withSeries: boolean) => {
    const cd = byKey.has(id) ? undefined : compiled.get(id)
    if (cd) {
      const { x, rows: rs } = buffer.rows(cd.refs, withSeries ? fromMs : (buffer.lastTs() ?? 0), HOLD_MS)
      const y = rs.map((r) => cd.eval(r))
      let lastTs: number | null = null
      let latest: number | null = null
      for (let i = y.length - 1; i >= 0; i--)
        if (y[i] !== null) {
          lastTs = x[i]!
          latest = y[i]!
          break
        }
      return { latest, lastTs, x: withSeries ? x : [], y: withSeries ? y : [], stat: clientStat(y) }
    }
    const s = withSeries ? buffer.slice(id, fromMs) : { x: [] as number[], y: [] as (number | null)[] }
    // latest value that is not missing (rows of other parts have no value for this parameter)
    let latest: number | null = null
    const lastTs = buffer.lastValueTs(id)
    if (lastTs !== null) {
      const i = buffer.lowerBound(lastTs)
      latest = buffer.slice(id, buffer.times[i]!).y[0] ?? null
    }
    return { latest, lastTs, x: s.x, y: s.y, stat: withSeries ? clientStat(s.y) : null }
  }

  const widgetData = (w: LayoutWidget): WidgetData => {
    const withSeries = NEEDS_SERIES.has(w.type)
    const ids = seriesIds(w)
    const parts = ids.map((id, i) => seriesData(id, withSeries && (i === 0 || isChartType(w.type))))
    const p0 = parts[0]!
    if (!isChartType(w.type) && p0.y.length) {
      // Single-series cards (stat sparkline, state, bits): keep only the rows that carry this parameter.
      const keep = p0.y.map((v, i) => (v === null ? -1 : i)).filter((i) => i >= 0)
      p0.x = keep.map((i) => p0.x[i]!)
      p0.y = keep.map((i) => p0.y[i]!)
    }
    return {
      latest: p0.latest,
      x: p0.x,
      ys: parts.map((p) => p.y),
      stats: parts.map((p) => p.stat),
      statSource: p0.stat ? 'client' : 'none',
      latests: parts.map((p) => p.latest),
      lastTs: parts.map((p) => p.lastTs),
    }
  }

  const sensors = useSensors(
    useSensor(PointerSensor, { activationConstraint: { distance: 5 } }),
    useSensor(KeyboardSensor, { coordinateGetter: sortableKeyboardCoordinates }),
  )
  const onDragEnd = (e: DragEndEvent) => {
    if (e.over && e.active.id !== e.over.id) setLayout((l) => moveWidget(l, String(e.active.id), String(e.over!.id)))
  }
  const onUpdate = useCallback(
    (uid: string, patch: Partial<LayoutWidget>) =>
      setLayout((l) => Object.fromEntries(Object.entries(l).map(([k, v]) => [k, v.map((w) => (w.uid === uid ? { ...w, ...patch } : w))]))),
    [setLayout],
  )
  const onRemove = useCallback(
    (uid: string) => setLayout((l) => Object.fromEntries(Object.entries(l).map(([k, v]) => [k, v.filter((w) => w.uid !== uid)]))),
    [setLayout],
  )
  const addWidget = (section: string, id: string) => {
    const r = resolveId(id, byKey, derived)
    const type = defaultTypeFor(r.kind === 'channel' ? r.channel.dtype : 'derived')
    setLayout((l) => ({ ...l, [section]: [...(l[section] ?? []), { uid: newUid(), id, type }] }))
  }
  const addSection = () => {
    const n = dash.sections.length + 1
    const key = `s${Date.now().toString(36)}`
    setDash((d) => ({ sections: [...d.sections, { key, title: `My section ${n}` }], widgets: { ...d.widgets, [key]: [] } }))
  }
  const visibleDerived = useMemo(() => derived.filter((d) => !byKey.has(d.id)), [derived, byKey])
  const seriesOptions: SeriesOption[] = useMemo(
    () => [
      ...visibleDerived.map((d) => ({ id: d.id, label: `${d.id} · ${d.name}` })),
      ...channels.filter((c) => isPlottable(c.dtype)).map((c) => ({ id: c.key, label: `${c.group} · ${c.name.slice(0, 40)}` })),
    ],
    [visibleDerived, channels],
  )
  const rowOptions: SeriesOption[] = useMemo(
    () => [
      ...visibleDerived.map((d) => ({ id: d.id, label: `${d.id} · ${d.name}` })),
      ...channels.map((c) => ({ id: c.key, label: `${c.group} · ${c.name.slice(0, 40)}` })),
    ],
    [visibleDerived, channels],
  )

  return (
    <div className="oxd oxd-page">
      <div className="oxd-head">
        <div>
          <h1>
            <span className="oxd-badge">
              <Icon name="orbit" size={22} />
            </span>
            Charts
          </h1>
          <p>
            Arrange widgets per section: plot, bar, gauge, state, stat card, value or table. Add up to three
            parameters to a plot, compare two with A − B, set thresholds, and build calculations below.
          </p>
        </div>
      </div>
      <div className="controls">
        <label className="fld row" htmlFor="win">
          <Icon name="clock" size={14} />
          Window
          <select id="win" value={windowMin} onChange={(e) => setWindowMin(Number(e.target.value))}>
            {WINDOWS.map((w) => (
              <option key={w.min} value={w.min}>
                {w.label}
              </option>
            ))}
          </select>
        </label>
        <span className="muted small">UTC · drag a chart to zoom · drag a widget by its title to move it · drag its corner to resize</span>
        <span className="sep" />
        <button type="button" className="btn sm" onClick={addSection}>
          <Icon name="section" size={14} />
          Add section
        </button>
        {confirmReset ? (
          <>
            <span className="small">Reset to the default layout?</span>
            <button type="button" className="btn sm danger" onClick={() => {
              setDash(defaultDashboard(defaultSections(rows), channels, derived))
              setConfirmReset(false)
            }}>
              <Icon name="reset" size={14} />
              Reset
            </button>
            <button type="button" className="btn sm" onClick={() => setConfirmReset(false)}>
              Cancel
            </button>
          </>
        ) : (
          <button type="button" className="btn sm" onClick={() => setConfirmReset(true)}>
            <Icon name="reset" size={14} />
            Reset layout
          </button>
        )}
      </div>

      <DndContext sensors={sensors} collisionDetection={closestCenter} onDragEnd={onDragEnd}>
        {dash.sections.map((s) => {
          const list = layout[s.key] ?? []
          return (
            <Section
              key={s.key}
              title={s.title}
              persistId={`charts.${s.key}`}
              count={`${list.length} widget${list.length === 1 ? '' : 's'}`}
              end={<AddWidget section={s.key} title={s.title} channels={channels} derived={visibleDerived} onAdd={addWidget} />}
            >
              <SortableContext items={list.map((w) => w.uid)} strategy={rectSortingStrategy}>
                <div className="grid">
                  {list.length === 0 && <p className="empty">No widgets. Add one with the menu above.</p>}
                  {list.map((w) => (
                    <Widget
                      key={w.uid}
                      w={w}
                      metas={seriesIds(w).map(meta)}
                      data={widgetData(w)}
                      markers={[]}
                      seriesOptions={seriesOptions}
                      rowOptions={rowOptions}
                      onUpdate={onUpdate}
                      onRemove={onRemove}
                      now={now}
                      live
                      periodMs={widgetPeriod(w)}
                    />
                  ))}
                </div>
              </SortableContext>
            </Section>
          )
        })}
      </DndContext>

      <DerivedEditor
        derived={derived}
        known={byKey}
        onChange={(next) => {
          setDerived(next)
          const ids = new Set(next.map((d) => d.id))
          const gone = (id: string) => DERIVED_ID.test(id) && !byKey.has(id) && !ids.has(id)
          setLayout((l) =>
            Object.fromEntries(
              Object.entries(l).map(([k, v]) => [
                k,
                v
                  .filter((w) => !gone(w.id))
                  .map((w) => {
                    const more = w.more?.filter((m) => !gone(m))
                    return more?.length === w.more?.length ? w : { ...w, more, diff: false }
                  }),
              ]),
            ),
          )
        }}
      />
    </div>
  )
}

/** "+ Add widget…": derived parameters first, then every parameter grouped by subsystem. */
function AddWidget(props: {
  section: string
  title: string
  channels: Channel[]
  derived: DerivedDef[]
  onAdd: (section: string, id: string) => void
}) {
  const id = `add-${props.section}`
  const groups = useMemo(() => {
    const m = new Map<string, Channel[]>()
    for (const c of props.channels) m.set(c.group ?? 'Other', [...(m.get(c.group ?? 'Other') ?? []), c])
    return [...m.entries()]
  }, [props.channels])
  return (
    <>
      <label className="sr-only" htmlFor={id}>
        Add widget to {props.title}
      </label>
      <select id={id} value="" onChange={(e) => e.target.value && props.onAdd(props.section, e.target.value)}>
        <option value="">+ Add widget…</option>
        {props.derived.length > 0 && (
          <optgroup label="Calculations">
            {props.derived.map((d) => (
              <option key={d.id} value={d.id}>
                {d.id} · {d.name}
              </option>
            ))}
          </optgroup>
        )}
        {groups.map(([g, cs]) => (
          <optgroup key={g} label={g}>
            {cs.map((c) => (
              <option key={c.key} value={c.key}>
                {c.name.slice(0, 48)}
                {displayUnit(c.unit) ? ` (${displayUnit(c.unit)})` : ''} · {c.key}
              </option>
            ))}
          </optgroup>
        ))}
      </select>
    </>
  )
}
