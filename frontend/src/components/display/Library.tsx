import { useEffect, useMemo, useState } from 'react'
import { Icon } from '../Icon'
import { api, type Page, type User } from './api'

function when(iso: string): string {
  const t = Date.parse(iso)
  return Number.isFinite(t) ? new Date(t).toLocaleString() : iso
}

/** Every saved page, anyone's: filter by owner, search by name, click to open. */
export function Library({
  users,
  userId,
  onOpen,
  onClose,
  onChanged,
}: {
  users: User[]
  userId: number | null
  onOpen: (p: Page) => void
  onClose: () => void
  onChanged: () => void
}) {
  const [pages, setPages] = useState<Page[] | null>(null)
  const [error, setError] = useState<string | null>(null)
  const [owner, setOwner] = useState<string>('')
  const [q, setQ] = useState('')

  const load = () => {
    setError(null)
    api
      .pages()
      .then((p) => setPages(Array.isArray(p) ? p : []))
      .catch((e: Error) => setError(e.message))
  }
  useEffect(load, [])

  useEffect(() => {
    const on = (e: KeyboardEvent) => {
      if (e.key === 'Escape') onClose()
    }
    window.addEventListener('keydown', on)
    return () => window.removeEventListener('keydown', on)
  }, [onClose])

  const shown = useMemo(() => {
    if (!pages) return []
    const t = q.trim().toLowerCase()
    return pages.filter(
      (p) => (!owner || String(p.ownerId) === owner) && (!t || p.name.toLowerCase().includes(t)),
    )
  }, [pages, owner, q])

  const remove = async (p: Page) => {
    if (userId == null || !confirm(`Delete the page "${p.name}" for everyone? This cannot be undone.`)) return
    try {
      await api.deletePage(userId, p.id)
      setPages((ps) => (ps ? ps.filter((x) => x.id !== p.id) : ps))
      onChanged()
    } catch (e) {
      setError((e as Error).message)
    }
  }

  return (
    <div className="dp-modal-back" onPointerDown={(e) => e.target === e.currentTarget && onClose()}>
      <div className="dp-modal" role="dialog" aria-modal="true" aria-label="Saved display pages">
        <div className="dp-modal-head">
          <Icon name="folder" size={16} />
          <b>Saved pages</b>
          <span className="dp-modal-sub">Anyone can open any page; only its owner can overwrite it.</span>
          <div className="grow" />
          <button type="button" className="icon" onClick={onClose} aria-label="Close">
            <Icon name="x" size={14} />
          </button>
        </div>
        <div className="dp-modal-filters">
          <select value={owner} onChange={(e) => setOwner(e.target.value)} aria-label="Owner">
            <option value="">All owners</option>
            {users.map((u) => (
              <option key={u.id} value={u.id}>
                {u.name}
                {u.id === userId ? ' (you)' : ''}
              </option>
            ))}
          </select>
          <input
            type="search"
            autoFocus
            placeholder="Search page names…"
            value={q}
            onChange={(e) => setQ(e.target.value)}
          />
          <span className="count">{pages ? `${shown.length} of ${pages.length}` : ''}</span>
        </div>
        {error && (
          <div className="dp-error" role="alert">
            <span>{error}</span>
            <button type="button" onClick={load}>Retry</button>
          </div>
        )}
        <div className="dp-modal-list">
          {!pages && !error && <div className="empty">Loading…</div>}
          {pages && shown.length === 0 && (
            <div className="empty">{pages.length ? 'No page matches.' : 'No pages saved yet. Make one with New page.'}</div>
          )}
          {shown.length > 0 && (
            <table className="dp-libtable">
              <thead>
                <tr>
                  <th>Page</th>
                  <th>Owner</th>
                  <th className="n">Slots</th>
                  <th>Updated</th>
                  <th />
                </tr>
              </thead>
              <tbody>
                {shown.map((p) => (
                  <tr key={p.id} onClick={() => onOpen(p)} tabIndex={0}
                    onKeyDown={(e) => e.key === 'Enter' && onOpen(p)} title="Open in a new window">
                    <td className="nm">{p.name}</td>
                    <td>{p.ownerName}{p.ownerId === userId ? ' (you)' : ''}</td>
                    <td className="n">{(p.params ?? []).filter((x) => x != null).length}/20</td>
                    <td className="dim">{when(p.updatedAt)}</td>
                    <td className="act">
                      {p.ownerId === userId && (
                        <button type="button" className="icon" title="Delete this page"
                          onClick={(e) => {
                            e.stopPropagation()
                            void remove(p)
                          }}>
                          <Icon name="trash" size={13} />
                        </button>
                      )}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </div>
      </div>
    </div>
  )
}
