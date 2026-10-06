// Geometry for the ground-track page. A track point is `{ t, lat, lon, altKm }`:
// t in ms (UTC), degrees, longitude -180..180, height in km.

export const RE = 6378.137; // Earth equatorial radius, km
export const DEG = 180 / Math.PI;

/** Unit-sphere position (scaled by R): +Y is north. */
export function llaToXyz(lat, lon, altKm, R) {
  const r = ((RE + altKm) / RE) * R;
  const phi = ((lon + 180) / 360) * Math.PI * 2;
  const th = ((90 - lat) / 180) * Math.PI;
  return [-r * Math.cos(phi) * Math.sin(th), r * Math.cos(th), r * Math.sin(phi) * Math.sin(th)];
}

/** Point on Earth where the sun is overhead (low-precision solar almanac, ±0.1°). */
export function subsolar(date) {
  const n = date.getTime() / 86400000 + 2440587.5 - 2451545.0;
  const L = (280.46 + 0.9856474 * n) % 360;
  const g = ((357.528 + 0.9856003 * n) % 360) / DEG;
  const lam = (L + 1.915 * Math.sin(g) + 0.02 * Math.sin(2 * g)) / DEG;
  const eps = 23.439 / DEG;
  const dec = Math.asin(Math.sin(eps) * Math.sin(lam)) * DEG;
  const utc = date.getUTCHours() + date.getUTCMinutes() / 60 + date.getUTCSeconds() / 3600;
  const eqt = 4 * (L - 0.0057183 - Math.atan2(Math.cos(eps) * Math.sin(lam), Math.cos(lam)) * DEG);
  return { lat: dec, lon: ((-15 * (utc - 12 + eqt / 60) + 540) % 360) - 180 };
}

/** The circle on the ground the spacecraft can see down to 0° elevation, as [lat, lon] pairs. */
export function footprint(lat, lon, altKm, stepDeg = 4) {
  const lam = Math.acos(RE / (RE + altKm));
  const la = lat / DEG;
  const lo = lon / DEG;
  const pts = [];
  for (let b = 0; b <= 360; b += stepDeg) {
    const br = b / DEG;
    const pl = Math.asin(Math.sin(la) * Math.cos(lam) + Math.cos(la) * Math.sin(lam) * Math.cos(br));
    const pn = lo + Math.atan2(Math.sin(br) * Math.sin(lam) * Math.cos(la), Math.cos(lam) - Math.sin(la) * Math.sin(pl));
    pts.push([pl * DEG, ((pn * DEG + 540) % 360) - 180]);
  }
  return pts;
}

/** Footprint radius on the ground, km (great-circle, 0° elevation). */
export const footprintRadiusKm = (altKm) => Math.acos(RE / (RE + altKm)) * RE;

/** Latitude of the day/night line at a given longitude, for a sun at (sLat, sLon). */
export function terminatorLat(lon, sLat, sLon) {
  const t = Math.tan(sLat / DEG);
  if (Math.abs(t) < 1e-6) return 0;
  return Math.atan(-Math.cos((lon - sLon) / DEG) / t) * DEG;
}

/** Split a track wherever it jumps across the ±180° meridian, so lines never cross the map. */
export function splitAtAntimeridian(pts) {
  const out = [];
  let cur = [];
  for (const p of pts) {
    const prev = cur[cur.length - 1];
    if (prev && Math.abs(p.lon - prev.lon) > 180) {
      out.push(cur);
      cur = [];
    }
    cur.push(p);
  }
  if (cur.length) out.push(cur);
  return out;
}

/**
 * Orbit figures measured from the track itself, not from any orbit model:
 *   maxAbsLat       highest |latitude| reached (the inclination, once a full orbit is seen)
 *   ascendingNodes  northbound equator crossings `{ t, lon }`, interpolated
 *   periodSec       mean time between ascending nodes
 *   heading         "north" | "south" from the last two points
 */
export function orbitMetrics(track) {
  const nodes = [];
  let maxAbs = null;
  for (let i = 0; i < track.length; i++) {
    const p = track[i];
    maxAbs = Math.max(maxAbs ?? 0, Math.abs(p.lat));
    if (i === 0) continue;
    const a = track[i - 1];
    // A gap of more than a minute is a data gap, not a crossing we can time.
    if (a.lat < 0 && p.lat >= 0 && p.t - a.t < 60_000) {
      const f = -a.lat / (p.lat - a.lat);
      let dLon = p.lon - a.lon;
      if (dLon > 180) dLon -= 360;
      if (dLon < -180) dLon += 360;
      nodes.push({ t: a.t + f * (p.t - a.t), lon: ((a.lon + f * dLon + 540) % 360) - 180 });
    }
  }
  const periods = nodes.slice(1).map((n, i) => (n.t - nodes[i].t) / 1000);
  const n = track.length;
  return {
    maxAbsLat: maxAbs,
    ascendingNodes: nodes,
    periodSec: periods.length ? periods.reduce((s, x) => s + x, 0) / periods.length : null,
    heading: n >= 2 ? (track[n - 1].lat >= track[n - 2].lat ? "north" : "south") : null,
  };
}

export const fmtLat = (v) => `${Math.abs(v).toFixed(3)}° ${v >= 0 ? "N" : "S"}`;
export const fmtLon = (v) => `${Math.abs(v).toFixed(3)}° ${v >= 0 ? "E" : "W"}`;

export function fmtDuration(sec) {
  const m = Math.floor(sec / 60);
  const s = Math.round(sec % 60);
  return m ? `${m} min ${String(s).padStart(2, "0")} s` : `${s} s`;
}
