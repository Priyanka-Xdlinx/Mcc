import { useEffect, useRef, useState } from 'react'
import { Chart } from './Chart'
import type { Row } from '../types'
import { displayValue } from '../format'

interface HistoryRow { spacecraft_time: string | null; received_at: string | null; value_num: number | null }
interface Series { t: number[]; y: number[] }

const COLORS = ['--s1', '--s2', '--s3', '--s4']

function timeOf(h: HistoryRow): number {
  if (h.spacecraft_time) {
    const t = Date.parse(h.spacecraft_time.replace(' ', 'T') + 'Z')
    if (Number.isFinite(t)) return t
  }
  return h.received_at ? Date.parse(h.received_at) : NaN
}

/** The last ten minutes of up to four changing parameters, refreshed every 3 s. */
export function Trends({ ids, rows, live, onOpen }: { ids: number[]; rows: Map<number, Row>; live: boolean; onOpen: () => void }) {
  const [data, setData] = useState<Record<number, Series>>({})
  const req = useRef(0)
  const key = ids.join(',')

  useEffect(() => {
    if (!ids.length) {
      setData({})
      return
    }
    let stop = false
    const load = async () => {
      const mine = ++req.current
      try {
        const out = await Promise.all(
          ids.map(async (id): Promise<[number, Series]> => {
            const r = await fetch(`/api/telemetry/${id}/history?limit=600`)
            if (!r.ok) throw new Error(String(r.status))
            const h: HistoryRow[] = await r.json()
            const pts = h
              .filter((x) => x.value_num !== null)
              .map((x) => ({ t: timeOf(x), y: x.value_num as number }))
              .filter((p) => Number.isFinite(p.t))
              .sort((a, b) => a.t - b.t)
            const from = pts.length ? pts[pts.length - 1].t - 600_000 : 0
            const w = pts.filter((p) => p.t >= from)
            return [id, { t: w.map((p) => p.t), y: w.map((p) => p.y) }]
          }),
        )
        if (!stop && mine === req.current) setData(Object.fromEntries(out))
      } catch {
        /* backend unreachable: keep the last charts */
      }
    }
    void load()
    if (!live) return () => void (stop = true)
    const t = setInterval(load, 3000)
    return () => {
      stop = true
      clearInterval(t)
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [key, live])

  return (
    <section className="card">
      <div className="card-head">
        <h2>Live trends</h2>
        <span className="sub">last 10 minutes · UTC · drag to zoom</span>
        {ids.length > 0 && <button className="linkbtn act" onClick={onOpen}>Open in Charts</button>}
      </div>
      {ids.length === 0 ? (
        <div className="list-empty">Charts appear here as soon as parameters start to change.</div>
      ) : (
        <div className="trends">
          {ids.map((id, i) => {
            const r = rows.get(id)
            const s = data[id]
            if (!r) return null
            return (
              <div className="trend" key={id}>
                <div className="trend-head">
                  <span className="swatch" style={{ background: `var(${COLORS[i]})` }} />
                  <span className="trend-name" title={`${r.subsystem} · ${r.entry} · ${r.message}`}>{r.name}</span>
                  <span className="trend-val">{displayValue(r.value)}</span>
                </div>
                <Chart
                  x={s?.t ?? []}
                  ys={[s?.y ?? []]}
                  series={[{ label: r.name, unit: r.unit ?? undefined, colorVar: COLORS[i] }]}
                  height={120}
                  ariaLabel={`${r.name}, last ten minutes`}
                />
              </div>
            )
          })}
        </div>
      )}
    </section>
  )
}
