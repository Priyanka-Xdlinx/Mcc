import { memo, useCallback, useEffect, useRef, useState } from "react";
import { Icon } from "../../../components/common/index.js";
import useWindowDrag from "../hooks/useWindowDrag.js";
import { clamp, nameOf, paramsOf, UNTITLED } from "../utils/layout.js";
import ParamPicker from "./ParamPicker.jsx";
import SpecTable, { HeaderBlock } from "./SpecTable.jsx";

/** Title bar, body padding and borders, in pixels; and each extra bar (edit, naming, error). */
const CHROME_H = 48;
const BAR_H = 40;

function duplicateCount(params) {
  const seen = new Set();
  let n = 0;
  for (const p of params) {
    if (p == null) continue;
    if (seen.has(p)) n++;
    seen.add(p);
  }
  return n;
}

/**
 * One display page on the desk: frame header, 20 slots, edit and save. `win`
 * is the window's state; `actions` is one stable object from the desk.
 */
function DisplayWindowImpl({ win, top, slots, stale, userId, canEdit, actions }) {
  const { key, pageNo } = win;
  const { geo, handle } = useWindowDrag(win, {
    onStart: () => actions.focus(key),
    onCommit: (g) => actions.geometry(key, g),
  });
  const [editing, setEditing] = useState(false);
  const [pick, setPick] = useState(null);
  /** The name bar: open while naming a brand-new page, or copying someone else's. */
  const [naming, setNaming] = useState(null);

  const params = paramsOf(win);
  const name = nameOf(win);
  const dirty = win.draft != null;
  const isNew = win.pageId == null;
  const owner = win.page?.ownerName ?? null;
  // Saving, in place or as a copy, needs DISPLAY_EDIT; "mine" decides overwrite or copy.
  const mine = canEdit && (isNew || win.page?.ownerId === userId);
  const saveHint = !canEdit
    ? "You can view pages but not save them"
    : isNew
      ? "Save this new page under your name"
      : !win.page
        ? "The page has not loaded"
        : mine
          ? dirty
            ? "Save changes to this page"
            : "No unsaved changes"
          : `Save a copy of this page under your name; ${owner ?? "its owner"}'s stays unchanged.`;

  const onSlot = useCallback((i) => setPick(i), []);

  /** Your own page: overwrite in place. New, or someone else's: name a copy first. */
  const doSave = () => {
    if (mine && !isNew) {
      void actions.save(key);
      return;
    }
    setNaming(isNew && name !== UNTITLED ? name : isNew ? "" : `${name} (copy)`);
  };
  const submitNaming = async (e) => {
    e.preventDefault();
    const n = (naming ?? "").trim();
    if (n && (await actions.saveAs(key, n))) setNaming(null);
  };

  const onKey = (e) => {
    if (e.key !== "Escape") return;
    if (pick != null) setPick(null);
    else if (naming != null) setNaming(null);
    else if (editing) setEditing(false);
    else return;
    e.stopPropagation();
  };

  const dupCount = duplicateCount(params);

  // Text follows the window's width, but never so large that the header and 10
  // rows (about 18.5em) stop fitting its height.
  const chrome = CHROME_H + (editing ? BAR_H : 0) + (naming != null ? BAR_H : 0) + (win.error ? BAR_H : 0);
  const fontSize = Math.round(clamp(Math.min(geo.w / 62, (geo.h - chrome) / 18.5), 10, 20) * 2) / 2;

  // Share the table's height between its 10 rows, so they always fill the window.
  const tableWrap = useRef(null);
  const [rowH, setRowH] = useState(null);
  useEffect(() => {
    const el = tableWrap.current;
    if (!el) return undefined;
    const measure = () => {
      const head = el.querySelector("thead")?.getBoundingClientRect().height ?? 0;
      const h = Math.floor((el.clientHeight - head) / 10);
      setRowH(h > 0 ? h : null);
    };
    const ro = new ResizeObserver(measure);
    ro.observe(el);
    const thead = el.querySelector("thead");
    if (thead) ro.observe(thead);
    return () => ro.disconnect();
  }, []);

  return (
    <section
      className={`dp-win ${top ? "top" : ""} ${editing ? "editing" : ""} ${geo.w < 760 ? "narrow" : ""}`}
      style={{ left: geo.x, top: geo.y, width: geo.w, height: geo.h, zIndex: win.z }}
      tabIndex={0}
      aria-label={`Display page ${pageNo}: ${name}`}
      onPointerDownCapture={() => actions.focus(key)}
      onFocus={() => actions.focus(key)}
      onKeyDown={onKey}
    >
      <header className="dp-title" {...handle("move")}>
        <Icon name="display" size={15} />
        <div className="dp-title-text">
          <b title={name}>
            {name}
            {dirty && (
              <span className="dp-dirty" title="Unsaved changes">
                {" "}
                •
              </span>
            )}
          </b>
          <span className="dp-owner">{isNew ? "not saved yet" : owner ? `by ${owner}` : win.loading ? "loading…" : ""}</span>
          <span className="dp-pno">page {pageNo}</span>
        </div>
      </header>

      {naming != null && (
        <form className="dp-bar" onSubmit={submitNaming}>
          <label>
            Name
            <input type="text" autoFocus maxLength={60} value={naming} placeholder="Page name" onChange={(e) => setNaming(e.target.value)} />
          </label>
          <button type="submit" className="primary" disabled={!naming.trim() || win.busy}>
            {win.busy ? "Saving…" : "Save"}
          </button>
          <button type="button" onClick={() => setNaming(null)}>
            Cancel
          </button>
          <span className="dp-bar-hint">A new page owned by you.</span>
        </form>
      )}

      {editing && (
        <div className="dp-bar">
          <label>
            Name
            <input type="text" maxLength={60} value={name} onChange={(e) => actions.rename(key, e.target.value)} />
          </label>
          <span className="dp-bar-hint">Click a slot to choose its parameter.</span>
          {dupCount > 0 && (
            <span className="dp-warn" role="status">
              {dupCount === 1 ? "One parameter appears twice" : `${dupCount} duplicate slots`}
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
        <div className="dp-head-row">
          <HeaderBlock pageNo={pageNo} />
          <div className="dp-head-actions">
            <button
              type="button"
              className={editing ? "on" : ""}
              aria-pressed={editing}
              aria-label={editing ? "Done choosing parameters" : "Choose the parameters in the 20 slots"}
              onClick={() => {
                setEditing((v) => !v);
                setPick(null);
              }}
              title={editing ? "Done" : "Choose the parameters in the 20 slots"}
            >
              <Icon name="edit" size={14} />
            </button>
            <button
              type="button"
              onClick={doSave}
              disabled={!canEdit || win.busy || (!isNew && !win.page) || (mine && !isNew && !dirty)}
              aria-label="Save"
              title={saveHint}
            >
              <Icon name={mine ? "save" : "copy"} size={14} />
            </button>
            <button
              type="button"
              onClick={() => {
                if (dirty && !window.confirm(`Close "${name}" and discard its unsaved changes?`)) return;
                actions.close(key);
              }}
              title="Close"
              aria-label="Close"
            >
              <Icon name="x" size={14} />
            </button>
          </div>
        </div>
        <div className="dp-tablewrap" ref={tableWrap} style={rowH != null ? { "--row-h": `${rowH}px` } : undefined}>
          <SpecTable params={params} slots={slots} stale={stale} editing={editing} activeSlot={pick} onSlot={onSlot} />
        </div>
      </div>

      {pick != null && (
        <ParamPicker
          slot={pick}
          params={params}
          onPick={(id) => {
            actions.setParam(key, pick, id);
            setPick(null);
          }}
          onClose={() => setPick(null)}
        />
      )}

      <div className="dp-rs r" {...handle("r")} aria-hidden="true" />
      <div className="dp-rs b" {...handle("b")} aria-hidden="true" />
      <div className="dp-rs rb" {...handle("rb")} aria-hidden="true" />
    </section>
  );
}

/**
 * Re-renders only when this window's own state changes or one of its 20 rows
 * is replaced; the rest of the telemetry table updating does not touch it.
 */
const DisplayWindow = memo(
  DisplayWindowImpl,
  (a, b) =>
    a.win === b.win &&
    a.top === b.top &&
    a.stale === b.stale &&
    a.userId === b.userId &&
    a.canEdit === b.canEdit &&
    a.actions === b.actions &&
    a.slots.length === b.slots.length &&
    a.slots.every((s, i) => s === b.slots[i]),
);

export default DisplayWindow;
