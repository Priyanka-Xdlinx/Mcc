import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { HexDump } from './HexDump'
import { FrameView } from './FrameView'
import type { Row } from '../types'

interface TraceLine {
  label: string
  value: string
  note: string | null
}
interface TraceHex {
  title: string
  baseOffset: number
  bytes: string[]
  markFrom: number | null
  markTo: number | null
}
interface TraceStep {
  title: string
  lines: TraceLine[]
  hex: TraceHex[]
}
interface Trace {
  error?: string
  steps?: TraceStep[]
  parameter?: string
  entry?: string
  subsystem?: string
  message?: string
  tier?: string
  blockSpan?: number
  masterFrame?: number
  subframe?: number
  spacecraftTime?: string
  slices?: { index: number; present: boolean; subframe: number | null }[]
  blockComplete?: boolean
  value?: string
  quality?: string
}
interface Frame {
  masterFrame: number
  subframe: number
  spacecraftTime: string
  payloadLength: number
  completes: string[]
}

const SPEEDS = [
  { label: '0.5×', ms: 2000 },
  { label: '1×', ms: 1000 },
  { label: '2×', ms: 500 },
  { label: '4×', ms: 250 },
]

/** Which layer a step belongs to, for the column it is drawn in. */
function layerOf(title: string): 1 | 2 | 3 {
  if (title.startsWith('Layer 1')) return 1
  if (title.startsWith('Layer 2') || title === 'The arithmetic') return 2
  return 3
}

export function DecodeView({ rows, live }: { rows: Row[]; live: boolean }) {
  const [paramId, setParamId] = useState<number | null>(null)
  const [q, setQ] = useState('')
  const [frames, setFrames] = useState<Frame[]>([])
  const [cursor, setCursor] = useState<number | null>(null) // index into frames (0 = newest)
  const [follow, setFollow] = useState(true)
  const [playing, setPlaying] = useState(false)
  const [speed, setSpeed] = useState(1)
  const [trace, setTrace] = useState<Trace | null>(null)
  const [mode, setMode] = useState<'param' | 'frame'>('frame')
  const timer = useRef<ReturnType<typeof setInterval> | null>(null)

  const candidates = useMemo(() => {
    const needle = q.trim().toLowerCase()
    const list = rows
      .filter(
        (r) =>
          !needle ||
          r.name.toLowerCase().includes(needle) ||
          r.entry.toLowerCase().includes(needle) ||
          r.subsystem.toLowerCase().includes(needle),
      )
      .slice(0, 300)
    // The option list is capped, so the parameter being traced can fall outside
    // it - at which point the <select> would quietly display someone else's name
    // next to this one's trace. Keep it present whatever the filter says.
    if (paramId !== null && !list.some((r) => r.parameter_id === paramId)) {
      const sel = rows.find((r) => r.parameter_id === paramId)
      if (sel) list.unshift(sel)
    }
    return list
  }, [rows, q, paramId])

  useEffect(() => {
    if (paramId === null && rows.length) {
      // Open on something whose decode is worth reading: a scaled float.
      const pick =
        rows.find((r) => r.name === 'Conv1 OP Voltage Reading') ??
        rows.find((r) => r.unit) ??
        rows[0]
      setParamId(pick.parameter_id)
    }
  }, [rows, paramId])

  const loadFrames = useCallback(async () => {
    const r = await fetch('/api/frames?limit=320')
    setFrames(await r.json())
  }, [])

  useEffect(() => {
    void loadFrames()
    if (!live) return
    const t = setInterval(loadFrames, 2000)
    return () => clearInterval(t)
  }, [loadFrames, live])

  const current = cursor === null ? frames[0] : frames[cursor]

  const loadTrace = useCallback(
    async (f?: Frame) => {
      if (paramId === null) return
      const qs =
        f && !follow
          ? `?parameterId=${paramId}&mf=${f.masterFrame}&sf=${f.subframe}`
          : `?parameterId=${paramId}`
      const r = await fetch(`/api/trace${qs}`)
      setTrace(await r.json())
    },
    [paramId, follow],
  )

  useEffect(() => {
    void loadTrace(current)
  }, [paramId, cursor, follow, loadTrace, current?.masterFrame, current?.subframe])

  // Live follow: re-fetch the newest frame on a cadence you can actually watch.
  useEffect(() => {
    if (!follow || !live) return
    const t = setInterval(() => void loadTrace(), SPEEDS[speed].ms)
    return () => clearInterval(t)
  }, [follow, live, speed, loadTrace])

  // Playback over buffered frames, oldest to newest.
  useEffect(() => {
    if (timer.current) clearInterval(timer.current)
    if (!playing || follow) return
    timer.current = setInterval(() => {
      setCursor((c) => {
        const next = (c ?? 0) - 1
        if (next < 0) {
          setPlaying(false)
          return 0
        }
        return next
      })
    }, SPEEDS[speed].ms)
    return () => {
      if (timer.current) clearInterval(timer.current)
    }
  }, [playing, follow, speed])

  const step = (delta: number) => {
    setFollow(false)
    setPlaying(false)
    setCursor((c) => {
      const base = c ?? 0
      return Math.max(0, Math.min(frames.length - 1, base + delta))
    })
  }

  const byLayer = (n: 1 | 2 | 3) => (trace?.steps ?? []).filter((s) => layerOf(s.title) === n)

  return (
    <div className="decode">
      {/* ── controls ─────────────────────────────────────────────────────── */}
      <div className="decode-bar">
        <div className="dv-group seg">
          <button
            className={mode === 'frame' ? 'primary' : ''}
            onClick={() => setMode('frame')}
            title="Everything in this subframe: all tiers, entries and parameters"
          >
            Whole frame
          </button>
          <button
            className={mode === 'param' ? 'primary' : ''}
            onClick={() => setMode('param')}
            title="Follow one parameter byte by byte, Layer 1 to Layer 2"
          >
            One parameter
          </button>
          {mode === 'frame' && (
            <span className="seg-hint">
              open an entry, then click a parameter to unfold its trace
            </span>
          )}
        </div>

        <div className="dv-group" style={{ display: mode === 'param' ? 'flex' : 'none' }}>
          <span className="dv-k">Parameter</span>
          <input
            type="search"
            placeholder="filter…"
            value={q}
            onChange={(e) => setQ(e.target.value)}
            style={{ minWidth: 150 }}
          />
          <select
            value={paramId ?? ''}
            onChange={(e) => setParamId(Number(e.target.value))}
            style={{ maxWidth: 320 }}
          >
            {candidates.map((r) => (
              <option key={r.parameter_id} value={r.parameter_id}>
                [{r.tier}] {r.entry} · {r.name}
              </option>
            ))}
          </select>
        </div>

        <div className="dv-group">
          <button
            className={follow ? 'primary' : ''}
            onClick={() => {
              setFollow(true)
              setPlaying(false)
              setCursor(null)
            }}
            title="Follow the newest frame as it arrives"
          >
            ● Live
          </button>
          <button onClick={() => step(+1)} title="One subframe older">‹ Prev</button>
          <button
            onClick={() => {
              setFollow(false)
              setPlaying((p) => !p)
            }}
            disabled={frames.length === 0}
          >
            {playing ? '❚❚ Pause' : '▶ Replay'}
          </button>
          <button onClick={() => step(-1)} title="One subframe newer">Next ›</button>
        </div>

        <div className="dv-group">
          <span className="dv-k">Speed</span>
          {SPEEDS.map((s, i) => (
            <button
              key={s.label}
              className={i === speed ? 'primary' : ''}
              onClick={() => setSpeed(i)}
            >
              {s.label}
            </button>
          ))}
        </div>

        <div className="count">
          {frames.length} frames buffered
          {current && (
            <>
              {' · viewing '}
              <b>
                MF#{current.masterFrame} SF{current.subframe}
              </b>
              {follow ? ' (live)' : ` · ${(cursor ?? 0) + 1} back`}
            </>
          )}
        </div>
      </div>

      {/* ── frame timeline ───────────────────────────────────────────────── */}
      <div className="timeline">
        {frames
          .slice()
          .reverse()
          .map((f, i) => {
            const idx = frames.length - 1 - i
            const isCur =
              current &&
              f.masterFrame === current.masterFrame &&
              f.subframe === current.subframe
            return (
              <button
                key={`${f.masterFrame}-${f.subframe}`}
                className={`tick ${isCur ? 'cur' : ''} ${
                  f.completes.includes(trace?.tier ?? '') ? 'completes' : ''
                }`}
                title={`MF#${f.masterFrame} SF${f.subframe} · ${f.spacecraftTime}${
                  f.completes.length ? ` · completes ${f.completes.join(', ')}` : ''
                }`}
                onClick={() => {
                  setFollow(false)
                  setPlaying(false)
                  setCursor(idx)
                }}
              >
                {f.subframe}
              </button>
            )
          })}
        {frames.length === 0 && (
          <span className="dv-k">no frames buffered — start the simulator</span>
        )}
      </div>

      {mode === 'frame' ? (
        <FrameView
          mf={follow ? null : (current?.masterFrame ?? null)}
          sf={follow ? null : (current?.subframe ?? null)}
          live={live}
          refreshMs={SPEEDS[speed].ms}
        />
      ) : trace?.error ? (
        <div className="empty">{trace.error}</div>
      ) : !trace?.steps ? (
        <div className="empty">select a parameter</div>
      ) : (
        <>
          <div className="verdict">
            <div>
              <div className="dv-k">Parameter</div>
              <div className="verdict-v">{trace.parameter}</div>
            </div>
            <div>
              <div className="dv-k">Entry · tier</div>
              <div className="verdict-v">
                {trace.entry} · <span className={`tier ${trace.tier}`}>{trace.tier}</span>
              </div>
            </div>
            <div>
              <div className="dv-k">Frame</div>
              <div className="verdict-v">
                MF#{trace.masterFrame} SF{trace.subframe}
              </div>
            </div>
            <div>
              <div className="dv-k">Spacecraft time</div>
              <div className="verdict-v">{trace.spacecraftTime}</div>
            </div>
            <div className="verdict-result">
              <div className="dv-k">Decoded value</div>
              <div className={`verdict-big q-${trace.quality ?? ''}`}>
                {trace.value ?? '—'}
              </div>
              {trace.quality && trace.quality !== 'OK' && (
                <div className={`q ${trace.quality}`}>{trace.quality}</div>
              )}
            </div>
          </div>

          <div className="lanes">
            <Lane
              n="1"
              title="LAYER 1 — which message do these bytes belong to?"
              note="wire framing, tier split, block assembly, and the LUT. Needs the lookup table; needs nothing from the MIB."
              steps={byLayer(1)}
            />
            <Lane
              n="2"
              title="LAYER 2 — what does this byte mean?"
              note="the mission database: offsets, type, endianness, scale, enums."
              steps={byLayer(2)}
            />
            <Lane n="3" title="RESULT" note="what reaches the dashboard and the database." steps={byLayer(3)} />
          </div>
        </>
      )}
    </div>
  )
}

function Lane({
  n,
  title,
  note,
  steps,
}: {
  n: string
  title: string
  note: string
  steps: TraceStep[]
}) {
  return (
    <div className={`lane lane-${n}`}>
      <div className="lane-head">
        <span className="lane-n">{n}</span>
        <div>
          <div className="lane-title">{title}</div>
          <div className="lane-note">{note}</div>
        </div>
      </div>
      {steps.map((s, i) => (
        <div className="tstep" key={i}>
          <div className="tstep-title">{s.title}</div>
          {s.lines.length > 0 && (
            <table className="tstep-lines">
              <tbody>
                {s.lines.map((l, j) => (
                  <tr key={j}>
                    <td className="tl-k">{l.label}</td>
                    <td className="tl-v">{l.value}</td>
                    <td className="tl-n">{l.note ?? ''}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
          {s.hex.map((h, j) => (
            <HexDump
              key={j}
              title={h.title}
              bytes={h.bytes}
              baseOffset={h.baseOffset}
              markFrom={h.markFrom}
              markTo={h.markTo}
            />
          ))}
        </div>
      ))}
      {steps.length === 0 && <div className="tstep dim">—</div>}
    </div>
  )
}
