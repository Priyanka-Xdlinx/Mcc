import { useEffect, useRef, useState } from "react";
import earthDay from "../assets/earth-day.jpg";
import earthNight from "../assets/earth-night.jpg";
import satelliteImg from "../assets/satellite.png";
import { fmtLat, fmtLon, footprint, splitAtAntimeridian, subsolar, terminatorLat } from "../utils/geo.js";

function useImage(src) {
  const [img, setImg] = useState(null);
  useEffect(() => {
    const i = new Image();
    i.onload = () => setImg(i);
    i.src = src;
  }, [src]);
  return img;
}

/** Draws the whole map onto a 2D context `w` × `h`. */
function draw(ctx, w, h, { day, night, sat, track, current, nodes }) {
  const X = (lon) => ((lon + 180) / 360) * w;
  const Y = (lat) => ((90 - lat) / 180) * h;

  ctx.fillStyle = "#0a1522";
  ctx.fillRect(0, 0, w, h);
  if (day) ctx.drawImage(day, 0, 0, w, h);

  // Night side: city lights clipped to the dark hemisphere, plus a shade.
  const ss = subsolar(current ? new Date(current.t) : new Date());
  const nightPole = ss.lat >= 0 ? -90 : 90;
  ctx.save();
  ctx.beginPath();
  for (let lon = -180; lon <= 180; lon += 2) {
    const la = terminatorLat(lon, ss.lat, ss.lon);
    if (lon === -180) ctx.moveTo(X(lon), Y(la));
    else ctx.lineTo(X(lon), Y(la));
  }
  ctx.lineTo(X(180), Y(nightPole));
  ctx.lineTo(X(-180), Y(nightPole));
  ctx.closePath();
  ctx.clip();
  if (night) ctx.drawImage(night, 0, 0, w, h);
  ctx.fillStyle = "rgba(2, 8, 20, 0.42)";
  ctx.fillRect(0, 0, w, h);
  ctx.restore();

  // Graticule every 30°.
  ctx.strokeStyle = "rgba(255,255,255,0.13)";
  ctx.lineWidth = 1;
  ctx.font = '10px "IBM Plex Mono", monospace';
  ctx.fillStyle = "rgba(235,242,247,0.7)";
  for (let lon = -150; lon <= 150; lon += 30) {
    ctx.beginPath();
    ctx.moveTo(X(lon) + 0.5, 0);
    ctx.lineTo(X(lon) + 0.5, h);
    ctx.stroke();
    ctx.fillText(`${Math.abs(lon)}°${lon < 0 ? "W" : lon > 0 ? "E" : ""}`, X(lon) + 3, h - 5);
  }
  for (let lat = -60; lat <= 60; lat += 30) {
    ctx.beginPath();
    ctx.moveTo(0, Y(lat) + 0.5);
    ctx.lineTo(w, Y(lat) + 0.5);
    ctx.stroke();
    ctx.fillText(`${Math.abs(lat)}°${lat < 0 ? "S" : lat > 0 ? "N" : ""}`, 4, Y(lat) - 4);
  }

  // Ground track: older passes fainter, split at the antimeridian.
  const n = track.length;
  const drawSegs = (lineW, color) => {
    let idx = 0;
    for (const seg of splitAtAntimeridian(track)) {
      for (let i = 1; i < seg.length; i++) {
        ctx.strokeStyle = color(n > 1 ? (idx + i) / (n - 1) : 1);
        ctx.lineWidth = lineW;
        ctx.beginPath();
        ctx.moveTo(X(seg[i - 1].lon), Y(seg[i - 1].lat));
        ctx.lineTo(X(seg[i].lon), Y(seg[i].lat));
        ctx.stroke();
      }
      idx += seg.length;
    }
  };
  ctx.lineCap = "round";
  drawSegs(4, (a) => `rgba(3, 10, 18, ${0.25 + 0.35 * a})`); // outline, readable over bright land
  drawSegs(2, (a) => `rgba(34, 184, 216, ${0.25 + 0.75 * a})`);

  // Ascending nodes (northbound equator crossings).
  ctx.fillStyle = "#ffffff";
  for (const nd of nodes) {
    ctx.beginPath();
    ctx.moveTo(X(nd.lon), Y(0) - 5);
    ctx.lineTo(X(nd.lon) + 4, Y(0) + 3);
    ctx.lineTo(X(nd.lon) - 4, Y(0) + 3);
    ctx.closePath();
    ctx.fill();
  }

  // Sub-solar point.
  ctx.fillStyle = "#ffd866";
  ctx.strokeStyle = "rgba(0,0,0,0.5)";
  ctx.beginPath();
  ctx.arc(X(ss.lon), Y(ss.lat), 6, 0, Math.PI * 2);
  ctx.fill();
  ctx.stroke();

  if (!current) return;
  // Visibility footprint.
  ctx.setLineDash([5, 4]);
  ctx.strokeStyle = "rgba(255, 216, 102, 0.9)";
  ctx.lineWidth = 1.5;
  const fp = footprint(current.lat, current.lon, current.altKm, 3).map(([lat, lon]) => ({ lat, lon }));
  for (const seg of splitAtAntimeridian(fp)) {
    ctx.beginPath();
    seg.forEach((p, i) => (i ? ctx.lineTo(X(p.lon), Y(p.lat)) : ctx.moveTo(X(p.lon), Y(p.lat))));
    ctx.stroke();
  }
  ctx.setLineDash([]);
  // Spacecraft.
  const sx = X(current.lon);
  const sy = Y(current.lat);
  ctx.fillStyle = "rgba(34, 184, 216, 0.25)";
  ctx.beginPath();
  ctx.arc(sx, sy, 16, 0, Math.PI * 2);
  ctx.fill();
  if (sat) {
    const iw = Math.max(30, Math.min(46, w / 26));
    const ih = (iw * 239) / 305;
    ctx.drawImage(sat, sx - iw / 2, sy - ih / 2, iw, ih);
  } else {
    ctx.fillStyle = "#22b8d8";
    ctx.beginPath();
    ctx.arc(sx, sy, 5, 0, Math.PI * 2);
    ctx.fill();
  }
}

/**
 * Equirectangular ground track: day map with city lights across the night
 * side, older passes fainter, the visibility footprint and the sub-solar point.
 * Hover to read a position.
 */
export default function GroundMap({ track, current, nodes }) {
  const box = useRef(null);
  const canvas = useRef(null);
  const [w, setW] = useState(900);
  const [hover, setHover] = useState(null);
  const day = useImage(earthDay);
  const night = useImage(earthNight);
  const sat = useImage(satelliteImg);
  const h = Math.round(w / 2);

  useEffect(() => {
    const el = box.current;
    if (!el) return undefined;
    const ro = new ResizeObserver(() => setW(Math.max(280, Math.floor(el.clientWidth))));
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  useEffect(() => {
    const c = canvas.current;
    if (!c) return;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    c.width = w * dpr;
    c.height = h * dpr;
    const ctx = c.getContext("2d");
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    draw(ctx, w, h, { day, night, sat, track, current, nodes });
  }, [w, h, day, night, sat, track, current, nodes]);

  const onMove = (e) => {
    const r = canvas.current.getBoundingClientRect();
    const x = e.clientX - r.left;
    const y = e.clientY - r.top;
    setHover({ x, y, lon: (x / r.width) * 360 - 180, lat: 90 - (y / r.height) * 180 });
  };

  return (
    <div className="gmap" ref={box}>
      <canvas
        ref={canvas}
        style={{ width: w, height: h }}
        onMouseMove={onMove}
        onMouseLeave={() => setHover(null)}
        role="img"
        aria-label={
          current ? `Ground track map. Spacecraft at ${fmtLat(current.lat)}, ${fmtLon(current.lon)}.` : "Ground track map, waiting for a position."
        }
      />
      {hover && (
        <div className="gmap-tip" style={{ left: Math.min(hover.x + 12, w - 170), top: Math.max(hover.y - 30, 4) }}>
          {fmtLat(hover.lat)} · {fmtLon(hover.lon)}
        </div>
      )}
    </div>
  );
}
