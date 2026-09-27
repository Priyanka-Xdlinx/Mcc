import { Icon } from '../../components/Icon';
import { useState, type FormEvent } from 'react';
import type { Widget, WidgetSettings } from '../lib/layout';
import { thresholdError } from '../lib/widgets';

const NUM_FIELDS = [
  ['warnLo', 'Warn below'],
  ['warnHi', 'Warn above'],
  ['critLo', 'Critical below'],
  ['critHi', 'Critical above'],
] as const;
const GAUGE_FIELDS = [
  ['min', 'Gauge min'],
  ['max', 'Gauge max'],
] as const;
type NumKey = (typeof NUM_FIELDS)[number][0] | (typeof GAUGE_FIELDS)[number][0];

const toText = (v: number | undefined) => (v === undefined ? '' : String(v));

/** Per-widget settings: custom title, decimals, warn/crit thresholds and (for gauges) the scale. */
export function WidgetSettingsPanel({
  id,
  w,
  name,
  onUpdate,
  onClose,
}: {
  id: string;
  w: Widget;
  name: string;
  onUpdate: (uid: string, patch: Partial<Widget>) => void;
  onClose: () => void;
}) {
  const [title, setTitle] = useState(w.title ?? '');
  const [dec, setDec] = useState(w.dec === undefined ? '' : String(w.dec));
  const [nums, setNums] = useState<Record<NumKey, string>>({
    warnLo: toText(w.warnLo),
    warnHi: toText(w.warnHi),
    critLo: toText(w.critLo),
    critHi: toText(w.critHi),
    min: toText(w.min),
    max: toText(w.max),
  });
  const fields = w.type === 'gauge' ? [...NUM_FIELDS, ...GAUGE_FIELDS] : NUM_FIELDS;
  const parsed: WidgetSettings = {};
  const bad: string[] = [];
  for (const [k, label] of fields) {
    const t = nums[k].trim();
    if (!t) continue;
    const n = Number(t);
    if (Number.isFinite(n)) parsed[k] = n;
    else bad.push(label);
  }
  const err = bad.length ? `${bad.join(', ')}: enter a number or leave blank.` : thresholdError(parsed);
  const blocking = bad.length > 0 || /must be/.test(err);

  const apply = (e: FormEvent) => {
    e.preventDefault();
    if (blocking) return;
    const patch: Partial<Widget> = {
      title: title.trim() || undefined,
      dec: dec === '' ? undefined : Number(dec),
      warnLo: parsed.warnLo,
      warnHi: parsed.warnHi,
      critLo: parsed.critLo,
      critHi: parsed.critHi,
    };
    if (w.type === 'gauge') Object.assign(patch, { min: parsed.min, max: parsed.max });
    onUpdate(w.uid, patch);
    onClose();
  };
  const reset = () => {
    onUpdate(w.uid, {
      title: undefined,
      dec: undefined,
      warnLo: undefined,
      warnHi: undefined,
      critLo: undefined,
      critHi: undefined,
      min: undefined,
      max: undefined,
    });
    onClose();
  };

  return (
    <form
      id={id}
      className="wset"
      onSubmit={apply}
      onKeyDown={(e) => {
        if (e.key === 'Escape') {
          e.stopPropagation();
          onClose();
        }
      }}
      aria-label={`Settings for ${name}`}
      noValidate
    >
      <div className="fld wide">
        <label htmlFor={`${id}-t`}>Title</label>
        <input
          id={`${id}-t`}
          value={title}
          maxLength={80}
          placeholder={name}
          onChange={(e) => setTitle(e.target.value)}
        />
      </div>
      <div className="fld">
        <label htmlFor={`${id}-d`}>Decimals</label>
        <select id={`${id}-d`} value={dec} onChange={(e) => setDec(e.target.value)}>
          <option value="">auto</option>
          {[0, 1, 2, 3, 4, 5, 6].map((n) => (
            <option key={n} value={n}>
              {n}
            </option>
          ))}
        </select>
      </div>
      {fields.map(([k, label]) => (
        <div className="fld" key={k}>
          <label htmlFor={`${id}-${k}`}>{label}</label>
          <input
            id={`${id}-${k}`}
            className="mono"
            inputMode="decimal"
            size={7}
            value={nums[k]}
            aria-invalid={bad.includes(label)}
            onChange={(e) => setNums({ ...nums, [k]: e.target.value })}
          />
        </div>
      ))}
      <div className="row wide">
        <button type="submit" className="btn sm primary" disabled={blocking}>
          <Icon name="check" size={14} />
          Apply
        </button>
        <button type="button" className="btn sm" onClick={onClose}>
          <Icon name="x" size={14} />
          Cancel
        </button>
        <button type="button" className="btn sm ghost" onClick={reset}>
          <Icon name="reset" size={14} />
          Clear settings
        </button>
        <span className={blocking ? 'err small' : 'muted small'} role="status">
          {err || 'Thresholds colour the value and draw lines on plots.'}
        </span>
      </div>
    </form>
  );
}
