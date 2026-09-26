import { useEffect, useMemo, useState } from 'react'
import type { Quality, Row, Status } from '../types'
import type { Mover } from '../hooks/useTelemetryStream'
import { ago, displayValue } from '../format'
import { PLATFORMS, XDLINX_LINKS } from './platforms'

interface Frame {
  masterFrame: number
  subframe: number
  spacecraftTime: string
  payloadLength: number
  completes: string[]
}

export interface TelemetryFilter {
  subsystem?: string
  onlyProblems?: boolean
}

const TIERS = [
  { id: 'VH', name: 'Very High', span: 1, bytes: '0 – 399' },
  { id: 'H', name: 'High', span: 2, bytes: '400 – 799' },
  { id: 'M', name: 'Medium', span: 4, bytes: '800 – 1099' },
  { id: 'L', name: 'Low', span: 8, bytes: '1100 – 1274' },
] as const

const PROBLEM: Quality[] = ['NO_ANSWER', 'BLOCK_INCOMPLETE']

function useNow(ms: number) {
  const [now, setNow] = useState(Date.now())
  useEffect(() => {
    const t = setInterval(() => setNow(Date.now()), ms)
    return () => clearInterval(t)
  }, [ms])
  return now
}

function counts(rows: Row[]) {
  let ok = 0, problem = 0, warmup = 0, none = 0
  for (const r of rows) {
    if (r.quality === 'OK') ok++
    else if (r.quality === 'WARMUP') warmup++
    else if (r.quality && PROBLEM.includes(r.quality)) problem++
    else none++
  }
  return { ok, problem, warmup, none, total: rows.length }
}

function pct(n: number, d: number) {
  return d ? `${(100 * n / d).toFixed(n === d ? 0 : 1)} %` : '—'
}

function Meter({ c }: { c: ReturnType<typeof counts> }) {
  const w = (n: number) => `${c.total ? (100 * n) / c.total : 0}%`
  return (
    <div className="meter" aria-hidden="true">
      <i className="m-ok" style={{ width: w(c.ok) }} />
      <i className="m-warn" style={{ width: w(c.problem) }} />
      <i className="m-warmup" style={{ width: w(c.warmup) }} />
      <i className="m-none" style={{ width: w(c.none) }} />
    </div>
  )
}

function Spark({ points }: { points: number[] }) {
  if (points.length < 2) return <svg className="spark" viewBox="0 0 76 24" />
  const min = Math.min(...points), max = Math.max(...points)
  const span = max - min || 1
  const x = (i: number) => (i / (points.length - 1)) * 72 + 2
  const y = (v: number) => 21 - ((v - min) / span) * 18
  const d = points.map((v, i) => `${i ? 'L' : 'M'}${x(i).toFixed(1)},${y(v).toFixed(1)}`).join(' ')
  const last = points.length - 1
  return (
    <svg className="spark" viewBox="0 0 76 24" aria-hidden="true">
      <path d={d} />
      <circle cx={x(last)} cy={y(points[last])} r={2.4} />
    </svg>
  )
}

export function Dashboard({
  rows,
  status,
  live,
  lastMsgAt,
  movers,
  onOpenTelemetry,
  onOpenCharts,
}: {
  rows: Row[]
  status: Status | null
  live: boolean
  lastMsgAt: number | null
  movers: Mover[]
  onOpenTelemetry: (f: TelemetryFilter) => void
  onOpenCharts: (ids: number[]) => void
}) {
  const now = useNow(500)
  const [frames, setFrames] = useState<Frame[]>([])
  const f = status?.feed

  useEffect(() => {
    let stop = false
    const load = async () => {
      try {
        const r = await fetch('/api/frames?limit=32')
        const data: Frame[] = await r.json()
        if (!stop) setFrames(data)
      } catch {
        /* backend restarting */
      }
    }
    void load()
    const t = setInterval(load, 1000)
    return () => {
      stop = true
      clearInterval(t)
    }
  }, [])

  const all = useMemo(() => counts(rows), [rows])
  const bySubsystem = useMemo(() => {
    const m = new Map<string, Row[]>()
    for (const r of rows) m.set(r.subsystem, [...(m.get(r.subsystem) ?? []), r])
    return Array.from(m.entries())
      .map(([name, rs]) => ({ name, c: counts(rs), messages: new Set(rs.map((r) => r.entry)).size }))
      .sort((a, b) => b.c.total - a.c.total)
  }, [rows])
  const byTier = useMemo(
    () => TIERS.map((t) => ({ ...t, c: counts(rows.filter((r) => r.tier === t.id)) })),
    [rows],
  )
  const rowById = useMemo(() => new Map(rows.map((r) => [r.parameter_id, r])), [rows])
  const topMovers = useMemo(
    () => movers.slice().sort((a, b) => b.at - a.at).slice(0, 7),
    [movers],
  )
  const attention = useMemo(
    () => rows.filter((r) => r.quality && PROBLEM.includes(r.quality)),
    [rows],
  )

  // The current master frame: which of its 16 subframes have arrived, and
  // which tier blocks each one completed.
  const mf = frames[0]?.masterFrame ?? null
  const inMf = new Map(frames.filter((x) => x.masterFrame === mf).map((x) => [x.subframe, x]))
  const curSf = frames[0]?.subframe ?? null

  const connected = !!f?.connected && live
  const [scDate, scClock] = (f?.spacecraftTime ?? ' ').split(' ')
  const fresh = lastMsgAt != null && now - lastMsgAt < 3000
  // Values on screen are live only while messages keep arriving; otherwise
  // they are the last known readings and must not look healthy.
  const current = connected && fresh
  const running = status?.simulator.running ?? false

  return (
    <div className="dash">
      <div className="dash-inner">
        <div className="dash-head">
          <div>
            <h1>Mission overview</h1>
            <p>Live XMCC telemetry from the XDLINX core team feed, decoded to engineering values.</p>
          </div>
          <div className="fresh">
            Last telemetry <b>{ago(lastMsgAt, now)}</b>
            <br />
            {fresh
              ? 'one message per second'
              : connected
                ? running ? 'stack starting: the OBC boots in about 30 s' : 'connected, no telemetry'
                : 'feed not connected · showing last known values'}
          </div>
        </div>

        <section className="kpis" aria-label="Key figures">
          <div className={`kpi ${connected ? 'good' : 'bad'}`}>
            <div className="k">Link</div>
            <div className="v">{connected ? 'Connected' : 'Offline'}</div>
            <div className="s">{f ? `XMCC · ${f.feedHost}:${f.feedPort}` : 'backend not reachable'}</div>
          </div>
          <div className="kpi">
            <div className="k">Spacecraft time</div>
            <div className="v">{scClock || '—'}</div>
            <div className="s">{scDate ? `${scDate} · UTC` : 'no telemetry yet'}</div>
          </div>
          <div className="kpi">
            <div className="k">Master frame</div>
            <div className="v">{f && f.masterFrame >= 0 ? `#${f.masterFrame}` : '—'}</div>
            <div className="s">{curSf ? `subframe ${curSf} of 16` : '16 subframes = 16 s'}</div>
          </div>
          <div className={`kpi ${current && all.total && all.ok === all.total ? 'good' : ''}`}>
            <div className="k">{current ? 'Parameters OK' : 'Parameters OK · last known'}</div>
            <div className="v">
              {all.ok.toLocaleString()} <small>/ {all.total.toLocaleString()}</small>
            </div>
            <Meter c={all} />
          </div>
          <div className={`kpi ${all.problem ? 'warn' : ''}`}>
            <div className="k">Needs attention</div>
            <div className="v">{all.problem.toLocaleString()}</div>
            <div className="s">
              {all.warmup ? `${all.warmup} warming up · ` : ''}no answer or incomplete
            </div>
          </div>
          <div className={`kpi ${f && f.subframeGaps > 0 ? 'warn' : ''}`}>
            <div className="k">Lost subframes</div>
            <div className="v">{(f?.subframeGaps ?? 0).toLocaleString()}</div>
            <div className="s">{(f?.incompleteBlocks ?? 0).toLocaleString()} incomplete blocks</div>
          </div>
        </section>

        <div className="dash-grid">
          <div className="dash-col">
            <section className="card">
              <div className="card-head">
                <h2>Current master frame</h2>
                <span className="sub">
                  {mf !== null ? `#${mf} · ${inMf.size} of 16 subframes received` : 'waiting for the first subframe'}
                </span>
              </div>
              <div className="mfstrip" role="img"
                   aria-label="Which subframes of the current master frame have arrived, and where each tier block completed">
                <span />
                {Array.from({ length: 16 }, (_, i) => (
                  <span key={i} className={`n ${curSf === i + 1 ? 'cur' : ''}`}>{i + 1}</span>
                ))}
                {TIERS.map((t) => (
                  <FrameRow key={t.id} tier={t.id} span={t.span} inMf={inMf} curSf={curSf} />
                ))}
              </div>
              <div className="mf-legend">
                {TIERS.map((t) => (
                  <span key={t.id}>
                    <i style={{ background: `var(--t-${t.id.toLowerCase()})` }} />
                    {t.id} complete every {t.span === 1 ? '1 s' : `${t.span} s`}
                  </span>
                ))}
                <span><i style={{ background: 'color-mix(in srgb, var(--dim) 14%, var(--panel-2))' }} /> piece held until the block is whole</span>
              </div>
            </section>

            <section className="card">
              <div className="card-head">
                <h2>Subsystem health</h2>
                <span className="sub">{bySubsystem.length} subsystems in the downlink</span>
                <button className="linkbtn act" onClick={() => onOpenTelemetry({})}>Open telemetry</button>
              </div>
              <div className="subsys">
                {bySubsystem.map((s) => (
                  <button key={s.name} className="ss" onClick={() => onOpenTelemetry({ subsystem: s.name })}
                          title={`Show ${s.name} parameters`}>
                    <div className="ss-top">
                      <span className="ss-name">{s.name}</span>
                      <span className="ss-count">{s.c.total} params</span>
                    </div>
                    <Meter c={s.c} />
                    <div className="ss-line">
                      <span className="ok"><b>{s.c.ok}</b> OK</span>
                      {s.c.problem > 0 && <span className="warn"><b>{s.c.problem}</b> attention</span>}
                      {s.c.warmup > 0 && <span className="wu"><b>{s.c.warmup}</b> warming up</span>}
                      <span>{s.messages} {s.messages === 1 ? 'message' : 'messages'}</span>
                    </div>
                  </button>
                ))}
              </div>
            </section>

            <section className="card">
              <div className="card-head">
                <h2>Downlink parts</h2>
                <span className="sub">each 1,280-byte payload carries four parts at fixed positions</span>
              </div>
              <table className="tiertable">
                <thead>
                  <tr>
                    <th>Part</th>
                    <th>Bytes</th>
                    <th>New value every</th>
                    <th style={{ textAlign: 'right' }}>Parameters</th>
                    <th style={{ textAlign: 'right' }}>OK</th>
                    <th style={{ width: '28%' }}>Quality</th>
                  </tr>
                </thead>
                <tbody>
                  {byTier.map((t) => (
                    <tr key={t.id}>
                      <td><span className={`tier ${t.id}`}>{t.id}</span> <span className="dimcol">{t.name}</span></td>
                      <td className="num dimcol" style={{ textAlign: 'left' }}>{t.bytes}</td>
                      <td>{t.span === 1 ? '1 second' : `${t.span} seconds`}</td>
                      <td className="num">{t.c.total}</td>
                      <td className="num">{pct(t.c.ok, t.c.total)}</td>
                      <td><Meter c={t.c} /></td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </section>
          </div>

          <div className="dash-col">
            <section className="card">
              <div className="card-head">
                <h2>Changing now</h2>
                <span className="sub">{movers.length} since this page opened</span>
                {movers.length > 0 && (
                  <button className="linkbtn act"
                          onClick={() => onOpenCharts(topMovers.map((m) => m.id).slice(0, 6))}>
                    Chart these
                  </button>
                )}
              </div>
              <div className="list">
                {topMovers.length === 0 && (
                  <div className="list-empty">
                    No value has changed yet. New readings arrive every second; most parameters hold a
                    constant value in the current feed.
                  </div>
                )}
                {topMovers.map((m) => {
                  const r = rowById.get(m.id)
                  if (!r) return null
                  return (
                    <div className="li" key={m.id}>
                      <span className="li-name" title={r.name}>{r.name}</span>
                      <span className="li-val">
                        <Spark points={m.points} />
                        {displayValue(r.value)}
                      </span>
                      <span className="li-meta">{r.subsystem} · {r.tier} · changed {ago(m.at, now)}</span>
                    </div>
                  )
                })}
              </div>
            </section>

            <section className="card">
              <div className="card-head">
                <h2>Needs attention</h2>
                <span className="sub">{attention.length} {attention.length === 1 ? 'parameter' : 'parameters'}</span>
                {attention.length > 0 && (
                  <button className="linkbtn act" onClick={() => onOpenTelemetry({ onlyProblems: true })}>
                    Show all
                  </button>
                )}
              </div>
              <div className="list">
                {attention.length === 0 && (
                  <div className="list-empty">Every subsystem answered and every block arrived complete.</div>
                )}
                {attention.slice(0, 6).map((r) => (
                  <div className="li" key={r.parameter_id}>
                    <span className="li-name" title={r.name}>{r.name}</span>
                    <span className="li-val">
                      <span className={`q ${r.quality}`}>
                        <span>{r.quality === 'NO_ANSWER' ? 'No answer' : 'Incomplete'}</span>
                      </span>
                    </span>
                    <span className="li-meta">{r.subsystem} · {r.entry} · {r.message}</span>
                  </div>
                ))}
              </div>
            </section>
          </div>
        </div>

        <section className="card">
          <div className="card-head">
            <h2>XDLINX satellite platforms</h2>
            <span className="sub">XDSAT family · from xdlinx.space</span>
            <a className="linkbtn act" href="https://xdlinx.space/platform/" target="_blank" rel="noreferrer">
              All platforms ↗
            </a>
          </div>
          <div className="platforms">
            {PLATFORMS.map((p) => (
              <a className="pf" key={p.id} href={p.url} target="_blank" rel="noreferrer">
                <div className="pf-img"><img src={p.image} alt={p.name} loading="lazy" /></div>
                <div className="pf-body">
                  <div className="pf-name">{p.name}<span className="ext">↗</span></div>
                  <dl>
                    <dt>Bus</dt><dd>{p.bus}</dd>
                    <dt>Payload</dt><dd>{p.payload}</dd>
                    <dt>Power storage</dt><dd>{p.power}</dd>
                    <dt>Solar</dt><dd>{p.solar}</dd>
                    <dt>Heritage</dt><dd>{p.heritage}</dd>
                  </dl>
                </div>
              </a>
            ))}
          </div>
          <div className="xd-links">
            {XDLINX_LINKS.map((l) => (
              <a key={l.url} href={l.url} target="_blank" rel="noreferrer">{l.label}</a>
            ))}
          </div>
        </section>

        <footer className="dash-foot">
          <span>LUT CRC-16 <b>{f?.lutCrc16 ?? '—'}</b></span>
          <span>LUT entries <b>{f?.lutEntries ?? '—'}</b></span>
          <span>Values decoded <b>{(f?.decodedValues ?? 0).toLocaleString()}</b></span>
          <span>Readings stored <b>{(f?.historyRows ?? 0).toLocaleString()}</b></span>
          <span>Subframes received <b>{(f?.subframesReceived ?? 0).toLocaleString()}</b></span>
        </footer>
      </div>
    </div>
  )
}

function FrameRow({
  tier,
  span,
  inMf,
  curSf,
}: {
  tier: string
  span: number
  inMf: Map<number, Frame>
  curSf: number | null
}) {
  return (
    <>
      <span className="rl">{tier}</span>
      {Array.from({ length: 16 }, (_, i) => {
        const sf = i + 1
        const fr = inMf.get(sf)
        const done = !!fr && fr.completes.includes(tier)
        const cls = !fr ? '' : done ? `done-${tier}` : 'part'
        return (
          <span
            key={sf}
            className={`c ${cls} ${curSf === sf ? 'cur' : ''}`}
            title={!fr ? `subframe ${sf}: not received yet` : done
              ? `subframe ${sf}: ${tier} block complete, values decoded`
              : `subframe ${sf}: ${tier} piece ${((sf - 1) % span) + 1} of ${span}`}
          />
        )
      })}
    </>
  )
}
