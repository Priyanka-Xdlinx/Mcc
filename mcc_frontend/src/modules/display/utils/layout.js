/**
 * Pure helpers for display windows: the stored layout format, page numbers and
 * window geometry. No React here, so all of it is unit-tested.
 *
 * A user's whole workspace is one or more independent screens, each its own
 * empty canvas of windows; switching screens never touches another screen's
 * windows. Stored layout (frontend-owned JSON, kept per user by the backend):
 *   { v: 2, active: screenId, screens: [{ id, name, windows: [...] }] }
 * One window: { key, pageId, draft, x, y, w, h, z, pageNo }; `draft` is
 * `{ name, params }` while it holds unsaved changes. A v1 layout (one screen,
 * no `screens` array) is read as a single screen named "Screen 1".
 */

/** Parameter slots on one display page. */
export const SLOTS = 20;
export const UNTITLED = "Untitled page";

export const MIN_W = 560;
export const MIN_H = 300;
/** Height of a new window: fits the header and the 10 compact rows at the default width. */
export const DEFAULT_H = 400;

export const emptyParams = () => Array.from({ length: SLOTS }, () => null);

/** A frozen empty slot list, shared so it never breaks memoisation. */
export const EMPTY_PARAMS = Object.freeze(emptyParams());

/** Exactly 20 entries, each an integer id or null, whatever came in. */
export function normParams(p) {
  const out = emptyParams();
  if (Array.isArray(p)) {
    for (let i = 0; i < SLOTS; i++) out[i] = Number.isInteger(p[i]) ? p[i] : null;
  }
  return out;
}

const num = (v, d) => (typeof v === "number" && Number.isFinite(v) ? v : d);

/** Smallest positive page number not used by any window. */
export function nextPageNo(ws) {
  const used = new Set(ws.map((w) => w.pageNo));
  let n = 1;
  while (used.has(n)) n++;
  return n;
}

/** Keeps each window's number, and gives a new one to any missing (0) or repeated number. */
export function uniquePageNos(ws) {
  const seen = new Set();
  const bad = [];
  ws.forEach((w, i) => {
    if (w.pageNo > 0 && !seen.has(w.pageNo)) seen.add(w.pageNo);
    else bad.push(i);
  });
  if (!bad.length) return ws;
  const out = ws.slice();
  for (const i of bad) {
    const n = nextPageNo(Array.from(seen, (pageNo) => ({ pageNo })));
    seen.add(n);
    out[i] = { ...out[i], pageNo: n };
  }
  return out;
}

/** Parses a stored layout defensively; anything unusable is dropped. Null when there is no layout at all. */
export function parseLayout(raw) {
  let v = raw;
  if (typeof v === "string") {
    try {
      v = JSON.parse(v);
    } catch {
      return null;
    }
  }
  if (!v || typeof v !== "object" || !Array.isArray(v.windows)) return null;
  const out = [];
  v.windows.forEach((o, i) => {
    if (!o || typeof o !== "object") return;
    const d = o.draft;
    const pageId = Number.isInteger(o.pageId) ? o.pageId : null;
    const draft =
      d && typeof d === "object" ? { name: typeof d.name === "string" ? d.name : UNTITLED, params: normParams(d.params) } : null;
    if (pageId == null && !draft) return;
    out.push({
      key: typeof o.key === "string" && o.key ? o.key : `r${i}`,
      pageId,
      draft,
      x: num(o.x, 24 + i * 28),
      y: num(o.y, 16 + i * 28),
      w: num(o.w, 820),
      h: num(o.h, 560),
      z: num(o.z, i + 1),
      pageNo: Number.isInteger(o.pageNo) && o.pageNo > 0 ? o.pageNo : 0,
    });
  });
  return uniquePageNos(out);
}

/** The stored form of a screen's windows (client-only fields dropped). */
function toStoredWindows(ws) {
  return ws.map(({ key, pageId, draft, x, y, w, h, z, pageNo }) => ({ key, pageId, draft, x, y, w, h, z, pageNo }));
}

/** One screen's windows, stored on their own (the v1 format, kept for parseLayout/tests). */
export function serializeLayout(ws) {
  return JSON.stringify({ v: 1, windows: toStoredWindows(ws) });
}

/** Stored windows as desk windows: client-only state added, sizes at least the minimum. */
export function toWinStates(lw) {
  return lw.map((l) => ({
    ...l,
    w: Math.max(MIN_W, l.w),
    h: Math.max(MIN_H, l.h),
    page: null,
    error: null,
    busy: false,
    loading: l.pageId != null,
  }));
}

export const paramsOf = (w) => w.draft?.params ?? w.page?.params ?? EMPTY_PARAMS;
export const nameOf = (w) => w.draft?.name ?? w.page?.name ?? UNTITLED;

export const clamp = (v, lo, hi) => Math.min(Math.max(v, lo), Math.max(lo, hi));

/** Keep every window's top-left on a desk `deskW` wide. The desk scrolls down, so only the top is bounded. */
export function clampAll(ws, deskW) {
  let changed = false;
  const out = ws.map((w) => {
    const x = clamp(w.x, 0, deskW - Math.min(w.w, deskW));
    const y = Math.max(0, w.y);
    if (x === w.x && y === w.y) return w;
    changed = true;
    return { ...w, x, y };
  });
  return changed ? out : ws;
}

let keySeq = 0;
export const newKey = () => `w${Date.now().toString(36)}${(keySeq++).toString(36)}`;

let screenSeq = 0;
export const newScreenId = () => `s${Date.now().toString(36)}${(screenSeq++).toString(36)}`;

/**
 * Parses the whole saved workspace: every screen and which one was active.
 * Reads a v1 layout (one screen's windows, no `screens` array) as a single
 * screen. Null when there is nothing saved at all.
 */
export function parseWorkspace(raw) {
  let v = raw;
  if (typeof v === "string") {
    try {
      v = JSON.parse(v);
    } catch {
      return null;
    }
  }
  if (!v || typeof v !== "object") return null;

  if (Array.isArray(v.windows)) {
    const id = newScreenId();
    return { active: id, screens: [{ id, name: "Screen 1", windows: parseLayout(v) ?? [] }] };
  }
  if (!Array.isArray(v.screens) || v.screens.length === 0) return null;

  const screens = v.screens.map((s, i) => ({
    id: typeof s?.id === "string" && s.id ? s.id : newScreenId(),
    name: typeof s?.name === "string" && s.name.trim() ? s.name.trim() : `Screen ${i + 1}`,
    windows: parseLayout({ windows: Array.isArray(s?.windows) ? s.windows : [] }) ?? [],
  }));
  const active = screens.some((s) => s.id === v.active) ? v.active : screens[0].id;
  return { active, screens };
}

/** The stored form of the whole workspace. */
export function serializeWorkspace(screens, active) {
  return JSON.stringify({
    v: 2,
    active,
    screens: screens.map((s) => ({ id: s.id, name: s.name, windows: toStoredWindows(s.windows) })),
  });
}

export const topZ = (ws) => ws.reduce((m, w) => Math.max(m, w.z), 0);

/** Where a new window goes: below the lowest open one, so it covers nothing. */
export const belowAll = (ws) => {
  const bottom = ws.reduce((m, x) => Math.max(m, x.y + x.h), -1);
  return bottom < 0 ? 16 : bottom + 16;
};

/**
 * New geometry for every window, by key: "cascade" offsets them diagonally,
 * "tile" packs them into a grid filling the desk.
 */
export function arrangeGeometry(ws, how, desk) {
  const vis = ws.slice().sort((a, b) => a.z - b.z);
  const geo = new Map();
  if (!vis.length) return geo;
  if (how === "cascade") {
    const w = Math.max(MIN_W, Math.min(880, desk.w - 48 - (vis.length - 1) * 28));
    const h = Math.max(MIN_H, Math.min(DEFAULT_H, desk.h - 40 - (vis.length - 1) * 28));
    vis.forEach((v, i) => {
      geo.set(v.key, { x: clamp(16 + i * 28, 0, desk.w - w), y: desk.top + clamp(12 + i * 28, 0, desk.h - h), w, h });
    });
  } else {
    const cols = Math.max(1, Math.min(vis.length, Math.floor(desk.w / MIN_W) || 1, Math.ceil(Math.sqrt(vis.length))));
    const rows = Math.ceil(vis.length / cols);
    const w = Math.max(MIN_W, Math.floor((desk.w - 8) / cols) - 8);
    const h = Math.max(MIN_H, Math.floor((desk.h - 8) / rows) - 8);
    vis.forEach((v, i) => {
      const c = i % cols;
      const r = Math.floor(i / cols);
      geo.set(v.key, { x: clamp(8 + c * (w + 8), 0, desk.w - w), y: desk.top + clamp(8 + r * (h + 8), 0, desk.h - h), w, h });
    });
  }
  return geo;
}

/** Raises one window to the front (optionally changing it); returns `ws` itself when nothing changes. */
export function raise(ws, key, extra) {
  let maxZ = 0;
  let atMax = 0;
  for (const w of ws) {
    if (w.z > maxZ) {
      maxZ = w.z;
      atMax = 1;
    } else if (w.z === maxZ) atMax++;
  }
  const me = ws.find((w) => w.key === key);
  if (!me) return ws;
  if (!extra && me.z === maxZ && atMax === 1) return ws;
  return ws.map((w) => (w.key === key ? { ...w, ...extra, z: maxZ + 1 } : w));
}
