import { useEffect, useRef, useState } from "react";
import uPlot from "uplot";
import "uplot/dist/uPlot.min.css";
import useResolvedTheme from "../hooks/useResolvedTheme.js";
import { AXIS_CHAR_PX, displayRange, fmtFull, SERIES_COLORS, timeTicks, xTickSpace, zoomRange } from "../utils/chart.js";
import "./chart.css";

const utc = (sec) => uPlot.tzDate(new Date(sec * 1000), "Etc/UTC");

/** Plot width for a host element: whole pixels, never below a usable minimum. */
const widthOf = (el, w = el.getBoundingClientRect().width) => Math.max(120, Math.floor(w));

function cssVar(name, fallback) {
  try {
    return getComputedStyle(document.documentElement).getPropertyValue(name).trim() || fallback;
  } catch {
    return fallback;
  }
}

const defaultFormat = (v) => (Math.abs(v) >= 1000 ? v.toFixed(0) : String(+v.toFixed(3)));

/**
 * Time-series chart (uPlot), UTC. `x` is ms since epoch; `ys` one array per
 * series, null for a gap. `series` is `[{ label, unit?, colorVar? }]`; series
 * with different units get their own y axis.
 *
 * The plot is built once per series shape, height and theme and fed with
 * setData afterwards, which keeps 1 Hz updates across many charts cheap.
 * Drag across it to zoom; the zoom is kept while new data arrives.
 */
export default function Chart({ x, ys, series, height = 130, ariaLabel, format = defaultFormat, spanGaps = false }) {
  const host = useRef(null);
  const plot = useRef(null);
  const zoomRef = useRef(null);
  const [zoomed, setZoomed] = useState(false);
  const resolved = useResolvedTheme();
  const scaleOf = (s) => s.unit ?? "";
  const scales = [...new Set(series.map(scaleOf))];
  const shape = series.map((s) => `${s.label}${s.colorVar ?? ""}@${scaleOf(s)}`).join("|") + (spanGaps ? "g" : "");

  const toData = () => [Float64Array.from(x, (v) => v / 1000), ...ys];

  useEffect(() => {
    const el = host.current;
    if (!el) return undefined;
    const grid = cssVar("--grid", "#e3e8eb");
    const muted = cssVar("--muted", "#6f7e89");
    const axisFont = '10px "IBM Plex Mono", monospace';
    const scaleKey = (i) => (i === 0 ? "y" : `y${i}`);
    const opts = {
      width: widthOf(el),
      height,
      tzDate: utc,
      legend: { show: series.length > 1, live: false },
      cursor: { drag: { x: true, y: false, setScale: true }, points: { size: 6 } },
      scales: {
        x: { time: true },
        ...Object.fromEntries(scales.map((_, i) => [scaleKey(i), { auto: true, range: (_u, lo, hi) => displayRange([lo, hi]) }])),
      },
      axes: [
        {
          stroke: muted,
          grid: { stroke: grid, width: 1 },
          ticks: { stroke: grid, width: 1 },
          font: axisFont,
          space: (_u, _i, min, max) => xTickSpace(max - min, Math.floor(min / 86400) !== Math.floor(max / 86400)),
          values: (_u, vals) => timeTicks(vals),
        },
        ...scales.map((sc, i) => ({
          scale: scaleKey(i),
          side: i === 0 ? 3 : 1,
          stroke: scales.length > 1 ? cssVar(SERIES_COLORS[series.findIndex((s) => scaleOf(s) === sc)] ?? "--plot", muted) : muted,
          grid: { show: i === 0, stroke: grid, width: 1 },
          ticks: { stroke: grid, width: 1 },
          font: axisFont,
          // Wide enough for the longest tick label, so values are never clipped.
          size: (_u, vals) => (vals?.length ? Math.max(36, Math.ceil(Math.max(...vals.map((v) => v.length)) * AXIS_CHAR_PX) + 16) : 52),
          label: scales.length > 1 && sc ? sc : undefined,
          labelSize: scales.length > 1 && sc ? 14 : 0,
          labelFont: axisFont,
          values: (_u, vals) => vals.map(format),
        })),
      ],
      series: [
        { value: (_u, v) => (v == null ? "—" : `${fmtFull(v)} UTC`) },
        ...series.map((s, i) => ({
          label: s.label,
          scale: scaleKey(scales.indexOf(scaleOf(s))),
          stroke: cssVar(s.colorVar ?? SERIES_COLORS[i] ?? "--plot", "#0b6e7a"),
          width: 1.75,
          spanGaps,
          fill: series.length === 1 ? cssVar("--plot-fill", "rgba(11,110,122,.1)") : undefined,
          points: { show: false },
          value: (_u, v) => (v == null ? "no data" : format(v) + (s.unit ? ` ${s.unit}` : "")),
        })),
      ],
      hooks: {
        setScale: [
          (u, key) => {
            // A drag-zoom: remember it so live updates keep the zoom.
            if (key !== "x" || u.select.width < 4) return;
            const { min, max } = u.scales.x ?? {};
            if (min != null && max != null) {
              zoomRef.current = [min, max];
              setZoomed(true);
            }
          },
        ],
      },
    };
    const u = new uPlot(opts, toData(), el);
    plot.current = u;
    if (zoomRef.current) u.setScale("x", { min: zoomRef.current[0], max: zoomRef.current[1] });
    const ro = new ResizeObserver((entries) => {
      const w = widthOf(el, entries[0]?.contentRect.width);
      if (w !== u.width) u.setSize({ width: w, height });
    });
    ro.observe(el);
    return () => {
      ro.disconnect();
      u.destroy();
      plot.current = null;
    };
    // Re-create only when the series shape, height or theme changes; data flows through setData below.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [shape, height, resolved]);

  useEffect(() => {
    const u = plot.current;
    if (!u) return;
    const z = zoomRef.current;
    u.setData(toData(), !z);
    if (z) u.setScale("x", { min: z[0], max: z[1] });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [x, ys]);

  const extent = () => {
    const xs = plot.current?.data[0];
    return xs && xs.length > 1 ? [xs[0], xs[xs.length - 1]] : null;
  };
  const zoom = (factor) => {
    const u = plot.current;
    const ext = extent();
    if (!u || !ext) return;
    const { min, max } = u.scales.x ?? {};
    const cur = zoomRef.current ?? [min ?? ext[0], max ?? ext[1]];
    const [a, b] = zoomRange(cur[0], cur[1], factor, ext[0], ext[1]);
    const full = a <= ext[0] && b >= ext[1];
    zoomRef.current = full ? null : [a, b];
    setZoomed(!full);
    u.setScale("x", { min: a, max: b });
  };
  const reset = () => {
    const ext = extent();
    zoomRef.current = null;
    setZoomed(false);
    if (plot.current && ext) plot.current.setScale("x", { min: ext[0], max: ext[1] });
  };

  const empty = !ys.some((y) => y.some((v) => v !== null && v !== undefined));
  return (
    <div className="chart">
      <div ref={host} role="img" aria-label={ariaLabel} />
      {empty && <div className="nodata">No data</div>}
      {!empty && (
        <div className={`zoombar${zoomed ? " on" : ""}`} role="group" aria-label="Zoom">
          <button type="button" onClick={() => zoom(0.5)} aria-label="Zoom in" title="Zoom in">
            +
          </button>
          <button type="button" onClick={() => zoom(2)} aria-label="Zoom out" title="Zoom out">
            −
          </button>
          {zoomed && (
            <button type="button" onClick={reset} title="Show all data">
              Reset
            </button>
          )}
        </div>
      )}
    </div>
  );
}
