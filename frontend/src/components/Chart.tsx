// Adapted from propulsion/orblinx_platform/web/src/components/Chart.tsx (uPlot wrapper).
import { useEffect, useRef, useState } from 'react';
import uPlot from 'uplot';
import 'uplot/dist/uPlot.min.css';
import { useResolvedTheme } from '../theme';
import {
  AXIS_CHAR_PX,
  SERIES_COLORS,
  fmtFull,
  layoutMarkerLabels,
  timeTicks,
  xTickSpace,
  zoomRange,
  displayRange,
} from '../lib/chart';

const utc = (sec: number) => uPlot.tzDate(new Date(sec * 1000), 'Etc/UTC');

export interface ChartSeries {
  label: string;
  unit?: string;
  /** CSS variable name for the stroke, e.g. "--plot". */
  colorVar?: string;
  /** Series with the same scale share a y axis; defaults to the unit. The first scale is on the left. */
  scale?: string;
  dashed?: boolean;
}

export interface Marker {
  ts: number; // ms
  label: string;
  /** Drawn on the chart instead of `label` when set (e.g. "▸ 18:12" for a run start). */
  short?: string;
}

/** Plot width for a host element: its content box, whole pixels, never below a usable minimum. */
const widthOf = (el: HTMLElement, w = el.getBoundingClientRect().width) => Math.max(120, Math.floor(w));

interface Props {
  /** x in ms since epoch (or elapsed seconds when `elapsed`); one y array per series; null = gap. */
  x: readonly number[];
  ys: readonly (readonly (number | null)[])[];
  series: readonly ChartSeries[];
  height?: number;
  bars?: boolean;
  markers?: readonly Marker[];
  /** X is elapsed seconds since run start instead of wall-clock ms. */
  elapsed?: boolean;
  ariaLabel: string;
  format?: (v: number) => string;
  /**
   * Drag across the plot, or use the zoom buttons, to pick a new range; called with x values (ms, or seconds
   * when `elapsed`). Without it the chart zooms locally and keeps the zoom while new data arrives.
   */
  onSelectRange?: (from: number, to: number) => void;
  /** Hide the zoom buttons (tiny charts). */
  noZoom?: boolean;
  /** Horizontal warn/crit lines on the first (left) axis; the axis range stretches to show them. */
  thresholds?: readonly { value: number; level: 'warn' | 'crit' }[];
  /** Join a line across nulls. Needed when series sampled at different rates share one x axis. */
  spanGaps?: boolean;
}

function cssVar(name: string, fallback: string): string {
  try {
    const v = getComputedStyle(document.documentElement).getPropertyValue(name).trim();
    return v || fallback;
  } catch {
    return fallback;
  }
}

const fmtElapsed = (s: number) => {
  const neg = s < 0;
  const a = Math.abs(Math.round(s));
  const h = Math.floor(a / 3600);
  const m = Math.floor((a % 3600) / 60);
  const x = a % 60;
  return (
    (neg ? '-' : '') + (h ? `${h}:${String(m).padStart(2, '0')}` : `${m}`) + ':' + String(x).padStart(2, '0')
  );
};

/**
 * uPlot wrapper. The instance is created once per (series shape, height, theme) and fed with setData on
 * updates, which keeps 1 Hz updates across many charts cheap. Missing values are nulls and render as gaps.
 */
export function Chart({
  x,
  ys,
  series,
  height = 130,
  bars,
  markers,
  elapsed,
  ariaLabel,
  format,
  onSelectRange,
  noZoom,
  thresholds,
  spanGaps = false,
}: Props) {
  const host = useRef<HTMLDivElement>(null);
  const plot = useRef<uPlot | null>(null);
  const markerRef = useRef(markers);
  markerRef.current = markers;
  const selectRef = useRef(onSelectRange);
  selectRef.current = onSelectRange;
  // Local zoom: the x range the user chose, kept across data updates until reset.
  const zoomRef = useRef<[number, number] | null>(null);
  const [zoomed, setZoomed] = useState(false);
  const resolved = useResolvedTheme();
  const scaleOf = (s: ChartSeries) => s.scale ?? s.unit ?? '';
  const scales = [...new Set(series.map(scaleOf))];
  const thKey = (thresholds ?? []).map((t) => `${t.level}${t.value}`).join(',');
  const shape =
    series.map((s) => s.label + (s.colorVar ?? '') + '@' + scaleOf(s)).join('|') +
    (bars ? 'b' : '') +
    (elapsed ? 'e' : '') +
    (spanGaps ? 'g' : '') +
    (thKey ? `|th:${thKey}` : '');

  const toData = (): uPlot.AlignedData => [
    Float64Array.from(x, (v) => (elapsed ? v : v / 1000)),
    ...ys.map((y) => y as (number | null)[]),
  ];

  useEffect(() => {
    const el = host.current;
    if (!el) return;
    const grid = cssVar('--grid', '#e3e8eb');
    const muted = cssVar('--muted', '#6f7e89');
    const event = cssVar('--event', '#9a6210');
    const fmt = format ?? ((v: number) => (Math.abs(v) >= 1000 ? v.toFixed(0) : String(+v.toFixed(3))));
    const scaleKey = (i: number) => (i === 0 ? 'y' : `y${i}`);
    const ths = thresholds ?? [];
    const thColor = { warn: cssVar('--warn', '#9a6210'), crit: cssVar('--crit', '#b3372a') };
    // The left axis includes the threshold values, so a line outside the data is still visible.
    // Every y scale: data (and, on the first scale, threshold lines) widened to a readable minimum span.
    const yRange =
      (withThresholds: boolean): uPlot.Range.Function =>
      (_u, dmin, dmax) =>
        displayRange([dmin, dmax, ...(withThresholds ? ths.map((t) => t.value) : [])]);
    const axisFont = '10px "IBM Plex Mono", monospace';
    const surface = cssVar('--surface', '#ffffff');
    const opts: uPlot.Options = {
      width: widthOf(el),
      height,
      tzDate: (ts) => utc(ts),
      // Names only (no live values): the widget's coloured stat lines carry the numbers.
      legend: { show: series.length > 1, live: false },
      cursor: { drag: { x: true, y: false, setScale: !onSelectRange }, points: { size: 6 } },
      scales: {
        x: { time: !elapsed },
        ...Object.fromEntries(
          scales.map((_, i) => [scaleKey(i), { auto: true, range: yRange(i === 0 && ths.length > 0) }]),
        ),
      },
      axes: [
        {
          stroke: muted,
          grid: { stroke: grid, width: 1 },
          ticks: { stroke: grid, width: 1 },
          font: axisFont,
          // Fewer ticks on narrow charts: never closer than the widest label of the visible span.
          space: (_u: uPlot, _i: number, min: number, max: number) =>
            xTickSpace(max - min, !!elapsed, !elapsed && Math.floor(min / 86400) !== Math.floor(max / 86400)),
          values: elapsed
            ? (_u: uPlot, vals: number[]) => vals.map(fmtElapsed)
            : (_u: uPlot, vals: number[]) => timeTicks(vals),
        },
        ...scales.map((sc, i) => {
          const unit = series.find((s) => scaleOf(s) === sc)?.unit;
          return {
            scale: scaleKey(i),
            side: i === 0 ? 3 : 1,
            stroke:
              scales.length > 1
                ? cssVar(SERIES_COLORS[series.findIndex((s) => scaleOf(s) === sc)] ?? '--plot', muted)
                : muted,
            grid: { show: i === 0, stroke: grid, width: 1 },
            ticks: { stroke: grid, width: 1 },
            font: axisFont,
            // Wide enough for the longest tick label, so values are never clipped.
            size: (_u: uPlot, vals: string[] | null) =>
              vals?.length
                ? Math.max(36, Math.ceil(Math.max(...vals.map((v) => v.length)) * AXIS_CHAR_PX) + 16)
                : 52,
            label: scales.length > 1 && unit ? unit : undefined,
            labelSize: scales.length > 1 && unit ? 14 : 0,
            labelFont: axisFont,
            values: (_u: uPlot, vals: number[]) => vals.map(fmt),
          } satisfies uPlot.Axis;
        }),
      ],
      series: [
        {
          value: (_u, v) => (v == null ? '—' : elapsed ? `T+${fmtElapsed(v)}` : fmtFull(v) + ' UTC'),
        },
        ...series.map((s, i) => {
          const stroke = cssVar(s.colorVar ?? SERIES_COLORS[i] ?? '--plot', '#0b6e7a');
          return {
            label: s.label,
            scale: scaleKey(scales.indexOf(scaleOf(s))),
            stroke,
            width: 1.75,
            dash: s.dashed ? [5, 4] : undefined,
            spanGaps,
            fill: series.length === 1 && !bars ? cssVar('--plot-fill', 'rgba(11,110,122,.1)') : undefined,
            points: { show: false },
            value: (_u: uPlot, v: number | null) =>
              v == null ? 'no data' : fmt(v) + (s.unit ? ' ' + s.unit : ''),
            ...(bars
              ? { paths: uPlot.paths.bars!({ size: [0.7, 24] }), fill: stroke, points: { show: false } }
              : {}),
          } satisfies uPlot.Series;
        }),
      ],
      hooks: {
        setSelect: [
          (u) => {
            const cb = selectRef.current;
            if (!cb || u.select.width < 4) return;
            const a = u.posToVal(u.select.left, 'x');
            const b = u.posToVal(u.select.left + u.select.width, 'x');
            u.setSelect({ left: 0, top: 0, width: 0, height: 0 }, false);
            cb(elapsed ? a : a * 1000, elapsed ? b : b * 1000);
          },
        ],
        setScale: [
          (u, key) => {
            // A drag-zoom in local mode: remember it so live updates keep the zoom.
            if (key !== 'x' || selectRef.current || u.select.width < 4) return;
            const { min, max } = u.scales.x ?? {};
            if (min != null && max != null) {
              zoomRef.current = [min, max];
              setZoomed(true);
            }
          },
        ],
        draw: [
          (u) => {
            if (!ths.length) return;
            const ctx = u.ctx;
            const { left, width } = u.bbox;
            ctx.save();
            ctx.lineWidth = 1.25 * devicePixelRatio;
            ctx.setLineDash([6 * devicePixelRatio, 4 * devicePixelRatio]);
            for (const t of ths) {
              const py = u.valToPos(t.value, 'y', true);
              if (!Number.isFinite(py) || py < u.bbox.top || py > u.bbox.top + u.bbox.height) continue;
              ctx.strokeStyle = thColor[t.level];
              ctx.beginPath();
              ctx.moveTo(left, py);
              ctx.lineTo(left + width, py);
              ctx.stroke();
            }
            ctx.restore();
          },
          (u) => {
            const ms = markerRef.current;
            if (!ms?.length) return;
            const ctx = u.ctx;
            const dpr = devicePixelRatio;
            const { left, top, width, height: h } = u.bbox;
            const lines = ms
              .map((m) => ({ m, px: u.valToPos(elapsed ? m.ts : m.ts / 1000, 'x', true) }))
              .filter(({ px }) => Number.isFinite(px) && px >= left && px <= left + width)
              .sort((a, b) => a.px - b.px);
            if (!lines.length) return;
            ctx.save();
            ctx.strokeStyle = event;
            ctx.setLineDash([3 * dpr, 3 * dpr]);
            ctx.lineWidth = dpr;
            for (const { px } of lines) {
              ctx.beginPath();
              ctx.moveTo(px, top);
              ctx.lineTo(px, top + h);
              ctx.stroke();
            }
            // Labels: measured, flipped or clamped to stay inside the plot, skipped when they would overlap.
            ctx.font = `${10 * dpr}px "IBM Plex Mono", monospace`;
            ctx.textBaseline = 'top';
            const texts = lines.map(({ m }) => m.short ?? m.label);
            const xs = layoutMarkerLabels(
              lines.map(({ px }, i) => ({ px, width: ctx.measureText(texts[i]!).width })),
              left,
              left + width,
              3 * dpr,
              6 * dpr,
            );
            xs.forEach((lx, i) => {
              if (lx === null) return;
              const w = ctx.measureText(texts[i]!).width;
              ctx.fillStyle = surface;
              ctx.globalAlpha = 0.85;
              ctx.fillRect(lx - dpr, top + 2 * dpr, w + 2 * dpr, 12 * dpr);
              ctx.globalAlpha = 1;
              ctx.fillStyle = event;
              ctx.fillText(texts[i]!, lx, top + 3 * dpr);
            });
            ctx.restore();
          },
        ],
      },
    };
    const u = new uPlot(opts, toData(), el);
    plot.current = u;
    if (zoomRef.current) u.setScale('x', { min: zoomRef.current[0], max: zoomRef.current[1] });
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
    if (z) u.setScale('x', { min: z[0], max: z[1] });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [x, ys, markers]);

  /** Data extent in plot units (s, or elapsed s). */
  const extent = (): [number, number] | null => {
    const xs = plot.current?.data[0];
    return xs && xs.length > 1 ? [xs[0]!, xs[xs.length - 1]!] : null;
  };
  const zoom = (factor: number) => {
    const u = plot.current;
    const ext = extent();
    if (!u || !ext) return;
    const { min, max } = u.scales.x ?? {};
    const cur: [number, number] = zoomRef.current ?? [min ?? ext[0], max ?? ext[1]];
    const cb = selectRef.current;
    if (cb) {
      // Range mode: the owner re-queries; zooming out is not limited to the loaded data.
      const [a, b] = zoomRange(cur[0], cur[1], factor, -Infinity, Infinity);
      cb(elapsed ? a : a * 1000, elapsed ? b : b * 1000);
      return;
    }
    const [a, b] = zoomRange(cur[0], cur[1], factor, ext[0], ext[1]);
    const full = a <= ext[0] && b >= ext[1];
    zoomRef.current = full ? null : [a, b];
    setZoomed(!full);
    u.setScale('x', { min: a, max: b });
  };
  const reset = () => {
    const u = plot.current;
    const ext = extent();
    zoomRef.current = null;
    setZoomed(false);
    if (u && ext) u.setScale('x', { min: ext[0], max: ext[1] });
  };

  const empty = !ys.some((y) => y.some((v) => v !== null));
  return (
    <div className="chart">
      <div ref={host} role="img" aria-label={ariaLabel} />
      {empty && <div className="nodata">No data</div>}
      {!noZoom && !empty && (
        <div className={`zoombar${zoomed ? ' on' : ''}`} role="group" aria-label="Zoom">
          <button
            type="button"
            className="btn sm"
            onClick={() => zoom(0.5)}
            aria-label="Zoom in"
            title="Zoom in"
          >
            +
          </button>
          <button
            type="button"
            className="btn sm"
            onClick={() => zoom(2)}
            aria-label="Zoom out"
            title="Zoom out"
          >
            −
          </button>
          {zoomed && !onSelectRange && (
            <button type="button" className="btn sm" onClick={reset} title="Show all data">
              Reset
            </button>
          )}
        </div>
      )}
    </div>
  );
}
