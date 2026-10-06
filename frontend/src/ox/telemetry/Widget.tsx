import { useSortable } from '@dnd-kit/sortable';
import { CSS } from '@dnd-kit/utilities';
import { memo, useRef, useState } from 'react';
import type { Level, Stat } from '../types';
import type { Marker } from '../../components/Chart';
import type { Dtype } from '../lib/format';
import {
  HEIGHT_PX,
  WIDGET_TYPE_LABEL,
  defaultCols,
  isChartType,
  maxIds,
  typesFor,
  type Widget as W,
  type WidgetHeight,
} from '../lib/layout';
import { fmtAge, isStale } from '../lib/widgets';
import { BitsBody, CannotChart, GaugeBody, PlotBody, StateBody, StatBody, TableBody, ValueBody } from './WidgetBodies';
import { WidgetSettingsPanel } from './WidgetSettings';

export interface WidgetMeta {
  name: string;
  unit: string;
  /** Text for a value; returns "No data" for missing. */
  format: (v: number | null) => string;
  /** What the id is: a channel's dtype, a derived parameter, or missing from the run. */
  kind: Dtype | 'derived' | 'missing';
  /** Labelled state (enum): shown in the condensed state style. */
  state?: boolean;
  missing?: boolean; // the channel does not exist in this run
  hexDigits?: number | null;
  /** Normal/abnormal colour of a state value, from the MIB roles (tm_ok_values); null = no opinion. */
  level?: (v: number | null) => Level | null;
  /** Table 1 bounds when the channel echoes a TC field with a range. */
  table1?: { min: number; max: number } | null;
}

export type SeriesStat = Pick<Stat, 'min' | 'max' | 'avg' | 'count'>;

export interface WidgetData {
  latest: number | null;
  /** Shared x (ms) for every series. */
  x: number[];
  /** One y array per id in `seriesIds(w)` order (empty for value and table widgets). */
  ys: (number | null)[][];
  /** Per id; null when unknown. */
  stats: (SeriesStat | null)[];
  /** Server stats are for the visible range; client stats are computed here from the buffer. */
  statSource: 'server' | 'client' | 'none';
  /** Latest value per id. */
  latests: (number | null)[];
  /** Time of the latest sample that has a value, per id. */
  lastTs: (number | null)[];
}

export interface SeriesOption {
  id: string;
  label: string;
}

export interface WidgetProps {
  w: W;
  /** One per id, primary first. */
  metas: WidgetMeta[];
  data: WidgetData;
  markers: readonly Marker[];
  /** Channels and derived parameters that can be added as extra plot series (numeric only). */
  seriesOptions: readonly SeriesOption[];
  /** Every channel and derived parameter, for table rows. */
  rowOptions: readonly SeriesOption[];
  onUpdate: (uid: string, patch: Partial<W>) => void;
  onRemove: (uid: string) => void;
  chip?: React.ReactNode;
  now: number;
  /** The run is live: old values are shown as stale. */
  live: boolean;
  /** Telemetry period in ms (stale = older than 3 periods). */
  periodMs: number;
  /** Why this plot or bar cannot be formed from its series, or null when it can. */
  cannot?: string | null;
}

const HEIGHTS: WidgetHeight[] = ['s', 'm', 'l'];

function ResizeHandle({ w, name, onUpdate }: { w: W; name: string; onUpdate: WidgetProps['onUpdate'] }) {
  const start = useRef<{ x: number; y: number; width: number; colW: number; h: number } | null>(null);
  const tall = isChartType(w.type);
  const cols = w.cols ?? defaultCols(w.type);
  const h = w.h ?? 's';
  const set = (patch: Partial<W>) => {
    if ((patch.cols ?? cols) !== cols || (patch.h ?? h) !== h) onUpdate(w.uid, patch);
  };
  return (
    <button
      type="button"
      className="resize"
      aria-label={`Resize ${name}: ${cols} column${cols > 1 ? 's' : ''}${tall ? `, height ${h}` : ''}. Arrow keys change the size.`}
      title="Drag to resize (arrow keys work too)"
      onPointerDown={(e) => {
        const card = e.currentTarget.closest('.w') as HTMLElement | null;
        const grid = card?.parentElement;
        if (!card || !grid) return;
        const n = getComputedStyle(grid).gridTemplateColumns.split(' ').filter(Boolean).length || 1;
        start.current = {
          x: e.clientX,
          y: e.clientY,
          width: card.getBoundingClientRect().width,
          colW: grid.clientWidth / n,
          h: HEIGHT_PX[h],
        };
        e.currentTarget.setPointerCapture(e.pointerId);
        e.preventDefault();
        e.stopPropagation();
      }}
      onPointerMove={(e) => {
        const s = start.current;
        if (!s) return;
        const nextCols = Math.min(3, Math.max(1, Math.round((s.width + e.clientX - s.x) / s.colW)));
        const target = s.h + e.clientY - s.y;
        const nextH = HEIGHTS.reduce((best, k) =>
          Math.abs(HEIGHT_PX[k] - target) < Math.abs(HEIGHT_PX[best] - target) ? k : best,
        );
        set(tall ? { cols: nextCols, h: nextH } : { cols: nextCols });
      }}
      onPointerUp={() => (start.current = null)}
      onPointerCancel={() => (start.current = null)}
      onKeyDown={(e) => {
        const hi = HEIGHTS.indexOf(h);
        const k: Record<string, Partial<W>> = {
          ArrowRight: { cols: Math.min(3, cols + 1) },
          ArrowLeft: { cols: Math.max(1, cols - 1) },
          ...(tall
            ? { ArrowDown: { h: HEIGHTS[Math.min(2, hi + 1)] }, ArrowUp: { h: HEIGHTS[Math.max(0, hi - 1)] } }
            : {}),
        };
        const patch = k[e.key];
        if (patch) {
          e.preventDefault();
          e.stopPropagation();
          set(patch);
        }
      }}
    />
  );
}

/** Chips for the extra ids of a plot (series) or a table (rows), with an add menu. */
function IdChips({
  w,
  metas,
  options,
  onUpdate,
}: {
  w: W;
  metas: WidgetMeta[];
  options: readonly SeriesOption[];
  onUpdate: WidgetProps['onUpdate'];
}) {
  const more = (w.more ?? []).slice(0, maxIds(w.type) - 1);
  const table = w.type === 'table';
  const addId = `wa-${w.uid}`;
  const addable = options.filter((o) => o.id !== w.id && !more.includes(o.id));
  const canDiff = !table && more.length === 1;
  const sameUnit = canDiff && metas[0]?.unit === metas[1]?.unit;
  const name = metas[0]?.name ?? w.id;
  return (
    <div className="wseries">
      {more.map((id, i) => (
        <span key={id} className="chip neutral">
          {!table && (
            <i className="swatch" style={{ background: `var(--plot-${i + 2})` }} aria-hidden="true" />
          )}
          {metas[i + 1]?.name ?? id}
          <button
            type="button"
            className="chipx"
            aria-label={`Remove ${metas[i + 1]?.name ?? id} from this ${table ? 'table' : 'plot'}`}
            onClick={() => onUpdate(w.uid, { more: more.filter((m) => m !== id), diff: false })}
          >
            ×
          </button>
        </span>
      ))}
      {more.length < maxIds(w.type) - 1 && addable.length > 0 && (
        <>
          <label className="sr-only" htmlFor={addId}>
            Add a parameter to the {name} {table ? 'table' : 'plot'}
          </label>
          <select
            id={addId}
            className="addseries"
            value=""
            onChange={(e) => e.target.value && onUpdate(w.uid, { more: [...more, e.target.value] })}
          >
            <option value="">{table ? '+ row' : '+ parameter'}</option>
            {addable.map((o) => (
              <option key={o.id} value={o.id}>
                {o.label}
              </option>
            ))}
          </select>
        </>
      )}
      {canDiff && (
        <label
          className="row small"
          title={sameUnit ? '' : 'The units differ; the difference gets its own axis.'}
        >
          <input
            type="checkbox"
            checked={!!w.diff}
            onChange={(e) => onUpdate(w.uid, { diff: e.target.checked })}
          />
          Compare (A − B)
        </label>
      )}
    </div>
  );
}

function WidgetView(props: WidgetProps) {
  const { w, metas, data, onUpdate, onRemove, chip, now, live, periodMs } = props;
  const {
    attributes,
    listeners,
    setNodeRef,
    setActivatorNodeRef,
    transform,
    transition,
    isDragging,
    isOver,
  } = useSortable({ id: w.uid });
  const { onKeyDown, ...pointerListeners } = listeners ?? {};
  const keyDown = onKeyDown as React.KeyboardEventHandler<HTMLButtonElement> | undefined;
  const [settingsOpen, setSettingsOpen] = useState(false);
  const meta = metas[0]!;
  const names = metas.map((m) => m.name).join(' · ');
  const title = w.title || names;
  const cols = w.cols ?? defaultCols(w.type);
  const style = { transform: CSS.Translate.toString(transform), transition };
  const typeId = `wt-${w.uid}`;
  const setId = `ws-${w.uid}`;
  const allowed = typesFor(meta.kind);
  const types = allowed.includes(w.type) ? allowed : [w.type, ...allowed];
  const lastTs = data.lastTs[0] ?? null;
  const stale = w.type !== 'table' && !meta.missing && isStale(lastTs, now, periodMs, live);
  const age = lastTs === null ? null : now - lastTs;

  let body: React.ReactNode;
  switch (w.type) {
    case 'plot':
    case 'bar':
      body = props.cannot ? <CannotChart reason={props.cannot} /> : <PlotBody {...props} />;
      break;
    case 'gauge':
      body = <GaugeBody {...props} />;
      break;
    case 'state':
      body = <StateBody {...props} />;
      break;
    case 'bits':
      body = <BitsBody {...props} />;
      break;
    case 'stat':
      body = <StatBody {...props} />;
      break;
    case 'table':
      body = <TableBody {...props} />;
      break;
    default:
      body = <ValueBody {...props} />;
  }

  return (
    <div
      ref={setNodeRef}
      style={style}
      data-cols={cols}
      className={`w ${w.type} ${stale ? 'stale' : ''} ${isDragging ? 'dragging' : ''} ${isOver && !isDragging ? 'over' : ''}`}
    >
      {/* The whole title bar drags with the mouse (after 5 px, so its buttons still click); the keyboard drags from
          the grip only, so Space inside the type menu never lifts the widget. */}
      <div className="wh" {...pointerListeners}>
        <button
          ref={setActivatorNodeRef}
          type="button"
          className="grip"
          aria-label={`Move ${title}`}
          {...attributes}
          onKeyDown={keyDown}
        >
          <svg viewBox="0 0 16 16" aria-hidden="true" className="gripdots">
            {[4, 8, 12].flatMap((y) => [6, 10].map((x) => <circle key={`${x}${y}`} cx={x} cy={y} r="1.2" />))}
          </svg>
        </button>
        <div className="wt">
          <span className="nm" title={w.title ? `${w.title} (${names})` : names}>
            {title}
          </span>
          <span className="sub">
            <span className="id">{[w.id, ...(w.more ?? []).slice(0, maxIds(w.type) - 1)].join(' ')}</span>
            {stale && age !== null && (
              <span
                className="age"
                title={`Last value ${fmtAge(age)} ago; telemetry arrives every ${fmtAge(periodMs)}.`}
              >
                {fmtAge(age)} old
              </span>
            )}
          </span>
        </div>
        <div className="wctl">
          <span className="wtype" title={`Display as: ${WIDGET_TYPE_LABEL[w.type]}`}>
            <svg viewBox="0 0 16 16" aria-hidden="true">
              <path d="M2 13h12M4 11V7M8 11V3M12 11V8" />
            </svg>
            <label className="sr-only" htmlFor={typeId}>
              Display {title} as
            </label>
            <select
              id={typeId}
              value={w.type}
              onChange={(e) => onUpdate(w.uid, { type: e.target.value as W['type'] })}
            >
              {types.map((t) => (
                <option key={t} value={t}>
                  {WIDGET_TYPE_LABEL[t]}
                </option>
              ))}
            </select>
          </span>
          <button
            type="button"
            className={`wbtn ${settingsOpen ? 'on' : ''}`}
            aria-label={`Settings for ${title}`}
            aria-expanded={settingsOpen}
            aria-controls={setId}
            title="Title, decimals, thresholds"
            onClick={() => setSettingsOpen((o) => !o)}
          >
            <svg viewBox="0 0 16 16" aria-hidden="true">
              <path d="M3 4.5h10M3 11.5h10" />
              <circle cx="6" cy="4.5" r="1.6" />
              <circle cx="10" cy="11.5" r="1.6" />
            </svg>
          </button>
          <button
            type="button"
            className="wbtn x"
            aria-label={`Remove ${title} widget`}
            title="Remove widget"
            onClick={() => onRemove(w.uid)}
          >
            <svg viewBox="0 0 16 16" aria-hidden="true">
              <path d="M4 4l8 8M12 4l-8 8" />
            </svg>
          </button>
        </div>
      </div>
      {settingsOpen && (
        <WidgetSettingsPanel
          id={setId}
          w={w}
          name={title}
          onUpdate={onUpdate}
          onClose={() => setSettingsOpen(false)}
        />
      )}
      {(isChartType(w.type) || w.type === 'table') && (
        <IdChips
          w={w}
          metas={metas}
          options={w.type === 'table' ? props.rowOptions : props.seriesOptions}
          onUpdate={onUpdate}
        />
      )}
      {body}
      {chip}
      <ResizeHandle w={w} name={title} onUpdate={onUpdate} />
    </div>
  );
}

export const Widget = memo(WidgetView);
