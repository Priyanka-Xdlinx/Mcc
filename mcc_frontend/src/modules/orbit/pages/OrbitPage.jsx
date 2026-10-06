import { useMemo, useState } from "react";
import { useSelector } from "react-redux";
import Chart from "../../../components/Chart.jsx";
import { selectLive, selectRows } from "../../../redux/telemetry/index.js";
import GroundMap from "../components/GroundMap.jsx";
import Globe from "../components/Globe.jsx";
import useGroundTrack from "../hooks/useGroundTrack.js";
import { fmtDuration, fmtLat, fmtLon, footprintRadiusKm, orbitMetrics, splitAtAntimeridian } from "../utils/geo.js";
import "../orbit.css";

const findGps = (rows, message, name) => rows.find((r) => r.subsystem === "GPS" && r.message === message && r.name === name);

/**
 * Ground track: where the spacecraft is, from the GPS position it reports, on a
 * 3D globe and a flat map, with orbit figures measured from the track itself.
 */
export default function OrbitPage() {
  const rows = useSelector(selectRows);
  const live = useSelector(selectLive);
  const { gps, track, current } = useGroundTrack(rows, live);
  const [follow, setFollow] = useState(true);
  const [resetKey, setResetKey] = useState(0);

  const svs = findGps(rows, "BESTXYZ", "num_svs");
  const sol = findGps(rows, "BESTXYZ", "position_solution_status");
  const pdop = findGps(rows, "PSRDOP2", "pdop");

  const m = useMemo(() => orbitMetrics(track), [track]);
  const oneOrbit = m.ascendingNodes.length >= 2;

  // Longitude gets a gap at each ±180° wrap instead of a vertical line.
  const chart = useMemo(() => {
    const lonY = [];
    splitAtAntimeridian(track).forEach((seg, i) => {
      if (i > 0) lonY[lonY.length - 1] = null;
      seg.forEach((p) => lonY.push(p.lon));
    });
    return { x: track.map((p) => p.t), lat: track.map((p) => p.lat), lon: lonY, alt: track.map((p) => p.altKm) };
  }, [track]);

  if (!gps) {
    return (
      <div className="dash feature">
        <div className="dash-inner">
          <div className="dash-head">
            <div>
              <h1>Ground track</h1>
              <p>The current LUT does not downlink the GPS BESTPOS position (lat, lon, hgt), so there is nothing to plot.</p>
            </div>
          </div>
        </div>
      </div>
    );
  }

  const sc = current ? new Date(current.t) : null;
  const lastNode = m.ascendingNodes[m.ascendingNodes.length - 1];
  const quality = gps.lat.quality;

  return (
    <div className="dash orbitpage feature">
      <section className="mission" aria-label="Orbit view">
        <Globe track={track} current={current} follow={follow} onManual={() => setFollow(false)} resetKey={resetKey} />

        <div className="hud hud-title">
          <div className="hud-eyebrow">XDSAT · live orbit</div>
          <h1>Ground track</h1>
          <div className="hud-sub">GPS BESTPOS from the XMCC feed · {track.length.toLocaleString()} positions this run</div>
        </div>

        <div className="hud hud-clock" aria-label="Spacecraft time">
          <div className="hud-eyebrow">Spacecraft time · UTC</div>
          <div className="clock">{sc ? sc.toISOString().slice(11, 19) : "--:--:--"}</div>
          <div className="hud-sub">{sc ? sc.toISOString().slice(0, 10) : "waiting for a position"}</div>
        </div>

        <div className="hud hud-panel hud-left">
          <div className="readout">
            <span>Latitude</span>
            <b>{current ? fmtLat(current.lat) : "—"}</b>
            <i className={`heading ${m.heading ?? ""}`}>
              {m.heading === "north" ? "▲ northbound" : m.heading === "south" ? "▼ southbound" : ""}
            </i>
          </div>
          <div className="readout">
            <span>Longitude</span>
            <b>{current ? fmtLon(current.lon) : "—"}</b>
          </div>
          <div className="readout">
            <span>Height</span>
            <b>
              {current ? current.altKm.toFixed(1) : "—"}
              <small> km</small>
            </b>
          </div>
        </div>

        <div className="hud hud-panel hud-right">
          <div className="hud-eyebrow">Measured from the track</div>
          <dl>
            <dt>Inclination</dt>
            <dd>{m.maxAbsLat === null ? "—" : `${oneOrbit ? "" : "≥ "}${m.maxAbsLat.toFixed(1)}°`}</dd>
            <dt>Orbit period</dt>
            <dd>{m.periodSec ? fmtDuration(m.periodSec) : "measuring…"}</dd>
            <dt>Orbits seen</dt>
            <dd>{Math.max(0, m.ascendingNodes.length - 1)}</dd>
            <dt>Last node</dt>
            <dd>{lastNode ? fmtLon(lastNode.lon) : "—"}</dd>
            <dt>Footprint</dt>
            <dd>{current ? `${Math.round(footprintRadiusKm(current.altKm)).toLocaleString()} km` : "—"}</dd>
          </dl>
          <div className="hud-eyebrow" style={{ marginTop: 12 }}>
            GPS receiver
          </div>
          <dl>
            <dt>Solution</dt>
            <dd>{sol?.value?.replace(/^\d+ \((.*)\)$/, "$1") ?? "—"}</dd>
            <dt>Satellites</dt>
            <dd>{svs?.value ?? "—"}</dd>
            <dt>PDOP</dt>
            <dd>{pdop?.value ?? "—"}</dd>
            <dt>Quality</dt>
            <dd className={quality === "OK" ? "ok" : "warn"}>{quality ?? "no data"}</dd>
          </dl>
        </div>

        <div className="hud hud-bar">
          <label className="hud-toggle">
            <input type="checkbox" checked={follow} onChange={(e) => setFollow(e.target.checked)} />
            <span>Follow spacecraft</span>
          </label>
          <button
            className="hud-btn"
            onClick={() => {
              setFollow(true);
              setResetKey((k) => k + 1);
            }}
          >
            Recenter
          </button>
          <div className="hud-legend" aria-hidden="true">
            <span>
              <i className="lg-orbit" />
              orbit
            </span>
            <span>
              <i className="lg-ground" />
              ground track
            </span>
            <span>
              <i className="lg-foot" />
              area in view
            </span>
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
          <div className="card-head">
            <h2>Position over time</h2>
            <span className="sub">UTC · drag to zoom</span>
          </div>
          <div className="orbit-charts">
            {[
              ["Latitude", "°", chart.lat, "--s1"],
              ["Longitude", "°", chart.lon, "--s2"],
              ["Height", "km", chart.alt, "--s3"],
            ].map(([label, unit, y, color]) => (
              <div key={label}>
                <div className="oc-title">
                  {label} <span>{unit}</span>
                </div>
                <Chart x={chart.x} ys={[y]} series={[{ label, unit, colorVar: color }]} height={170} ariaLabel={`${label} over time`} />
              </div>
            ))}
          </div>
          <p className="pos-note" style={{ marginTop: 10 }}>
            Inclination and period are measured from the positions received, not from an orbit model: the highest latitude
            reached, and the mean time between northbound equator crossings.
          </p>
        </section>
      </div>
    </div>
  );
}
