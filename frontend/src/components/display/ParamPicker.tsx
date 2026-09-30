import { useEffect, useMemo, useRef, useState } from 'react'
import type { Row } from '../../types'

export interface IndexEntry {
  row: Row
  /** Lower-case id, name, subsystem, message and entry, built once per table. */
  s: string
}

/** Sorted by subsystem then id, so search results come out grouped. */
export function buildIndex(rows: Row[]): IndexEntry[] {
  return rows
    .map((row) => ({
      row,
      s: `${row.parameter_id} ${row.name} ${row.subsystem} ${row.message} ${row.entry}`.toLowerCase(),
    }))
    .sort((a, b) => a.row.subsystem.localeCompare(b.row.subsystem) || a.row.parameter_id - b.row.parameter_id)
}

const MAX = 200

export function ParamPicker({
  slot,
  params,
  getIndex,
  onPick,
  onClose,
}: {
  slot: number
  params: (number | null)[]
  getIndex: () => IndexEntry[]
  onPick: (id: number | null) => void
  onClose: () => void
}) {
  const [q, setQ] = useState('')
  const [active, setActive] = useState(0)
  const index = useMemo(getIndex, [getIndex])
  const listRef = useRef<HTMLDivElement>(null)
  const current = params[slot]

  const { hits, total } = useMemo(() => {
    const terms = q.toLowerCase().split(/\s+/).filter(Boolean)
    const out: IndexEntry[] = []
    let n = 0
    for (const e of index) {
      if (terms.every((t) => e.s.includes(t))) {
        n++
        if (out.length < MAX) out.push(e)
      }
    }
    return { hits: out, total: n }
  }, [q, index])

  useEffect(() => setActive(0), [q])
  useEffect(() => {
    listRef.current?.querySelector<HTMLElement>(`[data-i="${active}"]`)?.scrollIntoView({ block: 'nearest' })
  }, [active])

  const where = (id: number) => {
    const at = params.findIndex((p, i) => p === id && i !== slot)
    return at < 0 ? null : at + 1
  }

  const onKey = (e: React.KeyboardEvent) => {
    if (e.key === 'ArrowDown') {
      e.preventDefault()
      setActive((a) => Math.min(hits.length - 1, a + 1))
    } else if (e.key === 'ArrowUp') {
      e.preventDefault()
      setActive((a) => Math.max(0, a - 1))
    } else if (e.key === 'Enter') {
      e.preventDefault()
      const h = hits[active]
      if (h) onPick(h.row.parameter_id)
    } else if (e.key === 'Escape') {
      e.preventDefault()
      e.stopPropagation()
      onClose()
    }
  }

  let lastSub = ''
  return (
    <div className="dp-picker" role="dialog" aria-label={`Choose the parameter for slot ${slot + 1}`} onKeyDown={onKey}>
      <div className="dp-picker-top">
        <b>Slot {slot + 1}</b>
        <input
          type="search"
          autoFocus
          placeholder="Search id, name, subsystem, message…"
          value={q}
          onChange={(e) => setQ(e.target.value)}
          aria-controls="dp-picker-list"
        />
        <button type="button" onClick={() => onPick(null)} disabled={current == null}>
          Clear slot
        </button>
        <button type="button" onClick={onClose} aria-label="Close">
          Esc
        </button>
      </div>
      <div className="dp-picker-count">
        {index.length === 0
          ? 'No parameter table loaded yet (is the backend running?)'
          : `${total.toLocaleString()} match${total === 1 ? '' : 'es'}${total > MAX ? ` · showing the first ${MAX}` : ''} · ↑↓ Enter`}
      </div>
      <div className="dp-picker-list" id="dp-picker-list" ref={listRef} role="listbox">
        {hits.map((e, i) => {
          const r = e.row
          const head = r.subsystem !== lastSub ? r.subsystem : null
          lastSub = r.subsystem
          const dupAt = where(r.parameter_id)
          return (
            <div key={r.parameter_id}>
              {head && <div className="dp-picker-sub">{head}</div>}
              <div
                data-i={i}
                role="option"
                aria-selected={i === active}
                className={`dp-picker-item ${i === active ? 'on' : ''} ${r.parameter_id === current ? 'cur' : ''}`}
                onMouseEnter={() => setActive(i)}
                onClick={() => onPick(r.parameter_id)}
              >
                <span className="pid">{r.parameter_id}</span>
                <span className="pname">{r.name}</span>
                <span className="pmsg">{r.message}</span>
                {dupAt != null && <span className="pdup">already in slot {dupAt}</span>}
                <span className={`tier ${r.tier}`}>{r.tier}</span>
              </div>
            </div>
          )
        })}
      </div>
    </div>
  )
}
