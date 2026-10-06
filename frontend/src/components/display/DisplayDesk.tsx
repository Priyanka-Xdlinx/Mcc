import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import type { Row, Status } from '../../types'
import type { LastFrame } from '../../hooks/useTelemetryStream'
import { ago } from '../../format'
import { Icon } from '../Icon'
import {
  api, ApiError, emptyParams, nextPageNo, parseLayout, uniquePageNos,
  type Draft, type Layout, type LayoutWindow, type Page, type User,
} from './api'
import { DEFAULT_H, DisplayWindow, EMPTY_PARAMS, MIN_H, MIN_W, type Geo, type WinActions, type WinState } from './DisplayWindow'
import { buildIndex, type IndexEntry } from './ParamPicker'
import { setHeader } from './headerStore'
import { Library } from './Library'
import './display.css'

/** Seconds between samples per tier; a value older than 3 periods is stale. */
const PERIOD_S: Record<Row['tier'], number> = { VH: 1, H: 2, M: 4, L: 8 }
const UNTITLED = 'Untitled page'
/** The layout is saved this long after the last change. */
const SAVE_DELAY_MS = 300
/** A failed layout save is tried again after this long. */
const RETRY_MS = 5000

function toWinStates(lw: LayoutWindow[]): WinState[] {
  return lw.map((l) => ({
    ...l,
    w: Math.max(MIN_W, l.w),
    h: Math.max(MIN_H, l.h),
    page: null,
    error: null,
    busy: false,
    loading: l.pageId != null,
  }))
}

const paramsOf = (w: WinState) => w.draft?.params ?? w.page?.params ?? EMPTY_PARAMS
const nameOf = (w: WinState) => w.draft?.name ?? w.page?.name ?? UNTITLED

function serialize(ws: WinState[]): string {
  const layout: Layout = {
    v: 1,
    windows: ws.map(({ key, pageId, draft, x, y, w, h, z, min, pageNo }) => ({ key, pageId, draft, x, y, w, h, z, min, pageNo })),
  }
  return JSON.stringify(layout)
}

let keySeq = 0
const newKey = () => `w${Date.now().toString(36)}${(keySeq++).toString(36)}`

const clampTo = (v: number, lo: number, hi: number) => Math.min(Math.max(v, lo), Math.max(lo, hi))

/** Keep every window's top-left on the desk (sizes are left alone). The desk scrolls down, so only the top is bounded. */
function clampAll(ws: WinState[], W: number): WinState[] {
  let changed = false
  const out = ws.map((w) => {
    const x = clampTo(w.x, 0, W - Math.min(w.w, W))
    const y = Math.max(0, w.y)
    if (x === w.x && y === w.y) return w
    changed = true
    return { ...w, x, y }
  })
  return changed ? out : ws
}

type SaveState =
  | { kind: 'nouser' }
  | { kind: 'loading' }
  | { kind: 'pending' }
  | { kind: 'saved' }
  | { kind: 'error'; msg: string; restore?: boolean }

export function DisplayDesk({
  rows,
  status,
  live,
  lastMsgAt,
  lastFrame,
  focused = false,
}: {
  /** Focus mode: toolbar hidden, windows highlighted. */
  focused?: boolean
  rows: Row[]
  status: Status | null
  live: boolean
  lastMsgAt: number | null
  lastFrame: LastFrame | null
}) {
  const [users, setUsers] = useState<User[] | null>(null)
  const [usersErr, setUsersErr] = useState<string | null>(null)
  const [userId, setUserId] = useState<number | null>(null)
  /** false until the backend has said which operator this browser is. */
  const [sessionReady, setSessionReady] = useState(false)
  const [newName, setNewName] = useState<string | null>(null)
  const [nameErr, setNameErr] = useState<string | null>(null)
  const [windows, setWindows] = useState<WinState[]>([])
  const [libOpen, setLibOpen] = useState(false)
  const [arrangeOpen, setArrangeOpen] = useState(false)
  const [save, setSave] = useState<SaveState>({ kind: 'nouser' })
  const [restoreNonce, setRestoreNonce] = useState(0)
  const [now, setNow] = useState(() => Date.now())

  const deskRef = useRef<HTMLDivElement>(null)
  const windowsRef = useRef(windows)
  windowsRef.current = windows
  const userIdRef = useRef(userId)
  userIdRef.current = userId
  const restoredFor = useRef<number | null>(null)
  const lastSaved = useRef<string | null>(null)
  const pending = useRef<{ uid: number; json: string } | null>(null)
  const retryTimer = useRef<ReturnType<typeof setTimeout> | null>(null)
  const arrangeRef = useRef<HTMLDivElement>(null)
  const scrollTarget = useRef<number | null>(null)

  // ── feed header, shared by every window through a tiny store ──────────────
  useEffect(() => {
    const f = status?.feed
    setHeader({
      frameTime: lastFrame?.spacecraftTime ?? f?.spacecraftTime ?? null,
      frameNo: lastFrame
        ? `MF#${lastFrame.masterFrame} SF${lastFrame.subframe}`
        : f && f.masterFrame >= 0 ? `MF#${f.masterFrame}` : null,
      receivedAt: lastFrame?.receivedAt ?? f?.lastReceivedAt ?? null,
      framesValid: f?.framesValid ?? f?.subframesReceived ?? null,
      framesRejected: f?.framesRejected ?? null,
      session: f?.sessionSubframes ?? null,
    })
  }, [lastFrame, status])

  useEffect(() => {
    const t = setInterval(() => setNow(Date.now()), 1000)
    return () => clearInterval(t)
  }, [])

  // ── rows: one Map per update, plus when each parameter last changed here ──
  const seen = useRef(new Map<number, { row: Row; at: number }>())
  const rowMap = useMemo(() => {
    const m = new Map<number, Row>()
    const t = Date.now()
    for (const r of rows) {
      m.set(r.parameter_id, r)
      const e = seen.current.get(r.parameter_id)
      if (!e) {
        const at = r.received_at ? Date.parse(r.received_at) : NaN
        seen.current.set(r.parameter_id, { row: r, at: Number.isFinite(at) ? Math.min(at, t) : 0 })
      } else if (e.row !== r) {
        const p = e.row
        if (p.master_frame !== r.master_frame || p.subframe !== r.subframe || p.received_at !== r.received_at ||
            p.value !== r.value || p.quality !== r.quality) e.at = t
        e.row = r
      }
    }
    return m
  }, [rows])

  const rowsRef = useRef(rows)
  rowsRef.current = rows
  const index = useRef<{ n: number; e: IndexEntry[] }>({ n: -1, e: [] })
  const getIndex = useCallback(() => {
    const r = rowsRef.current
    if (index.current.n !== r.length) index.current = { n: r.length, e: buildIndex(r) }
    return index.current.e
  }, [])

  const getDesk = useCallback(() => {
    const el = deskRef.current
    return { w: el?.clientWidth ?? 1200, h: el?.clientHeight ?? 700, top: el?.scrollTop ?? 0 }
  }, [])

  // ── users ─────────────────────────────────────────────────────────────────
  const loadUsers = useCallback(() => {
    api
      .users()
      .then((u) => {
        setUsers(Array.isArray(u) ? u : [])
        setUsersErr(null)
      })
      .catch((e: Error) => setUsersErr(e.message))
  }, [])
  useEffect(loadUsers, [loadUsers])

  // ── which operator this browser is: the backend remembers it in a cookie ──
  useEffect(() => {
    let cancelled = false
    let t: ReturnType<typeof setTimeout> | null = null
    const ask = () => {
      api
        .session()
        .then((s) => {
          if (cancelled) return
          setUserId(s?.user?.id ?? null)
          setSessionReady(true)
        })
        .catch(() => {
          // Backend not up yet: keep asking; nothing can be shown or saved without it.
          if (!cancelled) t = setTimeout(ask, RETRY_MS)
        })
    }
    ask()
    return () => {
      cancelled = true
      if (t) clearTimeout(t)
    }
  }, [])

  // ── saved pages behind restored windows are fetched from the backend ─────
  const loadPages = useCallback((ids: number[]) => {
    for (const id of new Set(ids)) {
      api
        .page(id)
        .then((p) => {
          setWindows((ws) => ws.map((w) => (w.pageId === id ? { ...w, page: p, loading: false } : w)))
        })
        .catch((e: Error) => {
          const gone = e instanceof ApiError && e.status === 404
          setWindows((ws) =>
            ws.map((w) =>
              w.pageId !== id
                ? w
                : gone
                  ? {
                      ...w, loading: false, pageId: null,
                      draft: w.draft ?? { name: UNTITLED, params: emptyParams() },
                      error: `Saved page #${id} no longer exists; this is now an unsaved copy.`,
                    }
                  : { ...w, loading: false, error: `Could not load page #${id}: ${e.message}` },
            ),
          )
        })
    }
  }, [])
  const pageIdsOf = (ws: { pageId: number | null }[]) => ws.map((w) => w.pageId).filter((x): x is number => x != null)

  // ── workspace save: debounced PUT of the layout, the only copy kept ───────
  const flush = useCallback(async (keepalive = false) => {
    const p = pending.current
    if (!p) return
    pending.current = null
    if (retryTimer.current) {
      clearTimeout(retryTimer.current)
      retryTimer.current = null
    }
    if (keepalive) {
      try {
        void fetch(`/api/users/${p.uid}/workspace`, {
          method: 'PUT',
          keepalive: true,
          headers: { 'Content-Type': 'application/json', 'X-MCC-User': String(p.uid) },
          body: JSON.stringify({ layout: JSON.parse(p.json) }),
        })
      } catch {
        /* the page is going away; nothing more to do */
      }
      return
    }
    try {
      await api.saveWorkspace(p.uid, JSON.parse(p.json) as Layout)
      if (p.uid === userIdRef.current) {
        lastSaved.current = p.json
        if (!pending.current) setSave({ kind: 'saved' })
      }
    } catch (e) {
      // Nothing else holds this layout: keep it and try again, unless a newer one is already waiting.
      if (!pending.current) pending.current = p
      retryTimer.current = setTimeout(() => void flush(), RETRY_MS)
      if (p.uid === userIdRef.current) setSave({ kind: 'error', msg: `Not saved: ${(e as Error).message}` })
    }
  }, [])

  useEffect(() => {
    if (userId == null || restoredFor.current !== userId) return
    const json = serialize(windows)
    if (json === lastSaved.current) return
    pending.current = { uid: userId, json }
    setSave({ kind: 'pending' })
    const t = setTimeout(() => void flush(), SAVE_DELAY_MS)
    return () => clearTimeout(t)
  }, [windows, userId, flush])

  useEffect(() => {
    const onUnload = () => void flush(true)
    window.addEventListener('beforeunload', onUnload)
    return () => {
      window.removeEventListener('beforeunload', onUnload)
      if (retryTimer.current) clearTimeout(retryTimer.current)
      void flush() // leaving the tab: send what is waiting now
    }
  }, [flush])

  // ── workspace restore when a user is chosen (or remembered) ───────────────
  useEffect(() => {
    restoredFor.current = null
    setWindows([]) // never show one operator's windows under another's name
    if (userId == null) {
      setSave({ kind: 'nouser' })
      return
    }
    let cancelled = false
    setSave({ kind: 'loading' })
    api
      .workspace(userId)
      .then((res) => {
        if (cancelled) return
        const restored = toWinStates(parseLayout(res?.layout ?? null) ?? [])
        lastSaved.current = serialize(restored)
        restoredFor.current = userId
        setWindows(uniquePageNos(clampAll(restored, getDesk().w)))
        setSave({ kind: 'saved' })
        loadPages(pageIdsOf(restored))
      })
      .catch((e: Error) => {
        if (cancelled) return
        // Do not overwrite a layout we could not read.
        setSave({ kind: 'error', msg: `Could not load your layout: ${e.message}`, restore: true })
      })
    return () => {
      cancelled = true
    }
  }, [userId, restoreNonce, getDesk, loadPages])

  const chooseUser = (id: number | null) => {
    void flush()
    setUserId(id)
    // If this fails the choice holds until a refresh, which then asks for the name again.
    api.setSession(id).catch(() => {})
  }

  const createUser = async (e: React.FormEvent) => {
    e.preventDefault()
    const n = (newName ?? '').trim()
    if (!n) return
    if (n.length > 40) {
      setNameErr('At most 40 characters')
      return
    }
    try {
      const u = await api.createUser(n)
      setUsers((us) => {
        const list = (us ?? []).filter((x) => x.id !== u.id)
        return [...list, u].sort((a, b) => a.name.localeCompare(b.name))
      })
      setNewName(null)
      setNameErr(null)
      chooseUser(u.id)
    } catch (err) {
      setNameErr((err as Error).message)
    }
  }

  // ── window actions (one stable object, so windows stay memoised) ──────────
  const actions = useMemo<WinActions>(() => {
    const patch = (key: string, p: Partial<WinState>) =>
      setWindows((ws) => ws.map((w) => (w.key === key ? { ...w, ...p } : w)))
    const withDraft = (key: string, f: (d: Draft) => Draft) =>
      setWindows((ws) =>
        ws.map((w) => (w.key === key ? { ...w, draft: f({ name: nameOf(w), params: paramsOf(w).slice() }) } : w)),
      )
    const store = async (key: string, asName: string | null): Promise<boolean> => {
      const uid = userIdRef.current
      const w = windowsRef.current.find((x) => x.key === key)
      if (!w) return false
      if (uid == null) {
        patch(key, { error: 'Pick your name in the toolbar first.' })
        return false
      }
      const sentDraft = w.draft
      const d: Draft = { name: (asName ?? nameOf(w)).trim(), params: paramsOf(w).slice() }
      if (!d.name) {
        patch(key, { error: 'Give the page a name first.' })
        return false
      }
      patch(key, { busy: true, error: null })
      try {
        const p = asName != null || w.pageId == null
          ? await api.createPage(uid, d)
          : await api.updatePage(uid, w.pageId, d)
        setWindows((ws) =>
          ws.map((x) =>
            x.key !== key
              ? x
              : {
                  ...x, busy: false, page: p, pageId: p.id,
                  // Keep edits made while the request was in flight.
                  draft: x.draft !== sentDraft ? x.draft : null,
                },
          ),
        )
        loadUsers()
        return true
      } catch (e) {
        patch(key, { busy: false, error: (e as Error).message })
        return false
      }
    }
    const raise = (ws: WinState[], key: string, extra?: Partial<WinState>) => {
      let maxZ = 0
      let atMax = 0
      for (const w of ws) {
        if (w.min) continue
        if (w.z > maxZ) {
          maxZ = w.z
          atMax = 1
        } else if (w.z === maxZ) atMax++
      }
      const me = ws.find((w) => w.key === key)
      if (!me) return ws
      if (!extra && !me.min && me.z === maxZ && atMax === 1) return ws
      return ws.map((w) => (w.key === key ? { ...w, ...extra, z: maxZ + 1 } : w))
    }
    return {
      focus: (key) => setWindows((ws) => raise(ws, key)),
      geometry: (key, g: Geo) => patch(key, { x: g.x, y: g.y, w: g.w, h: g.h }),
      setParam: (key, slot, id) =>
        withDraft(key, (d) => {
          d.params[slot] = id
          return d
        }),
      rename: (key, name) => withDraft(key, (d) => ({ ...d, name })),
      save: (key) => store(key, null),
      saveAs: (key, name) => store(key, name),
      minimize: (key) => patch(key, { min: true }),
      close: (key) => setWindows((ws) => ws.filter((w) => w.key !== key)),
      dismissError: (key) => patch(key, { error: null }),
      // used by the dock
      restore: (key: string) => setWindows((ws) => raise(ws, key, { min: false })),
    } as WinActions & { restore: (key: string) => void }
  }, [loadUsers])
  const restoreWin = (actions as WinActions & { restore: (key: string) => void }).restore

  // Bring a newly opened window into view once it is on the desk.
  useEffect(() => {
    const y = scrollTarget.current
    if (y == null) return
    scrollTarget.current = null
    deskRef.current?.scrollTo({ top: Math.max(0, y - 8), behavior: 'smooth' })
  }, [windows])

  const openWindow = (page: Page | null) => {
    const desk = getDesk()
    setWindows((ws) => {
      if (page) {
        const open = ws.find((w) => w.pageId === page.id)
        if (open) {
          const maxZ = ws.reduce((m, w) => Math.max(m, w.z), 0)
          return ws.map((w) => (w === open ? { ...w, page, min: false, z: maxZ + 1 } : w))
        }
      }
      const w = Math.max(MIN_W, Math.min(880, desk.w - 48))
      const h = Math.max(MIN_H, Math.min(DEFAULT_H, desk.h - 40))
      const maxZ = ws.reduce((m, x) => Math.max(m, x.z), 0)
      // New windows stack below the lowest open one instead of covering it.
      const bottom = ws.reduce((m, x) => (x.min ? m : Math.max(m, x.y + x.h)), -1)
      const y = bottom < 0 ? 16 : bottom + 16
      scrollTarget.current = y
      const win: WinState = {
        key: newKey(),
        pageNo: nextPageNo(ws),
        pageId: page?.id ?? null,
        page,
        draft: page ? null : { name: UNTITLED, params: emptyParams() },
        x: clampTo(16, 0, desk.w - w),
        y,
        w, h,
        z: maxZ + 1,
        min: false,
        error: null,
        busy: false,
        loading: false,
      }
      return [...ws, win]
    })
  }

  /** Copies the front window into a new unsaved page; editing it leaves the original alone. */
  const copyWindow = () => {
    setWindows((ws) => {
      let src: WinState | null = null
      for (const w of ws) if (!w.min && (!src || w.z > src.z)) src = w
      if (!src) return ws
      const maxZ = ws.reduce((m, x) => Math.max(m, x.z), 0)
      const bottom = ws.reduce((m, x) => (x.min ? m : Math.max(m, x.y + x.h)), -1)
      const y = bottom < 0 ? 16 : bottom + 16
      scrollTarget.current = y
      const copy: WinState = {
        key: newKey(),
        pageNo: nextPageNo(ws),
        pageId: null,
        page: null,
        draft: { name: `${nameOf(src)} (copy)`, params: paramsOf(src).slice() },
        x: src.x, y, w: src.w, h: src.h,
        z: maxZ + 1,
        min: false,
        error: null,
        busy: false,
        loading: false,
      }
      return [...ws, copy]
    })
  }

  const arrange = (how: 'cascade' | 'tile') => {
    setArrangeOpen(false)
    const desk = getDesk()
    setWindows((ws) => {
      const vis = ws.filter((w) => !w.min).sort((a, b) => a.z - b.z)
      if (!vis.length) return ws
      const geo = new Map<string, Geo>()
      if (how === 'cascade') {
        const w = Math.max(MIN_W, Math.min(880, desk.w - 48 - (vis.length - 1) * 28))
        const h = Math.max(MIN_H, Math.min(DEFAULT_H, desk.h - 40 - (vis.length - 1) * 28))
        vis.forEach((v, i) => {
          geo.set(v.key, {
            x: clampTo(16 + i * 28, 0, desk.w - w),
            y: desk.top + clampTo(12 + i * 28, 0, desk.h - h),
            w, h,
          })
        })
      } else {
        const cols = Math.max(1, Math.min(vis.length, Math.floor(desk.w / MIN_W) || 1, Math.ceil(Math.sqrt(vis.length))))
        const rowsN = Math.ceil(vis.length / cols)
        const w = Math.max(MIN_W, Math.floor((desk.w - 8) / cols) - 8)
        const h = Math.max(MIN_H, Math.floor((desk.h - 8) / rowsN) - 8)
        vis.forEach((v, i) => {
          const c = i % cols
          const r = Math.floor(i / cols)
          geo.set(v.key, {
            x: clampTo(8 + c * (w + 8), 0, desk.w - w),
            y: desk.top + clampTo(8 + r * (h + 8), 0, desk.h - h),
            w, h,
          })
        })
      }
      return ws.map((w) => {
        const g = geo.get(w.key)
        return g ? { ...w, ...g } : w
      })
    })
  }

  // Keep windows reachable when the browser window shrinks.
  useEffect(() => {
    const on = () => {
      const d = getDesk()
      setWindows((ws) => clampAll(ws, d.w))
    }
    window.addEventListener('resize', on)
    return () => window.removeEventListener('resize', on)
  }, [getDesk])

  // Esc and outside clicks close the Arrange menu.
  useEffect(() => {
    if (!arrangeOpen) return
    const key = (e: KeyboardEvent) => e.key === 'Escape' && setArrangeOpen(false)
    const down = (e: PointerEvent) => {
      if (!arrangeRef.current?.contains(e.target as Node)) setArrangeOpen(false)
    }
    window.addEventListener('keydown', key)
    window.addEventListener('pointerdown', down)
    return () => {
      window.removeEventListener('keydown', key)
      window.removeEventListener('pointerdown', down)
    }
  }, [arrangeOpen])

  // ── per-window inputs: 20 resolved rows and a stale mask ──────────────────
  let topKey: string | null = null
  let topZ = -Infinity
  for (const w of windows) if (!w.min && w.z > topZ) {
    topZ = w.z
    topKey = w.key
  }

  const user = users?.find((u) => u.id === userId) ?? null
  const minimized = windows.filter((w) => w.min)

  // Pages need a name, and the layout must be back from the server first, or it would be overwritten.
  const ready = userId != null && save.kind !== 'loading' && !(save.kind === 'error' && save.restore)
  const needName = userId == null ? 'Pick your name first' : undefined

  const saveText = (() => {
    if (!sessionReady) return 'Connecting to the backend…'
    switch (save.kind) {
      case 'nouser': return 'Pick your name to start'
      case 'loading': return 'Loading your layout…'
      case 'pending': return 'Saving…'
      case 'saved': return 'Layout saved'
      case 'error': return save.msg
    }
  })()

  return (
    <div className={`dp-root${focused ? ' focused' : ''}`}>
      <div className="dp-toolbar" role="toolbar" aria-label="Display pages" hidden={focused}>
        <label className="dp-user">
          <Icon name="user" size={15} />
          <span className="sr">Operator</span>
          <select
            value={newName != null ? '__new' : userId ?? ''}
            onChange={(e) => {
              const v = e.target.value
              if (v === '__new') {
                setNewName('')
                setNameErr(null)
              } else {
                setNewName(null)
                chooseUser(v ? Number(v) : null)
              }
            }}
            aria-label="Operator"
          >
            <option value="">Pick your name…</option>
            {userId != null && !user && <option value={userId}>user #{userId}</option>}
            {(users ?? []).map((u) => (
              <option key={u.id} value={u.id}>
                {u.name} ({u.pages})
              </option>
            ))}
            <option value="__new">New name…</option>
          </select>
        </label>
        {newName != null && (
          <form className="dp-newname" onSubmit={createUser}>
            <input
              type="text"
              autoFocus
              maxLength={40}
              placeholder="Your name"
              value={newName}
              onChange={(e) => setNewName(e.target.value)}
              onKeyDown={(e) => e.key === 'Escape' && setNewName(null)}
            />
            <button type="submit" className="primary" disabled={!newName.trim()}>OK</button>
            <button type="button" onClick={() => setNewName(null)}>Cancel</button>
            {nameErr && <span className="err">{nameErr}</span>}
          </form>
        )}
        {usersErr && (
          <span className="err" title={usersErr}>
            Users unavailable <button type="button" className="link" onClick={loadUsers}>retry</button>
          </span>
        )}
        <span className="dp-sep" />
        <button type="button" onClick={() => openWindow(null)} disabled={!ready} title={needName}>
          <Icon name="plus" size={14} /> New page
        </button>
        <button
          type="button"
          onClick={copyWindow}
          disabled={!ready || !windows.some((w) => !w.min)}
          title="Copy the front page into a new page you can edit without changing the original"
        >
          <Icon name="copy" size={14} /> Copy page
        </button>
        <button type="button" onClick={() => setLibOpen(true)} disabled={!ready} title={needName}>
          <Icon name="folder" size={14} /> Open saved…
        </button>
        <div className="dp-menuwrap" ref={arrangeRef}>
          <button
            type="button"
            aria-haspopup="menu"
            aria-expanded={arrangeOpen}
            onClick={() => setArrangeOpen((v) => !v)}
            disabled={!windows.some((w) => !w.min)}
          >
            <Icon name="grid" size={14} /> Arrange
          </button>
          {arrangeOpen && (
            <div className="dp-menu" role="menu">
              <button type="button" role="menuitem" autoFocus onClick={() => arrange('cascade')}>Cascade</button>
              <button type="button" role="menuitem" onClick={() => arrange('tile')}>Tile</button>
            </div>
          )}
        </div>
        <div className="grow" />
        <span className="dp-feed" title={live ? 'Telemetry stream connected' : 'Telemetry stream offline'}>
          <span className={`dot ${live ? 'on' : 'off'}`} />
          {live ? `last frame ${ago(lastMsgAt, now)}` : 'stream offline'}
        </span>
        <span className={`dp-savestate ${save.kind}`} role="status" title={save.kind === 'error' ? save.msg : undefined}>
          {saveText}
          {save.kind === 'error' && (
            <button
              type="button"
              className="link"
              onClick={() => {
                if (save.restore) setRestoreNonce((n) => n + 1)
                else {
                  lastSaved.current = null
                  setWindows((ws) => [...ws])
                }
              }}
            >
              retry
            </button>
          )}
        </span>
      </div>

      <div className="dp-desk" ref={deskRef}>
        {windows.some((w) => !w.min) && (
          <div
            className="dp-spacer"
            aria-hidden="true"
            style={{ top: windows.reduce((m, w) => (w.min ? m : Math.max(m, w.y + w.h)), 0) + 120 }}
          />
        )}
        {windows.length === 0 && (
          <div className="dp-hint">
            <Icon name="display" size={28} />
            {!sessionReady ? (
              <p>Connecting to the MCC backend…</p>
            ) : userId == null ? (
              <p>
                <b>Pick your name</b> in the toolbar to start. Your pages and window layout are kept on the
                server under your name.
              </p>
            ) : save.kind === 'loading' ? (
              <p>Loading your layout…</p>
            ) : (
              <p>
                No display pages open. <b>New page</b> starts an empty one; <b>Open saved…</b> lists every
                operator's pages.
              </p>
            )}
          </div>
        )}
        {windows.map((w) => {
          if (w.min) return null
          const params = paramsOf(w)
          const slots = params.map((id) => (id == null ? null : rowMap.get(id) ?? null))
          let stale = ''
          for (let s = 0; s < slots.length; s++) {
            const r = slots[s]
            const e = r && r.value != null ? seen.current.get(r.parameter_id) : undefined
            stale += e && now - e.at > 3000 * (PERIOD_S[r!.tier] ?? 8) ? '1' : '0'
          }
          return (
            <DisplayWindow
              key={w.key}
              win={w}
              pageNo={w.pageNo}
              top={w.key === topKey}
              slots={slots}
              stale={stale}
              userId={userId}
              actions={actions}
              getIndex={getIndex}
              getDesk={getDesk}
            />
          )
        })}
      </div>

      {minimized.length > 0 && (
        <div className="dp-dock" aria-label="Minimized pages">
          {minimized.map((w) => (
            <button key={w.key} type="button" className="dp-chip" onClick={() => restoreWin(w.key)} title="Restore">
              <Icon name="display" size={13} />
              <span>page {w.pageNo}</span>
              <b>{nameOf(w)}</b>
              {w.draft && <span className="dp-dirty">•</span>}
            </button>
          ))}
        </div>
      )}

      {libOpen && (
        <Library
          users={users ?? []}
          userId={userId}
          onOpen={(p) => {
            setLibOpen(false)
            openWindow(p)
          }}
          onClose={() => setLibOpen(false)}
          onChanged={loadUsers}
        />
      )}
    </div>
  )
}
