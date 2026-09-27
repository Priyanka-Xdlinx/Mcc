import { useEffect, useMemo, useRef, useState } from 'react'
import type { Row } from '../../types'
import { Chart } from '../Chart'
import { Globe } from './Globe'
import { GroundMap } from './GroundMap'
import { fmtDuration, fmtLat, fmtLon, footprintRadiusKm, orbitMetrics, splitAtAntimeridian, type TrackPoint } from './geo'

interface HistoryRow { spacecraft_time: string | null; received_at: string | null; value_num: number | null }

/** Spacecraft time is UTC calendar time ("2026-09-26 18:52:56.883"). */
function timeOf(s: string | null, fallback: string | null): number {
  if (s) {
    const t = Date.parse(s.replace(' ', 'T') + 'Z')
    if (Number.isFinite(t)) return t
  }
  return fallback ? Date.parse(fallback) : NaN
}

const find = (rows: Row[], message: string, name: string) =>
  rows.find((r) => r.subsystem === 'GPS' && r.message === message && r.name === name)

/**
 * Ground track: where the spacecraft is, from the GPS BESTPOS position it reports
 * (lat, lon, hgt), on a 3D globe and a flat map, with orbit figures measured from
 * the track itself.
 */
export default function OrbitPage({ rows, live }: { rows: Row[]; live: boolean }) {
  const lat = find(rows, 'BESTPOS', 'lat')
  const lon = find(rows, 'BESTPOS', 'lon')
  const hgt = find(rows, 'BESTPOS', 'hgt')
  const svs = find(rows, 'BESTXYZ', 'num_svs')
  const sol = find(rows, 'BESTXYZ', 'position_solution_status')
  const pdop = find(rows, 'PSRDOP2', 'pdop')
  const ids = lat && lon && hgt ? [lat.parameter_id, lon.parameter_id, hgt.parameter_id] : null

  const [history, setHistory] = useState<TrackPoint[]>([])
  const [follow, setFollow] = useState(true)
  const [resetKey, setResetKey] = useState(0)
  const req = useRef(0)

  // Stored history for the current run, refreshed every 4 s; the live value fills in between.
  useEffect(() => {
    if (!ids) return
    let stop = false
    const load = async () => {
      const mine = ++req.current
      try {
        const [la, lo, al] = await Promise.all(
          ids.map(async (id) => {
            const r = await fetch(`/api/telemetry/${id}/history?limit=5000`)
            if (!r.ok) throw new Error(String(r.status))
            return (await r.json()) as HistoryRow[]
          }),
        )
        const byTime = (h: HistoryRow[]) => new Map(h.filter((x) => x.value_num !== null).map((x) => [x.spacecraft_time ?? x.received_at, x]))
        const lonMap = byTime(lo)
        const altMap = byTime(al)
        const pts: TrackPoint[] = []
        for (const x of la) {
          if (x.value_num === null) continue
          const key = x.spacecraft_time ?? x.received_at
          const l = lonMap.get(key)
          if (!l || l.value_num === null) continue
          const a = altMap.get(key)
          const t = timeOf(x.spacecraft_time, x.received_at)
          if (!Number.isFinite(t)) continue
          pts.push({ t, lat: x.value_num, lon: l.value_num, altKm: (a?.value_num ?? 0) / 1000 })
        }
        pts.sort((a, b) => a.t - b.t)
        if (!stop && mine === req.current) setHistory(pts)
      } catch {
        /* backend unreachable: keep the last track */
      }
    }
    void load()
    if (!live) return () => void (stop = true)
    const t = setInterval(load, 4000)
    return () => {
      stop = true
      clearInterval(t)
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [ids?.join(','), live])

  // The position right now, straight from the live stream (one message per second).
  const current = useMemo<TrackPoint | null>(() => {
    if (!lat || !lon || lat.value_num === null || lon.value_num === null || lat.quality !== 'OK' || lon.quality !== 'OK') {
      return history[history.length - 1] ?? null
    }
    const t = timeOf(lat.spacecraft_time, lat.received_at)
    return { t: Number.isFinite(t) ? t : Date.now(), lat: lat.value_num, lon: lon.value_num, altKm: (hgt?.value_num ?? 0) / 1000 }
  }, [lat, lon, hgt, history])

  const track = useMemo(() => {
    const last = history[history.length - 1]
    return current && (!last || current.t > last.t) ? [...history, current] : history
  }, [history, current])

  const m = useMemo(() => orbitMetrics(track), [track])
  const oneOrbit = m.ascendingNodes.length >= 2

  // Charts: longitude gets a gap at each ±180° wrap instead of a vertical line.
  const chart = useMemo(() => {
    const x = track.map((p) => p.t)
    const lonY: (number | null)[] = []
    splitAtAntimeridian(track).forEach((seg, i) => {
      if (i > 0) lonY[lonY.length - 1] = null
      seg.forEach((p) => lonY.push(p.lon))
    })
    return { x, lat: track.map((p) => p.lat), lon: lonY, alt: track.map((p) => p.altKm) }
  }, [track])

  if (!ids) {
    return (
      <div className="dash"><div className="dash-inner">
        <div className="dash-head"><div><h1>Ground track</h1><p>The current LUT does not downlink the GPS BESTPOS position (lat, lon, hgt), so there is nothing to plot.</p></div></div>
      </div></div>
    )
  }

  const sc = current ? new Date(current.t) : null
  const lastNode = m.ascendingNodes[m.ascendingNodes.length - 1]
  return (
    <div className="dash orbitpage">
      <section className="mission" aria-label="Orbit view">
        <Globe
          track={track}
          current={current}
          follow={follow}
          onManual={() => setFollow(false)}
          resetKey={resetKey}
        />

        <div className="hud hud-title">
          <div className="hud-eyebrow">XDSAT · live orbit</div>
          <h1>Ground track</h1>
          <div className="hud-sub">GPS BESTPOS from the XMCC feed · {track.length.toLocaleString()} positions this run</div>
        </div>

        <div className="hud hud-clock" aria-label="Spacecraft time">
          <div className="hud-eyebrow">Spacecraft time · UTC</div>
          <div className="clock">{sc ? sc.toISOString().slice(11, 19) : '--:--:--'}</div>
          <div className="hud-sub">{sc ? sc.toISOString().slice(0, 10) : 'waiting for a position'}</div>
        </div>

        <div className="hud hud-panel hud-left">
          <div className="readout">
            <span>Latitude</span>
            <b>{current ? fmtLat(current.lat) : '—'}</b>
            <i className={`heading ${m.heading ?? ''}`}>{m.heading === 'north' ? '▲ northbound' : m.heading === 'south' ? '▼ southbound' : ''}</i>
          </div>
          <div className="readout">
            <span>Longitude</span>
            <b>{current ? fmtLon(current.lon) : '—'}</b>
          </div>
          <div className="readout">
            <span>Height</span>
            <b>{current ? `${current.altKm.toFixed(1)}` : '—'}<small> km</small></b>
          </div>
        </div>

        <div className="hud hud-panel hud-right">
          <div className="hud-eyebrow">Measured from the track</div>
          <dl>
            <dt>Inclination</dt>
            <dd>{m.maxAbsLat === null ? '—' : oneOrbit ? `${m.maxAbsLat.toFixed(1)}°` : `≥ ${m.maxAbsLat.toFixed(1)}°`}</dd>
            <dt>Orbit period</dt>
            <dd>{m.periodSec ? fmtDuration(m.periodSec) : 'measuring…'}</dd>
            <dt>Orbits seen</dt>
            <dd>{Math.max(0, m.ascendingNodes.length - 1)}</dd>
            <dt>Last node</dt>
            <dd>{lastNode ? fmtLon(lastNode.lon) : '—'}</dd>
            <dt>Footprint</dt>
            <dd>{current ? `${Math.round(footprintRadiusKm(current.altKm)).toLocaleString()} km` : '—'}</dd>
          </dl>
          <div className="hud-eyebrow" style={{ marginTop: 12 }}>GPS receiver</div>
          <dl>
            <dt>Solution</dt><dd>{sol?.value?.replace(/^\d+ \((.*)\)$/, '$1') ?? '—'}</dd>
            <dt>Satellites</dt><dd>{svs?.value ?? '—'}</dd>
            <dt>PDOP</dt><dd>{pdop?.value ?? '—'}</dd>
            <dt>Quality</dt><dd className={lat?.quality === 'OK' ? 'ok' : 'warn'}>{lat?.quality === 'OK' ? 'OK' : lat?.quality ?? 'no data'}</dd>
          </dl>
        </div>

        <div className="hud hud-bar">
          <label className="hud-toggle">
            <input type="checkbox" checked={follow} onChange={(e) => setFollow(e.target.checked)} />
            <span>Follow spacecraft</span>
          </label>
          <button className="hud-btn" onClick={() => { setFollow(true); setResetKey((k) => k + 1) }}>Recenter</button>
          <div className="hud-legend" aria-hidden="true">
            <span><i className="lg-orbit" />orbit</span>
            <span><i className="lg-ground" />ground track</span>
            <span><i className="lg-foot" />area in view</span>
          </div>
          <span className="hud-hint">drag to turn · scroll to zoom</span>
        </div>
      </section>

      <div className="dash-inner">
        <section className="card">
          <div className="card-head">
            <h2>Ground track map</h2>
            <span className="sub">this run · older passes fainter · ▲ northbound equator crossing · ● sun overhead · dashed: area in view</span>
          </div>
          <GroundMap track={track} current={current} nodes={m.ascendingNodes} />
        </section>

        <section className="card">
          <div className="card-head"><h2>Position over time</h2><span className="sub">UTC · drag to zoom</span></div>
          <div className="orbit-charts">
            <div>
              <div className="oc-title">Latitude <span>°</span></div>
              <Chart x={chart.x} ys={[chart.lat]} series={[{ label: 'Latitude', unit: '°', colorVar: '--s1' }]} height={170} ariaLabel="Latitude over time" />
            </div>
            <div>
              <div className="oc-title">Longitude <span>°</span></div>
              <Chart x={chart.x} ys={[chart.lon]} series={[{ label: 'Longitude', unit: '°', colorVar: '--s2' }]} height={170} ariaLabel="Longitude over time" />
            </div>
            <div>
              <div className="oc-title">Height <span>km</span></div>
              <Chart x={chart.x} ys={[chart.alt]} series={[{ label: 'Height', unit: 'km', colorVar: '--s3' }]} height={170} ariaLabel="Height over time" />
            </div>
          </div>
          <p className="pos-note" style={{ marginTop: 10 }}>
            Inclination and period are measured from the positions received, not from an orbit model: the highest
            latitude reached, and the mean time between northbound equator crossings.
          </p>
        </section>
      </div>
    </div>
  )
}
