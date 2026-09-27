// The telemetry dashboard: sections of widgets. The default comes from the MIB (roles.sections); what a
// user arranges is saved per user AND per MIB sha256, so a new MIB starts from its own default while the
// layout made for the old one is kept. Serialisation is compact and versioned: v1/v2 (fixed sections,
// before the MIB registry) still load; v3 carries the section list and the per-widget settings.
import { z } from 'zod';
import type { RoleSection } from '../types';
import { DERIVED_ID, type DerivedDef } from './derived';
import type { Dtype } from './format';
import { readJson, readString, writeString } from './storage';

export type WidgetType = 'value' | 'plot' | 'bar' | 'gauge' | 'state' | 'bits' | 'stat' | 'table';
export const WIDGET_TYPES: readonly WidgetType[] = [
  'value',
  'plot',
  'bar',
  'gauge',
  'state',
  'bits',
  'stat',
  'table',
];
export const WIDGET_TYPE_LABEL: Record<WidgetType, string> = {
  value: 'value',
  plot: 'plot',
  bar: 'bar',
  gauge: 'gauge',
  state: 'state lamp',
  bits: 'bits',
  stat: 'stat card',
  table: 'table',
};

export type WidgetHeight = 's' | 'm' | 'l';
/** Chart height in px per widget height. */
export const HEIGHT_PX: Record<WidgetHeight, number> = { s: 130, m: 220, l: 340 };
/** Series in one plot, and channels in one table widget. */
export const MAX_SERIES = 3;
export const MAX_TABLE = 8;

/** Types drawn with a time chart (they have a height and extra series). */
export const isChartType = (t: WidgetType) => t === 'plot' || t === 'bar';
/** How many ids a widget of this type shows (primary included). */
export const maxIds = (t: WidgetType) => (t === 'table' ? MAX_TABLE : isChartType(t) ? MAX_SERIES : 1);

export interface WidgetSettings {
  /** Custom title (replaces the channel names in the header). */
  title?: string;
  /** Fixed decimals for numeric values (0–6); unset = by magnitude. */
  dec?: number;
  warnLo?: number;
  warnHi?: number;
  critLo?: number;
  critHi?: number;
  /** Gauge scale override. */
  min?: number;
  max?: number;
}

export interface Widget extends WidgetSettings {
  uid: string;
  id: string; // channel key or derived id ("D1"); channels win when both exist
  type: WidgetType;
  /** More channels/derived ids: extra plot series (up to 2) or table rows (up to 7). */
  more?: string[];
  /** With exactly two series: also draw A − B. */
  diff?: boolean;
  /** Grid columns spanned (1–3); unset = 2 for plots and tables, 1 otherwise. */
  cols?: number;
  /** Chart height; unset = small. */
  h?: WidgetHeight;
}

/** All ids a widget shows, primary first, limited by its type. */
export const seriesIds = (w: Widget): string[] => [w.id, ...(w.more ?? [])].slice(0, maxIds(w.type));

export const defaultCols = (t: WidgetType) => (isChartType(t) || t === 'table' ? 2 : 1);

export interface SectionMeta {
  key: string;
  title: string;
}
export type Layout = Record<string, Widget[]>;
export interface Dashboard {
  sections: SectionMeta[];
  widgets: Layout;
}

export interface ChannelRef {
  key: string;
  group?: string | null;
  hidden?: boolean;
  dtype?: Dtype;
  unit?: string | null;
}

// ---------- default widget types

/** Widget types that make sense for a channel kind. */
export function typesFor(kind: Dtype | 'derived' | 'missing'): WidgetType[] {
  switch (kind) {
    case 'float':
    case 'int':
    case 'derived':
      return ['value', 'plot', 'bar', 'gauge', 'stat', 'table'];
    case 'enum':
    case 'bool':
      return ['state', 'value', 'plot', 'table'];
    case 'hex':
      return ['bits', 'value', 'table'];
    case 'version':
      return ['value', 'table'];
    default:
      return [...WIDGET_TYPES];
  }
}

/** Widget type when a channel is added: enum → state, hex/bitmap → bits, numbers → plot. */
export function defaultTypeFor(kind: Dtype | 'derived' | undefined): WidgetType {
  if (kind === 'enum' || kind === 'bool') return 'state';
  if (kind === 'hex') return 'bits';
  if (kind === 'version') return 'value';
  return 'plot';
}

/** A role's "value" widget for an enum becomes a state lamp and for a bitmap a bit display. */
function presentType(type: string, c: ChannelRef | undefined): WidgetType {
  const t = (WIDGET_TYPES as readonly string[]).includes(type) ? (type as WidgetType) : 'value';
  if (t !== 'value' || !c) return t;
  if (c.dtype === 'enum') return 'state';
  if (c.dtype === 'hex' && (c.unit ?? '').toLowerCase() === 'bitmap') return 'bits';
  return t;
}

const FALLBACK_PER_GROUP = 12;

/**
 * The default dashboard for a run: the MIB's sections (roles.sections), keeping widgets whose channel is in
 * the run or whose derived parameter exists. Without roles (no MIB loaded), one section per channel group.
 */
export function defaultDashboard(
  sections: readonly RoleSection[],
  channels: readonly ChannelRef[],
  derived: readonly DerivedDef[] = [],
): Dashboard {
  const byKey = new Map(channels.map((c) => [c.key, c]));
  const derivedIds = new Set(derived.map((d) => d.id));
  const exists = (id: string) => byKey.has(id) || (DERIVED_ID.test(id) && derivedIds.has(id));
  const out: Dashboard = { sections: [], widgets: {} };
  const add = (key: string, title: string, items: [string, WidgetType][]) => {
    if (!key || key in out.widgets) return;
    out.sections.push({ key, title: title || key });
    out.widgets[key] = items.map(([id, type], i) => ({ uid: `${key}${i}`, id, type }));
  };
  if (sections.length) {
    for (const s of sections)
      add(
        s.key,
        s.title,
        s.widgets.filter(([id]) => exists(id)).map(([id, type]) => [id, presentType(type, byKey.get(id))]),
      );
    return out;
  }
  const groups = [...new Set(channels.filter((c) => !c.hidden).map((c) => c.group || 'Channels'))];
  for (const g of groups)
    add(
      g.toLowerCase().replace(/[^a-z0-9]+/g, '') || 'channels',
      g,
      channels
        .filter((c) => !c.hidden && (c.group || 'Channels') === g)
        .slice(0, FALLBACK_PER_GROUP)
        .map((c) => [c.key, isChartType(defaultTypeFor(c.dtype)) ? 'value' : defaultTypeFor(c.dtype)]),
    );
  return out;
}

// ---------- canonical form

const IdSchema = z.string().min(1).max(32);
const num = z.number().finite().optional().catch(undefined);
const WidgetSchema = z.object({
  uid: z.string().min(1).max(64),
  id: IdSchema,
  type: z.enum(WIDGET_TYPES as [WidgetType, ...WidgetType[]]),
  more: z.array(IdSchema).max(16).optional().catch(undefined),
  diff: z.boolean().optional().catch(undefined),
  cols: z.number().int().min(1).max(3).optional().catch(undefined),
  h: z.enum(['s', 'm', 'l']).optional().catch(undefined),
  title: z.string().max(80).optional().catch(undefined),
  dec: z.number().int().min(0).max(6).optional().catch(undefined),
  warnLo: num,
  warnHi: num,
  critLo: num,
  critHi: num,
  min: num,
  max: num,
});

const SETTING_KEYS = ['warnLo', 'warnHi', 'critLo', 'critHi', 'min', 'max'] as const;

/** Drops empty optional fields so layouts compare and serialise stably. */
function cleanWidget(w: z.infer<typeof WidgetSchema>): Widget {
  const out: Widget = { uid: w.uid, id: w.id, type: w.type };
  const more = (w.more ?? []).filter((m, i, a) => m !== w.id && a.indexOf(m) === i).slice(0, MAX_TABLE - 1);
  if (more.length) out.more = more;
  if (w.diff && more.length === 1) out.diff = true;
  if (w.cols) out.cols = w.cols;
  if (w.h && w.h !== 's') out.h = w.h;
  const title = w.title?.trim();
  if (title) out.title = title;
  if (w.dec !== undefined) out.dec = w.dec;
  for (const k of SETTING_KEYS) if (w[k] !== undefined) out[k] = w[k];
  if (out.min !== undefined && out.max !== undefined && out.max <= out.min) {
    delete out.min;
    delete out.max;
  }
  return out;
}

const SectionSchema = z.object({ key: z.string().min(1).max(40), title: z.string().max(80) });

/**
 * Canonical form: sections in the given order (duplicates and malformed ones dropped), each with a widget
 * list; malformed widgets dropped; duplicate uids dropped (first wins). null if the input is not a dashboard.
 */
export function canonicalizeDashboard(input: unknown): Dashboard | null {
  if (!input || typeof input !== 'object' || Array.isArray(input)) return null;
  const src = input as { sections?: unknown; widgets?: unknown };
  if (!Array.isArray(src.sections)) return null;
  const widgets =
    src.widgets && typeof src.widgets === 'object' && !Array.isArray(src.widgets)
      ? (src.widgets as Record<string, unknown>)
      : {};
  const out: Dashboard = { sections: [], widgets: {} };
  const seen = new Set<string>();
  for (const s of src.sections) {
    const r = SectionSchema.safeParse(s);
    if (!r.success || r.data.key in out.widgets) continue;
    out.sections.push({ key: r.data.key, title: r.data.title || r.data.key });
    const list = Array.isArray(widgets[r.data.key]) ? (widgets[r.data.key] as unknown[]) : [];
    out.widgets[r.data.key] = [];
    for (const w of list) {
      const p = WidgetSchema.safeParse(w);
      if (!p.success || seen.has(p.data.uid)) continue;
      seen.add(p.data.uid);
      out.widgets[r.data.key]!.push(cleanWidget(p.data));
    }
  }
  return out;
}

// ---------- serialisation

/** Section titles of the fixed layouts saved before the MIB registry (formats v1 and v2). */
const LEGACY_TITLES: Record<string, string> = {
  status: 'Status',
  discharge: 'Discharge',
  cathode: 'Cathode',
  ppu: 'PPU power',
  press: 'Pressures',
  temp: 'Temperatures',
  rails: 'Board voltages',
  counters: 'Counters and bitmaps',
  lasttc: 'Last TC echo',
};

type SettingsTuple = {
  t?: string;
  d?: number;
  wl?: number;
  wh?: number;
  cl?: number;
  ch?: number;
  mn?: number;
  mx?: number;
};

function settingsTuple(w: Widget): SettingsTuple | null {
  const o: SettingsTuple = {};
  if (w.title) o.t = w.title;
  if (w.dec !== undefined) o.d = w.dec;
  if (w.warnLo !== undefined) o.wl = w.warnLo;
  if (w.warnHi !== undefined) o.wh = w.warnHi;
  if (w.critLo !== undefined) o.cl = w.critLo;
  if (w.critHi !== undefined) o.ch = w.critHi;
  if (w.min !== undefined) o.mn = w.min;
  if (w.max !== undefined) o.mx = w.max;
  return Object.keys(o).length ? o : null;
}

/** Deterministic JSON (v3): sections in dashboard order, widget fields in a fixed order. */
export function serializeDashboard(d: Dashboard): string {
  const c = canonicalizeDashboard(d) ?? { sections: [], widgets: {} };
  return JSON.stringify({
    v: 3,
    sections: c.sections.map((s) => [
      s.key,
      s.title,
      (c.widgets[s.key] ?? []).map((w) => {
        const t: unknown[] = [w.uid, w.id, w.type, w.more ?? [], w.diff ? 1 : 0, w.cols ?? 0, w.h ?? 's'];
        const st = settingsTuple(w);
        if (st) t.push(st);
        return t;
      }),
    ]),
  });
}

function widgetFromTuple(w: unknown): unknown {
  if (!Array.isArray(w)) return null;
  const st = (w[7] && typeof w[7] === 'object' ? w[7] : {}) as SettingsTuple;
  return {
    uid: w[0],
    id: w[1],
    type: w[2],
    more: w[3],
    diff: w[4] === 1 ? true : undefined,
    cols: w[5] || undefined,
    h: w[6],
    title: st.t,
    dec: st.d,
    warnLo: st.wl,
    warnHi: st.wh,
    critLo: st.cl,
    critHi: st.ch,
    min: st.mn,
    max: st.mx,
  };
}

/** Reads v3, and the fixed-section v1/v2 formats (their sections get their old titles). */
export function deserializeDashboard(raw: unknown): Dashboard | null {
  if (!raw || typeof raw !== 'object') return null;
  const o = raw as { v?: unknown; sections?: unknown };
  if (!Array.isArray(o.sections) || (o.v !== 1 && o.v !== 2 && o.v !== 3)) return null;
  const sections: SectionMeta[] = [];
  const widgets: Record<string, unknown> = {};
  for (const entry of o.sections) {
    if (!Array.isArray(entry) || typeof entry[0] !== 'string') continue;
    const legacy = o.v !== 3;
    const title = legacy ? (LEGACY_TITLES[entry[0]] ?? entry[0]) : entry[1];
    const list = legacy ? entry[1] : entry[2];
    if (typeof title !== 'string' || !Array.isArray(list)) continue;
    sections.push({ key: entry[0], title });
    widgets[entry[0]] = (list as unknown[]).map(widgetFromTuple);
  }
  return canonicalizeDashboard({ sections, widgets });
}

// ---------- storage (per user and MIB sha)

const shaPart = (sha: string) => (sha || 'none').slice(0, 16);
export const dashboardKey = (userId: string, sha: string) => `mcc.layout.v3.${userId}.${shaPart(sha)}`;
export const derivedKey = (userId: string, sha: string) => `mcc.derived.v2.${userId}.${shaPart(sha)}`;
/** Keys used before layouts were per MIB. */
export const legacyLayoutKey = (userId: string) => `mcc.layout.v1.${userId}`;
export const legacyDerivedKey = (userId: string) => `mcc.derived.v1.${userId}`;
const adoptedKey = (what: string, userId: string) => `mcc.${what}.legacyAdoptedBy.${userId}`;

/**
 * A layout saved before layouts were per MIB belongs to the MIB that was current then: it is adopted by the
 * first MIB that finds no layout of its own, and only by that one. The legacy entry itself is left in place.
 */
function adoptLegacy<T>(what: string, userId: string, sha: string, legacy: T | null): T | null {
  const marker = readString('local', adoptedKey(what, userId));
  if (marker && marker !== shaPart(sha)) return null;
  if (legacy) writeString('local', adoptedKey(what, userId), shaPart(sha));
  return legacy;
}

export function loadDashboard(userId: string, sha: string): Dashboard | null {
  const own = deserializeDashboard(readJson('local', dashboardKey(userId, sha)));
  if (own) return own;
  return adoptLegacy('layout', userId, sha, deserializeDashboard(readJson('local', legacyLayoutKey(userId))));
}

export function saveDashboard(userId: string, sha: string, d: Dashboard): boolean {
  return writeString('local', dashboardKey(userId, sha), serializeDashboard(d));
}

const DerivedSchema = z.array(
  z.object({
    id: z.string().regex(DERIVED_ID),
    name: z.string().min(1).max(80),
    expr: z.string().min(1).max(200),
    unit: z.string().max(16),
  }),
);

/** The user's derived parameters for this MIB, or null when there are none saved (use the MIB's). */
export function loadDerived(userId: string, sha: string): DerivedDef[] | null {
  const own = DerivedSchema.safeParse(readJson('local', derivedKey(userId, sha)));
  if (own.success) return own.data;
  const legacy = DerivedSchema.safeParse(readJson('local', legacyDerivedKey(userId)));
  return adoptLegacy('derived', userId, sha, legacy.success ? legacy.data : null);
}

export function saveDerived(userId: string, sha: string, defs: readonly DerivedDef[]): boolean {
  return writeString('local', derivedKey(userId, sha), JSON.stringify(defs));
}

// ---------- editing

/** Moves the widget `uid` to the position of `overUid` (possibly across sections). */
export function moveWidget(layout: Layout, uid: string, overUid: string): Layout {
  if (uid === overUid) return layout;
  const find = (l: Layout, u: string): [string, number] | null => {
    for (const k of Object.keys(l)) {
      const i = l[k]!.findIndex((w) => w.uid === u);
      if (i >= 0) return [k, i];
    }
    return null;
  };
  const from = find(layout, uid);
  const to = find(layout, overUid);
  if (!from || !to) return layout;
  const next: Layout = Object.fromEntries(Object.entries(layout).map(([k, v]) => [k, [...v]]));
  const [w] = next[from[0]]!.splice(from[1], 1);
  const target = next[to[0]]!;
  const idx = target.findIndex((x) => x.uid === overUid);
  // Moving down within a section lands after the target, as dnd-kit's arrayMove does.
  const insertAt = from[0] === to[0] && from[1] < to[1] ? idx + 1 : idx;
  target.splice(insertAt, 0, w!);
  return next;
}

export const newUid = () => `u${Date.now().toString(36)}${Math.random().toString(36).slice(2, 6)}`;
