import { useEffect, useMemo, useState } from "react";
import displayApi from "../../../api/displayApi.js";
import { errorMessage } from "../../../api/apiError.js";
import { Icon } from "../../../components/common/index.js";

function when(iso) {
  const t = Date.parse(iso);
  return Number.isFinite(t) ? new Date(t).toLocaleString() : iso;
}

/**
 * Every saved page, anyone's: filter by owner, search by name, click to open.
 * Deleting needs DISPLAY_EDIT, and then either ownership of the page or
 * DISPLAY_DELETE_ANY. `me` is `{ id, canEdit, canDeleteAny }`.
 */
export default function Library({ me, onOpen, onClose }) {
  const canDelete = (p) => me.canEdit && (p.ownerId === me.id || me.canDeleteAny);
  const [pages, setPages] = useState(null);
  const [error, setError] = useState(null);
  const [owner, setOwner] = useState("");
  const [q, setQ] = useState("");
  /** Bumped by Retry, to list the pages again. */
  const [attempt, setAttempt] = useState(0);

  useEffect(() => {
    let dead = false;
    displayApi
      .pages()
      .then((p) => !dead && setPages(Array.isArray(p) ? p : []))
      .catch((e) => !dead && setError(errorMessage(e, "Could not list the saved pages.")));
    return () => {
      dead = true;
    };
  }, [attempt]);
  const load = () => {
    setError(null);
    setAttempt((n) => n + 1);
  };

  useEffect(() => {
    const on = (e) => e.key === "Escape" && onClose();
    window.addEventListener("keydown", on);
    return () => window.removeEventListener("keydown", on);
  }, [onClose]);

  // Owners are whoever has pages, you first.
  const owners = useMemo(() => {
    const m = new Map((pages ?? []).map((p) => [p.ownerId, p.ownerName]));
    return [...m.entries()].sort(([a, an], [b, bn]) => (a === me.id ? -1 : b === me.id ? 1 : an.localeCompare(bn)));
  }, [pages, me.id]);

  const shown = useMemo(() => {
    if (!pages) return [];
    const t = q.trim().toLowerCase();
    return pages.filter((p) => (!owner || String(p.ownerId) === owner) && (!t || p.name.toLowerCase().includes(t)));
  }, [pages, owner, q]);

  const remove = async (p) => {
    if (!window.confirm(`Delete the page "${p.name}" for everyone? This cannot be undone.`)) return;
    try {
      await displayApi.deletePage(p.id);
      setPages((ps) => ps?.filter((x) => x.id !== p.id) ?? ps);
    } catch (e) {
      setError(errorMessage(e, "The page was not deleted."));
    }
  };

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
            {owners.map(([id, name]) => (
              <option key={id} value={id}>
                {name}
                {id === me.id ? " (you)" : ""}
              </option>
            ))}
          </select>
          <input type="search" autoFocus placeholder="Search page names…" value={q} onChange={(e) => setQ(e.target.value)} />
          <span className="count">{pages ? `${shown.length} of ${pages.length}` : ""}</span>
        </div>
        {error && (
          <div className="dp-error" role="alert">
            <span>{error}</span>
            <button type="button" onClick={load}>
              Retry
            </button>
          </div>
        )}
        <div className="dp-modal-list">
          {!pages && !error && <div className="empty">Loading…</div>}
          {pages && shown.length === 0 && (
            <div className="empty">{pages.length ? "No page matches." : "No pages saved yet. Make one with New page."}</div>
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
                  <tr
                    key={p.id}
                    onClick={() => onOpen(p)}
                    tabIndex={0}
                    onKeyDown={(e) => e.key === "Enter" && onOpen(p)}
                    title="Open in a new window"
                  >
                    <td className="nm">{p.name}</td>
                    <td>
                      {p.ownerName}
                      {p.ownerId === me.id ? " (you)" : ""}
                    </td>
                    <td className="n">{(p.params ?? []).filter((x) => x != null).length}/20</td>
                    <td className="dim">{when(p.updatedAt)}</td>
                    <td className="act">
                      {canDelete(p) && (
                        <button
                          type="button"
                          className="icon"
                          title="Delete this page"
                          aria-label={`Delete ${p.name}`}
                          onClick={(e) => {
                            e.stopPropagation();
                            void remove(p);
                          }}
                        >
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
  );
}
