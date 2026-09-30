import { memo, useCallback, useEffect, useRef, useState } from 'react'
import type { Row } from '../../types'
import { Icon } from '../Icon'
import type { LayoutWindow, Page } from './api'
import { HeaderBlock, SpecTable } from './SpecTable'
import { ParamPicker, type IndexEntry } from './ParamPicker'

export const MIN_W = 560
export const MIN_H = 380

/** A window on the desk: the persisted layout fields plus client-only state. */
export interface WinState extends LayoutWindow {
  page: Page | null
  error: string | null
  busy: boolean
  loading: boolean
}

export interface Geo {
  x: number
  y: number
  w: number
  h: number
}

/** Callbacks from the desk. The object is created once, so it never breaks memo. */
export interface WinActions {
  focus: (key: string) => void
  geometry: (key: string, g: Geo) => void
  setParam: (key: string, slot: number, id: number | null) => void
  rename: (key: string, name: string) => void
  save: (key: string) => Promise<boolean>
  saveAs: (key: string, name: string) => Promise<boolean>
  minimize: (key: string) => void
  close: (key: string) => void
  dismissError: (key: string) => void
}

export interface DisplayWindowProps {
  win: WinState
  pageNo: number
  top: boolean
  slots: (Row | null)[]
  stale: string
  userId: number | null
  actions: WinActions
  getIndex: () => IndexEntry[]
  getDesk: () => { w: number; h: number }
}

export const EMPTY_PARAMS: (number | null)[] = Object.freeze(
  Array.from({ length: 20 }, () => null),
) as (number | null)[]

type Mode = 'move' | 'r' | 'b' | 'rb'

const clamp = (v: number, lo: number, hi: number) => Math.min(Math.max(v, lo), Math.max(lo, hi))

function DisplayWindowImpl({ win, pageNo, top, slots, stale, userId, actions, getIndex, getDesk }: DisplayWindowProps) {
  const { key } = win
  const [geo, setGeo] = useState<Geo>({ x: win.x, y: win.y, w: win.w, h: win.h })
  const drag = useRef<{ mode: Mode; px: number; py: number; g: Geo; last: Geo } | null>(null)
  const [editing, setEditing] = useState(false)
  const [pick, setPick] = useState<number | null>(null)
  const [saveAs, setSaveAs] = useState<string | null>(null)

  // Geometry lives here while dragging, so other windows never re-render;
  // the desk hears about it once, on pointerup.
  useEffect(() => {
    if (!drag.current) setGeo({ x: win.x, y: win.y, w: win.w, h: win.h })
  }, [win.x, win.y, win.w, win.h])

  const start = (mode: Mode) => (e: React.PointerEvent<HTMLElement>) => {
    if (e.button !== 0) return
    if (mode === 'move' && (e.target as HTMLElement).closest('button, input, select')) return
    e.preventDefault()
    e.currentTarget.setPointerCapture(e.pointerId)
    drag.current = { mode, px: e.clientX, py: e.clientY, g: geo, last: geo }
    actions.focus(key)
  }
  const move = (e: React.PointerEvent<HTMLElement>) => {
    const d = drag.current
    if (!d) return
    const desk = getDesk()
    const dx = e.clientX - d.px
    const dy = e.clientY - d.py
    const g = d.g
    let next: Geo
    if (d.mode === 'move') {
      next = { ...g, x: clamp(g.x + dx, 0, desk.w - g.w), y: clamp(g.y + dy, 0, desk.h - g.h) }
    } else {
      next = {
        ...g,
        w: d.mode === 'b' ? g.w : clamp(g.w + dx, MIN_W, desk.w - g.x),
        h: d.mode === 'r' ? g.h : clamp(g.h + dy, MIN_H, desk.h - g.y),
      }
    }
    d.last = next
    setGeo(next)
  }
  const end = (e: React.PointerEvent<HTMLElement>) => {
    const d = drag.current
    if (!d) return
    drag.current = null
    if (e.currentTarget.hasPointerCapture(e.pointerId)) e.currentTarget.releasePointerCapture(e.pointerId)
    const g = d.last
    if (g.x !== d.g.x || g.y !== d.g.y || g.w !== d.g.w || g.h !== d.g.h) actions.geometry(key, g)
  }
  const handle = (mode: Mode) => ({
    onPointerDown: start(mode),
    onPointerMove: move,
    onPointerUp: end,
    onPointerCancel: end,
  })

  const params = win.draft?.params ?? win.page?.params ?? EMPTY_PARAMS
  const name = win.draft?.name ?? win.page?.name ?? 'Untitled page'
  const dirty = win.draft != null
  const isNew = win.pageId == null
  const owner = win.page?.ownerName ?? null
  const mine = userId != null && (isNew || win.page?.ownerId === userId)
  const saveHint =
    userId == null
      ? 'Pick your name in the toolbar to save pages'
      : isNew
        ? 'Save this new page under your name'
        : !win.page
          ? 'The page has not loaded'
          : mine
            ? dirty ? 'Save changes to this page' : 'No unsaved changes'
            : `Only ${owner ?? 'its owner'} can overwrite this page. Use Save as to keep a copy under your name.`

  const onSlot = useCallback((i: number) => setPick(i), [])

  const doSave = () => {
    if (isNew) setSaveAs(name === 'Untitled page' ? '' : name)
    else void actions.save(key)
  }
  const submitSaveAs = async (e: React.FormEvent) => {
    e.preventDefault()
    const n = (saveAs ?? '').trim()
    if (!n) return
    if (await actions.saveAs(key, n)) setSaveAs(null)
  }

  const onKey = (e: React.KeyboardEvent) => {
    if (e.key !== 'Escape') return
    if (pick != null) setPick(null)
    else if (saveAs != null) setSaveAs(null)
    else if (editing) setEditing(false)
    else return
    e.stopPropagation()
  }

  const dupCount = (() => {
    const seen = new Set<number>()
    let n = 0
    for (const p of params) {
      if (p == null) continue
      if (seen.has(p)) n++
      seen.add(p)
    }
    return n
  })()

  const fontSize = Math.round(clamp(geo.w / 64, 12, 14.5) * 2) / 2

  return (
    <section
      className={`dp-win ${top ? 'top' : ''} ${editing ? 'editing' : ''} ${geo.w < 760 ? 'narrow' : ''}`}
      style={{ left: geo.x, top: geo.y, width: geo.w, height: geo.h, zIndex: win.z }}
      tabIndex={0}
      aria-label={`Display page ${pageNo}: ${name}`}
      onPointerDownCapture={() => actions.focus(key)}
      onFocus={() => actions.focus(key)}
      onKeyDown={onKey}
    >
      <header className="dp-title" {...handle('move')}>
        <Icon name="display" size={15} />
        <div className="dp-title-text">
          <b title={name}>{name}{dirty && <span className="dp-dirty" title="Unsaved changes"> •</span>}</b>
          <span className="dp-owner">
            {isNew ? 'not saved yet' : owner ? `by ${owner}` : win.loading ? 'loading…' : ''}
          </span>
          <span className="dp-pno">page {pageNo}</span>
        </div>
        <div className="dp-title-btns">
          <button
            type="button"
            className={editing ? 'on' : ''}
            aria-pressed={editing}
            onClick={() => {
              setEditing((v) => !v)
              setPick(null)
            }}
            title="Choose the parameters in the 20 slots"
          >
            <Icon name="edit" size={14} />
            <span className="lbl">{editing ? 'Done' : 'Edit parameters'}</span>
          </button>
          <button
            type="button"
            onClick={doSave}
            disabled={!mine || win.busy || (!isNew && !dirty) || (!isNew && !win.page)}
            title={saveHint}
          >
            <Icon name="save" size={14} />
            <span className="lbl">Save</span>
          </button>
          <button
            type="button"
            onClick={() => setSaveAs(isNew || mine ? name : `${name} (copy)`)}
            disabled={userId == null || win.busy}
            title={userId == null ? 'Pick your name in the toolbar to save pages' : 'Save a copy under your name'}
          >
            <Icon name="copy" size={14} />
            <span className="lbl">Save as…</span>
          </button>
          <button type="button" className="icon" onClick={() => actions.minimize(key)} title="Minimize to the dock" aria-label="Minimize">
            <Icon name="minus" size={14} />
          </button>
          <button
            type="button"
            className="icon"
            onClick={() => {
              if (dirty && !confirm(`Close "${name}" and discard its unsaved changes?`)) return
              actions.close(key)
            }}
            title="Close"
            aria-label="Close"
          >
            <Icon name="x" size={14} />
          </button>
        </div>
      </header>

      {saveAs != null && (
        <form className="dp-bar" onSubmit={submitSaveAs}>
          <label>
            Save as
            <input
              type="text"
              autoFocus
              maxLength={60}
              value={saveAs}
              placeholder="Page name"
              onChange={(e) => setSaveAs(e.target.value)}
            />
          </label>
          <button type="submit" className="primary" disabled={!saveAs.trim() || win.busy}>
            {win.busy ? 'Saving…' : 'Save'}
          </button>
          <button type="button" onClick={() => setSaveAs(null)}>Cancel</button>
          <span className="dp-bar-hint">A new page owned by you.</span>
        </form>
      )}

      {editing && (
        <div className="dp-bar">
          <label>
            Name
            <input
              type="text"
              maxLength={60}
              value={name}
              onChange={(e) => actions.rename(key, e.target.value)}
            />
          </label>
          <span className="dp-bar-hint">Click a slot to choose its parameter.</span>
          {dupCount > 0 && (
            <span className="dp-warn" role="status">
              {dupCount === 1 ? 'One parameter appears twice' : `${dupCount} duplicate slots`}
            </span>
          )}
        </div>
      )}

      {win.error && (
        <div className="dp-error" role="alert">
          <span>{win.error}</span>
          <button type="button" className="icon" onClick={() => actions.dismissError(key)} aria-label="Dismiss">
            <Icon name="x" size={12} />
          </button>
        </div>
      )}

      <div className="dp-body" style={{ fontSize }}>
        {win.loading && <div className="dp-loading">Loading page…</div>}
        <HeaderBlock pageNo={pageNo} />
        <SpecTable
          params={params}
          slots={slots}
          stale={stale}
          editing={editing}
          activeSlot={pick}
          onSlot={onSlot}
        />
      </div>

      {pick != null && (
        <ParamPicker
          slot={pick}
          params={params}
          getIndex={getIndex}
          onPick={(id) => {
            actions.setParam(key, pick, id)
            setPick(null)
          }}
          onClose={() => setPick(null)}
        />
      )}

      <div className="dp-rs r" {...handle('r')} aria-hidden="true" />
      <div className="dp-rs b" {...handle('b')} aria-hidden="true" />
      <div className="dp-rs rb" {...handle('rb')} aria-hidden="true" />
    </section>
  )
}

/**
 * Re-renders only when this window's own state changes or one of its 20 rows
 * is replaced; the rest of the telemetry table updating does not touch it.
 */
export const DisplayWindow = memo(DisplayWindowImpl, (a, b) => {
  if (
    a.win !== b.win || a.pageNo !== b.pageNo || a.top !== b.top || a.stale !== b.stale ||
    a.userId !== b.userId || a.actions !== b.actions || a.getIndex !== b.getIndex ||
    a.getDesk !== b.getDesk || a.slots.length !== b.slots.length
  ) return false
  for (let i = 0; i < a.slots.length; i++) if (a.slots[i] !== b.slots[i]) return false
  return true
})
