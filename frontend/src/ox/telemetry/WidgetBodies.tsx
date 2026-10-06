// The body of each widget type. The frame (header, settings, resize) is in Widget.tsx.
import { useMemo } from 'react';
import { Chart, type ChartSeries } from '../../components/Chart';
import { fmtNumber } from '../lib/format';
import { HEIGHT_PX, seriesIds } from '../lib/layout';
import { clientStat, difference, withGaps } from '../lib/series';
import { fmtUtcTime } from '../lib/time';
import {
  arcPath,
  bitsOf,
  byteCount,
  deltaInView,
  fmtAge,
  fmtDecimals,
  gaugeBands,
  gaugeFraction,
  gaugeScale,
  isStale,
  levelOf,
  sparkPath,
  stateSince,
  thresholdsOf,
  toggledBits,
} from '../lib/widgets';
import type { SeriesStat, WidgetMeta, WidgetProps } from './Widget';

const NUMERIC = new Set(['float', 'int', 'derived']);

/** Text for a value: fixed decimals for numbers when the widget sets them. */
function text(meta: WidgetMeta, v: number | null, dec: number | undefined): string {
  if (v === null || !NUMERIC.has(meta.kind)) return meta.format(v);
  return fmtDecimals(v, dec, (x) => meta.format(x));
}

/** Level class for a value: the widget's thresholds first, then the MIB's normal state values. */
function levelClass(p: WidgetProps, meta: WidgetMeta, v: number | null): string {
  const lv = (NUMERIC.has(meta.kind) ? levelOf(v, p.w) : null) ?? meta.level?.(v) ?? null;
  return lv ? `lv-${lv}` : '';
}

function NotInRun() {
  return <div className="val missing">Not in this run</div>;
}

/** Shown in place of a plot or bar whose series cannot share one axis (see sharedAxisProblem). */
export function CannotChart({ reason }: { reason: string }) {
  return (
    <div className="banner warn" role="alert">
      <b>This chart is not possible.</b> {reason}
    </div>
  );
}

export function ValueBody(p: WidgetProps) {
  const meta = p.metas[0]!;
  if (meta.missing) return <NotInRun />;
  const v = p.data.latest;
  const missing = v === null;
  return (
    <div
      className={`val ${missing ? 'missing' : ''} ${meta.state && !missing ? 'state' : ''} ${levelClass(p, meta, v)}`}
      aria-live="off"
    >
      {text(meta, v, p.w.dec)}
      {!missing && !meta.state && meta.unit && <span className="u">{meta.unit}</span>}
    </div>
  );
}

function StatLines({ p, diffStat }: { p: WidgetProps; diffStat: SeriesStat | null }) {
  const { metas, data, w } = p;
  const stats: { name: string; st: SeriesStat | null }[] = metas.map((m, i) => ({
    name: m.name,
    st: data.stats[i] ?? null,
  }));
  if (w.diff && metas.length === 2) stats.push({ name: 'A − B', st: diffStat });
  const multi = stats.length > 1;
  const f = (v: number | null) => (v === null ? '—' : fmtDecimals(v, w.dec, fmtNumber));
  return (
    <div className={`stats ${multi ? 'multi' : ''}`}>
      {stats.map(({ name, st }, i) => (
        <span key={name + i} className="statline">
          {multi && (
            <b
              className="sname"
              style={{ color: `var(${i < metas.length ? `--plot${i ? `-${i + 1}` : ''}` : '--ink-2'})` }}
            >
              {name}
            </b>
          )}
          {st && st.count > 0 ? (
            <>
              <span>
                min <b>{f(st.min)}</b>
              </span>
              <span>
                max <b>{f(st.max)}</b>
              </span>
              <span>
                avg <b>{f(st.avg)}</b>
              </span>
              {!multi && (
                <span>
                  n <b>{st.count}</b>
                </span>
              )}
            </>
          ) : (
            <span>no samples in range</span>
          )}
        </span>
      ))}
      {data.statSource === 'client' && (
        <span title="Computed in the browser from the samples in view">(in view)</span>
      )}
    </div>
  );
}

export function PlotBody(p: WidgetProps) {
  const { w, metas, data, markers } = p;
  const meta = metas[0]!;
  const showDiff = metas.length === 2 && !!w.diff;
  const sameUnit = showDiff && metas[0]?.unit === metas[1]?.unit;
  const plotted = useMemo(() => {
    const ys = showDiff ? [...data.ys, difference(data.ys[0] ?? [], data.ys[1] ?? [])] : data.ys;
    return withGaps(data.x, ys, p.periodMs);
  }, [showDiff, data.x, data.ys, p.periodMs]);
  const diffStat = useMemo(
    () => (showDiff ? clientStat(difference(data.ys[0] ?? [], data.ys[1] ?? [])) : null),
    [showDiff, data.ys],
  );
  const thresholds = useMemo(() => thresholdsOf(w), [w]);
  const series: ChartSeries[] = metas.map((m, i) => ({
    label: m.name,
    unit: m.unit,
    scale: m.unit || `s${i}`,
  }));
  if (showDiff)
    series.push({
      label: `${metas[0]!.name} − ${metas[1]!.name}`,
      unit: sameUnit ? meta.unit : '',
      scale: sameUnit ? meta.unit || 's0' : 'diff',
      dashed: true,
    });
  const v = data.latest;
  const multi = series.length > 1;
  return (
    <>
      {!multi &&
        (meta.missing ? (
          <NotInRun />
        ) : (
          <div
            className={`val ${v === null ? 'missing' : ''} ${meta.state && v !== null ? 'state' : ''} ${levelClass(p, meta, v)}`}
          >
            {text(meta, v, w.dec)}
            {v !== null && !meta.state && meta.unit && <span className="u">{meta.unit}</span>}
          </div>
        ))}
      <Chart
        x={plotted.x}
        ys={plotted.ys}
        series={series}
        bars={w.type === 'bar' && series.length === 1}
        markers={markers}
        thresholds={thresholds}
        height={HEIGHT_PX[w.h ?? 's'] - (w.type === 'bar' && !w.h ? 20 : 0)}
        ariaLabel={`${metas.map((m) => m.name).join(', ')}: ${data.x.length} samples in view${v === null ? ', latest value missing' : `, latest ${text(meta, v, w.dec)}`}`}
      />
      <StatLines p={p} diffStat={diffStat} />
    </>
  );
}

const G = { w: 160, h: 112, cx: 80, cy: 70, r: 56 };

export function GaugeBody(p: WidgetProps) {
  const { w, data } = p;
  const meta = p.metas[0]!;
  if (meta.missing) return <NotInRun />;
  const st = data.stats[0];
  const observed = st && st.count > 0 ? { min: st.min, max: st.max } : (clientStat(data.ys[0] ?? []) ?? null);
  const scale = gaugeScale(w, meta.table1 ?? null, observed);
  const v = data.latest;
  const frac = gaugeFraction(v, scale.min, scale.max);
  const bands = gaugeBands(w, scale.min, scale.max);
  const lv = levelOf(v, w);
  const valueText = text(meta, v, w.dec);
  const f = (x: number) => fmtDecimals(x, w.dec, fmtNumber);
  const source = {
    setting: 'scale set on this widget',
    table1: 'scale: TC Table 1 range',
    observed: 'scale: min/max in view',
    none: 'no scale yet',
  }[scale.source];
  const outside = v !== null && (v < scale.min || v > scale.max);
  return (
    <div className="gauge">
      <svg
        viewBox={`0 0 ${G.w} ${G.h}`}
        role="meter"
        aria-valuemin={scale.min}
        aria-valuemax={scale.max}
        aria-valuenow={v ?? undefined}
        aria-valuetext={`${valueText}${meta.unit && v !== null ? ` ${meta.unit}` : ''}${lv && lv !== 'ok' ? `, ${lv}` : ''}`}
        aria-label={meta.name}
      >
        <path className="g-track" d={arcPath(G.cx, G.cy, G.r, 0, 1)} />
        {bands.map((b, i) => (
          <path key={i} className={`g-band ${b.level}`} d={arcPath(G.cx, G.cy, G.r, b.from, b.to)} />
        ))}
        {frac !== null && frac > 0.001 && (
          <path className={`g-value ${lv ? `lv-${lv}` : ''}`} d={arcPath(G.cx, G.cy, G.r, 0, frac)} />
        )}
        <text
          className={`g-num ${v === null ? 'missing' : ''} ${lv ? `lv-${lv}` : ''}`}
          x={G.cx}
          y={G.cy + 2}
        >
          {valueText}
        </text>
        {v !== null && meta.unit && (
          <text className="g-unit" x={G.cx} y={G.cy + 18}>
            {meta.unit}
          </text>
        )}
        <text className="g-tick" x={G.cx - G.r * 0.87} y={G.h - 4}>
          {f(scale.min)}
        </text>
        <text className="g-tick" x={G.cx + G.r * 0.87} y={G.h - 4}>
          {f(scale.max)}
        </text>
      </svg>
      <span className="g-src small muted">
        {source}
        {outside ? ' · value outside the scale' : ''}
      </span>
    </div>
  );
}

export function StateBody(p: WidgetProps) {
  const { data, now } = p;
  const meta = p.metas[0]!;
  if (meta.missing) return <NotInRun />;
  const v = data.latest;
  const lv = meta.level?.(v) ?? null;
  const since = stateSince(data.x, data.ys[0] ?? []);
  const label = meta.format(v);
  return (
    <div className="statebody">
      <div className={`lamp ${v === null ? 'off' : lv ? `lv-${lv}` : 'lv-none'}`}>
        <i aria-hidden="true" />
        <span className={`val state ${v === null ? 'missing' : ''}`}>{label}</span>
      </div>
      {v !== null && (
        <div className="small muted">
          {lv === 'ok' ? 'Normal. ' : lv ? 'Not the normal state. ' : ''}
          {since &&
            (since.sinceStart
              ? `Unchanged for at least ${fmtAge(now - since.since)} (all of the view).`
              : `For ${fmtAge(now - since.since)}, since ${fmtUtcTime(since.since)} UTC; was ${meta.format(since.previous)}.`)}
        </div>
      )}
    </div>
  );
}

export function BitsBody(p: WidgetProps) {
  const { data } = p;
  const meta = p.metas[0]!;
  const v = data.latest;
  const bytes = byteCount(meta.hexDigits, v);
  const toggled = useMemo(() => toggledBits(data.ys[0] ?? [], bytes), [data.ys, bytes]);
  if (meta.missing) return <NotInRun />;
  const bits = bitsOf(v, bytes);
  const on = bits.filter((b) => b.on).map((b) => b.bit);
  const rows: (typeof bits)[] = [];
  for (let i = 0; i < bits.length; i += 8) rows.push(bits.slice(i, i + 8));
  return (
    <div className="bitsbody">
      <div className={`val ${v === null ? 'missing' : ''}`}>{meta.format(v)}</div>
      <div
        className="bitgrid"
        role="img"
        aria-label={
          v === null
            ? 'No data'
            : `Bits set: ${on.length ? on.join(', ') : 'none'}${toggled.size ? `. Changed in view: ${[...toggled].sort((a, b) => a - b).join(', ')}` : ''}`
        }
      >
        {rows.map((row, r) => (
          <div className="bitrow" key={r}>
            {row.map((b) => (
              <span
                key={b.bit}
                className={`bit ${b.on === null ? 'nd' : b.on ? 'on' : ''} ${toggled.has(b.bit) ? 'chg' : ''}`}
                title={`bit ${b.bit}: ${b.on === null ? 'no data' : b.on ? '1' : '0'}${toggled.has(b.bit) ? ' (changed in view)' : ''}`}
              >
                <i aria-hidden="true" />
                <small>{b.bit}</small>
              </span>
            ))}
          </div>
        ))}
      </div>
      {toggled.size > 0 && <span className="small muted">Ringed bits changed in the visible window.</span>}
    </div>
  );
}

const SPARK = { w: 200, h: 36 };

export function StatBody(p: WidgetProps) {
  const { w, data } = p;
  const meta = p.metas[0]!;
  const path = useMemo(() => sparkPath(data.x, data.ys[0] ?? [], SPARK.w, SPARK.h), [data.x, data.ys]);
  if (meta.missing) return <NotInRun />;
  const v = data.latest;
  const y = data.ys[0] ?? [];
  const delta = deltaInView(y);
  const st = data.stats[0];
  const f = (x: number | null) => (x === null ? '—' : fmtDecimals(x, w.dec, fmtNumber));
  return (
    <div className="statcard">
      <div className={`big ${v === null ? 'missing' : ''} ${levelClass(p, meta, v)}`}>
        {text(meta, v, w.dec)}
        {v !== null && meta.unit && <span className="u">{meta.unit}</span>}
      </div>
      <svg
        className="spark"
        viewBox={`0 0 ${SPARK.w} ${SPARK.h}`}
        preserveAspectRatio="none"
        role="img"
        aria-label={`${meta.name} trend over the view`}
      >
        {path ? (
          <path d={path} />
        ) : (
          <text x={SPARK.w / 2} y={SPARK.h / 2 + 4}>
            No data
          </text>
        )}
      </svg>
      <div className="stats">
        <span>
          Δ view{' '}
          <b className={delta === null ? '' : delta > 0 ? 'up' : delta < 0 ? 'down' : ''}>
            {delta === null ? '—' : `${delta > 0 ? '▲ +' : delta < 0 ? '▼ ' : ''}${f(delta)}`}
          </b>
        </span>
        <span>
          min <b>{f(st?.min ?? null)}</b>
        </span>
        <span>
          max <b>{f(st?.max ?? null)}</b>
        </span>
      </div>
    </div>
  );
}

export function TableBody(p: WidgetProps) {
  const { w, metas, data, now, live, periodMs } = p;
  const ids = seriesIds(w);
  return (
    <div className="tblwrap wtable">
      <table>
        <thead>
          <tr>
            <th>Parameter</th>
            <th>Value</th>
            <th>Age</th>
          </tr>
        </thead>
        <tbody>
          {ids.map((id, i) => {
            const m = metas[i]!;
            const v = data.latests[i] ?? null;
            const ts = data.lastTs[i] ?? null;
            const stale = isStale(ts, now, periodMs, live);
            return (
              <tr key={id} className={stale ? 'stale' : ''}>
                <td title={id}>
                  {m.name} <span className="mono muted">{id}</span>
                </td>
                <td className={`num ${v === null ? 'muted' : ''} ${levelClass(p, m, v)}`}>
                  {m.missing ? 'Not in this run' : text(m, v, w.dec)}
                  {v !== null && !m.state && m.unit ? ` ${m.unit}` : ''}
                </td>
                <td className="num muted">{ts === null ? '—' : fmtAge(now - ts)}</td>
              </tr>
            );
          })}
        </tbody>
      </table>
    </div>
  );
}
