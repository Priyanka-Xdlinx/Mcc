import { useCallback, useEffect, useLayoutEffect, useMemo, useRef, useState } from "react";
import { useSelector } from "react-redux";
import displayApi from "../../../api/displayApi.js";
import { errorMessage } from "../../../api/apiError.js";
import { Icon } from "../../../components/common/index.js";
import useNow from "../../../hooks/useNow.js";
import { selectFocusMode } from "../../../redux/app/index.js";
import { selectLastMsgAt, selectLive, selectRows } from "../../../redux/telemetry/index.js";
import { ago, PERIOD_S } from "../../../utils/format.js";
import useWorkspace from "../hooks/useWorkspace.js";
import {
  arrangeGeometry,
  belowAll,
  clamp,
  clampAll,
  DEFAULT_H,
  emptyParams,
  MIN_H,
  MIN_W,
  nameOf,
  newKey,
  nextPageNo,
  paramsOf,
  raise,
  topZ,
  UNTITLED,
} from "../utils/layout.js";
import DisplayWindow from "./DisplayWindow.jsx";
import Library from "./Library.jsx";

/** A value with no update for this many tier periods is shown as stale. */
const STALE_PERIODS = 3;

/** When a row's current reading arrived: this browser's clock if it came over the socket, else the backend's. */
const readingAt = (r) => r.seen_at ?? (r.received_at ? Date.parse(r.received_at) : 0);

/** "0110…": which of a window's 20 slots hold a value that has stopped updating. */
function staleMask(slots, now) {
  let s = "";
  for (const r of slots) {
    s += r && r.value != null && now - readingAt(r) > STALE_PERIODS * 1000 * (PERIOD_S[r.tier] ?? 8) ? "1" : "0";
  }
  return s;
}

/**
 * The signed-in user's desk: the toolbar and the floating windows. `me` is
 * `{ id, canEdit, canDeleteAny }`; without `canEdit` the desk is view only.
 */
export default function DisplayDesk({ me }) {
  const rows = useSelector(selectRows);
  const live = useSelector(selectLive);
  const lastMsgAt = useSelector(selectLastMsgAt);
  const focused = useSelector(selectFocusMode);
  const now = useNow(1000);

  const deskRef = useRef(null);
  const getDesk = useCallback(() => {
    const el = deskRef.current;
    return { w: el?.clientWidth ?? 1200, h: el?.clientHeight ?? 700, top: el?.scrollTop ?? 0 };
  }, []);
  const getDeskWidth = useCallback(() => getDesk().w, [getDesk]);

  const { windows, setWindows, screens, activeId, addScreen, switchScreen, renameScreen, save, ready, retry } =
    useWorkspace(getDeskWidth, me.canEdit);
  // Read by the save action, which runs after a request and must see the windows as they are then.
  const windowsRef = useRef(windows);
  useLayoutEffect(() => {
    windowsRef.current = windows;
  });

  const [libOpen, setLibOpen] = useState(false);
  const [arrangeOpen, setArrangeOpen] = useState(false);
  const arrangeRef = useRef(null);
  const [screenOpen, setScreenOpen] = useState(false);
  const screenRef = useRef(null);
  /** The id of the screen whose name is being edited in the dropdown, if any. */
  const [renaming, setRenaming] = useState(null);
  const [renameText, setRenameText] = useState("");
  // So a listener added once (outside click) always commits the latest text,
  // never a stale one from when it was attached.
  const renameRef = useRef({ id: null, text: "" });
  useLayoutEffect(() => {
    renameRef.current = { id: renaming, text: renameText };
  });
  const [scrollTo, setScrollTo] = useState(null);

  const rowMap = useMemo(() => new Map(rows.map((r) => [r.parameter_id, r])), [rows]);

  // ── window actions: one stable object, so windows stay memoised
  const actions = useMemo(() => {
    const patch = (key, p) => setWindows((ws) => ws.map((w) => (w.key === key ? { ...w, ...p } : w)));
    const withDraft = (key, f) =>
      setWindows((ws) => ws.map((w) => (w.key === key ? { ...w, draft: f({ name: nameOf(w), params: paramsOf(w).slice() }) } : w)));
    const store = async (key, asName) => {
      const w = windowsRef.current.find((x) => x.key === key);
      if (!w) return false;
      const sentDraft = w.draft;
      const draft = { name: (asName ?? nameOf(w)).trim(), params: paramsOf(w).slice() };
      if (!draft.name) {
        patch(key, { error: "Give the page a name first." });
        return false;
      }
      patch(key, { busy: true, error: null });
      try {
        const p =
          asName != null || w.pageId == null
            ? await displayApi.createPage(draft)
            : await displayApi.updatePage(w.pageId, draft);
        // Keep edits made while the request was in flight.
        setWindows((ws) =>
          ws.map((x) => (x.key !== key ? x : { ...x, busy: false, page: p, pageId: p.id, draft: x.draft !== sentDraft ? x.draft : null })),
        );
        return true;
      } catch (e) {
        patch(key, { busy: false, error: errorMessage(e, "The page was not saved.") });
        return false;
      }
    };
    return {
      focus: (key) => setWindows((ws) => raise(ws, key)),
      geometry: (key, g) => patch(key, { x: g.x, y: g.y, w: g.w, h: g.h }),
      setParam: (key, slot, id) =>
        withDraft(key, (d) => {
          d.params[slot] = id;
          return d;
        }),
      rename: (key, name) => withDraft(key, (d) => ({ ...d, name })),
      save: (key) => store(key, null),
      saveAs: (key, name) => store(key, name),
      close: (key) => setWindows((ws) => ws.filter((w) => w.key !== key)),
      dismissError: (key) => patch(key, { error: null }),
    };
  }, [setWindows]);

  // Bring a newly opened window into view once it is on the desk.
  useEffect(() => {
    if (scrollTo == null) return;
    deskRef.current?.scrollTo({ top: Math.max(0, scrollTo - 8), behavior: "smooth" });
  }, [scrollTo]);

  /** A new window below the lowest open one, so it covers nothing. */
  const addWindow = (fields) => {
    const desk = getDesk();
    const ws = windowsRef.current;
    const y = belowAll(ws);
    const w = fields.w ?? Math.max(MIN_W, Math.min(880, desk.w - 48));
    const win = {
      key: newKey(),
      pageNo: nextPageNo(ws),
      x: clamp(16, 0, desk.w - w),
      y,
      w,
      h: Math.max(MIN_H, Math.min(DEFAULT_H, desk.h - 40)),
      z: topZ(ws) + 1,
      error: null,
      busy: false,
      loading: false,
      ...fields,
    };
    setWindows((cur) => [...cur, { ...win, pageNo: nextPageNo(cur), z: topZ(cur) + 1 }]);
    setScrollTo(y);
  };

  const openWindow = (page) => {
    const open = page && windowsRef.current.find((w) => w.pageId === page.id);
    if (open) {
      setWindows((ws) => ws.map((w) => (w.key === open.key ? { ...w, page, z: topZ(ws) + 1 } : w)));
      return;
    }
    addWindow({ pageId: page?.id ?? null, page, draft: page ? null : { name: UNTITLED, params: emptyParams() } });
  };

  /** Copies the front window into a new unsaved page; editing it leaves the original alone. */
  const copyWindow = () => {
    let src = null;
    for (const w of windowsRef.current) if (!src || w.z > src.z) src = w;
    if (!src) return;
    addWindow({
      pageId: null,
      page: null,
      draft: { name: `${nameOf(src)} (copy)`, params: paramsOf(src).slice() },
      x: src.x,
      w: src.w,
      h: src.h,
    });
  };

  const arrange = (how) => {
    setArrangeOpen(false);
    const desk = getDesk();
    setWindows((ws) => {
      const geo = arrangeGeometry(ws, how, desk);
      return geo.size ? ws.map((w) => (geo.has(w.key) ? { ...w, ...geo.get(w.key) } : w)) : ws;
    });
  };

  // Keep windows reachable when the browser window shrinks.
  useEffect(() => {
    const on = () => setWindows((ws) => clampAll(ws, getDesk().w));
    window.addEventListener("resize", on);
    return () => window.removeEventListener("resize", on);
  }, [getDesk, setWindows]);

  /** Reads the ref, not the render's own state, so it is safe to call from a stale closure. */
  const commitRename = useCallback(() => {
    const { id, text } = renameRef.current;
    const n = text.trim();
    if (id != null && n) renameScreen(id, n);
    setRenaming(null);
  }, [renameScreen]);

  // Esc and outside clicks close the Arrange menu.
  useEffect(() => {
    if (!arrangeOpen) return undefined;
    const key = (e) => e.key === "Escape" && setArrangeOpen(false);
    const down = (e) => !arrangeRef.current?.contains(e.target) && setArrangeOpen(false);
    window.addEventListener("keydown", key);
    window.addEventListener("pointerdown", down);
    return () => {
      window.removeEventListener("keydown", key);
      window.removeEventListener("pointerdown", down);
    };
  }, [arrangeOpen]);

  // Esc and outside clicks close the Screen menu. Esc cancels a rename in progress
  // without saving it; an outside click (or anything else that blurs the field) saves it first.
  useEffect(() => {
    if (!screenOpen) return undefined;
    const key = (e) => {
      if (e.key !== "Escape") return;
      if (renameRef.current.id != null) setRenaming(null);
      else setScreenOpen(false);
    };
    const down = (e) => {
      if (!screenRef.current?.contains(e.target)) {
        if (renameRef.current.id != null) commitRename();
        setScreenOpen(false);
      }
    };
    window.addEventListener("keydown", key);
    window.addEventListener("pointerdown", down);
    return () => {
      window.removeEventListener("keydown", key);
      window.removeEventListener("pointerdown", down);
    };
  }, [screenOpen, commitRename]);

  /** A whole separate, empty canvas of windows; the current screen is left exactly as it is. */
  const pickScreen = (id) => {
    if (renameRef.current.id != null) commitRename();
    switchScreen(id);
    setScreenOpen(false);
    deskRef.current?.scrollTo({ top: 0 });
  };

  const startRename = (s) => {
    if (renameRef.current.id != null) commitRename();
    setRenaming(s.id);
    setRenameText(s.name);
  };

  let topKey = null;
  let frontZ = -Infinity;
  for (const w of windows) {
    if (w.z > frontZ) {
      frontZ = w.z;
      topKey = w.key;
    }
  }
  const hasWindows = windows.length > 0;
  const saveText = { loading: "Loading your layout…", pending: "Saving…", saved: "Layout saved", readonly: "View only" }[save.kind] ?? save.msg;

  return (
    <div className={`dp-root feature${focused ? " focused" : ""}`}>
      <div className="dp-toolbar" role="toolbar" aria-label="Display pages" hidden={focused}>
        <button type="button" onClick={() => openWindow(null)} disabled={!ready}>
          <Icon name="plus" size={14} /> New page
        </button>
        <button
          type="button"
          onClick={copyWindow}
          disabled={!ready || !hasWindows}
          title="Copy the front page into a new page you can edit without changing the original"
        >
          <Icon name="copy" size={14} /> Copy page
        </button>
        <button type="button" onClick={() => setLibOpen(true)} disabled={!ready}>
          <Icon name="folder" size={14} /> Open saved…
        </button>
        <div className="dp-menuwrap" ref={arrangeRef}>
          <button type="button" aria-haspopup="menu" aria-expanded={arrangeOpen} onClick={() => setArrangeOpen((v) => !v)} disabled={!hasWindows}>
            <Icon name="grid" size={14} /> Arrange
          </button>
          {arrangeOpen && (
            <div className="dp-menu" role="menu">
              <button type="button" role="menuitem" autoFocus onClick={() => arrange("cascade")}>
                Cascade
              </button>
              <button type="button" role="menuitem" onClick={() => arrange("tile")}>
                Tile
              </button>
            </div>
          )}
        </div>
        <div className="dp-menuwrap" ref={screenRef}>
          <button
            type="button"
            aria-haspopup="menu"
            aria-expanded={screenOpen}
            onClick={() => setScreenOpen((v) => !v)}
            disabled={!ready}
            title="A screen is its own empty canvas of windows; switching never closes another screen's"
          >
            <Icon name="display" size={14} /> {screens.find((s) => s.id === activeId)?.name ?? "Screen"}
            <Icon name="down" size={12} className={`dp-chev ${screenOpen ? "open" : ""}`} />
          </button>
          {screenOpen && (
            <div className="dp-menu" role="menu">
              {screens.map((s) =>
                renaming === s.id ? (
                  <form
                    key={s.id}
                    className="dp-menu-row"
                    onSubmit={(e) => {
                      e.preventDefault();
                      commitRename();
                    }}
                  >
                    <input
                      type="text"
                      autoFocus
                      maxLength={40}
                      value={renameText}
                      onChange={(e) => setRenameText(e.target.value)}
                      onBlur={commitRename}
                      onClick={(e) => e.stopPropagation()}
                    />
                  </form>
                ) : (
                  <div className="dp-menu-row" key={s.id}>
                    <button
                      type="button"
                      role="menuitemradio"
                      aria-checked={s.id === activeId}
                      className={s.id === activeId ? "on" : ""}
                      onClick={() => pickScreen(s.id)}
                    >
                      {s.name}
                      {s.windows.length > 0 && <span className="dp-menu-count">{s.windows.length}</span>}
                    </button>
                    <button
                      type="button"
                      className="icon"
                      title={`Rename "${s.name}"`}
                      aria-label={`Rename "${s.name}"`}
                      onClick={(e) => {
                        e.stopPropagation();
                        startRename(s);
                      }}
                    >
                      <Icon name="edit" size={12} />
                    </button>
                  </div>
                ),
              )}
              <div className="dp-menu-sep" />
              <button
                type="button"
                role="menuitem"
                onClick={() => {
                  if (renameRef.current.id != null) commitRename();
                  addScreen();
                  setScreenOpen(false);
                  deskRef.current?.scrollTo({ top: 0 });
                }}
              >
                <Icon name="plus" size={13} /> New screen
              </button>
            </div>
          )}
        </div>
        <div className="grow" />
        <span className="dp-feed" title={live ? "Telemetry stream connected" : "Telemetry stream offline"}>
          <span className={`dot ${live ? "on" : "off"}`} />
          {live ? `last frame ${ago(lastMsgAt, now)}` : "stream offline"}
        </span>
        <span className={`dp-savestate ${save.kind}`} role="status" title={save.kind === "error" ? save.msg : undefined}>
          {saveText}
          {save.kind === "error" && (
            <button type="button" className="link" onClick={retry}>
              retry
            </button>
          )}
        </span>
      </div>

      <div className="dp-desk" ref={deskRef}>
        {hasWindows && (
          <div
            className="dp-spacer"
            aria-hidden="true"
            style={{ top: windows.reduce((m, w) => Math.max(m, w.y + w.h), 0) + 120 }}
          />
        )}
        {windows.length === 0 && (
          <div className="dp-hint">
            <Icon name="display" size={28} />
            {save.kind === "loading" ? (
              <p>Loading your layout…</p>
            ) : (
              <p>
                No display pages open. <b>New page</b> starts an empty one; <b>Open saved…</b> lists everyone&apos;s
                pages. Your window layout is kept on the server under your account.
              </p>
            )}
          </div>
        )}
        {windows.map((w) => {
          const slots = paramsOf(w).map((id) => (id == null ? null : rowMap.get(id) ?? null));
          return (
            <DisplayWindow
              key={w.key}
              win={w}
              top={w.key === topKey}
              slots={slots}
              stale={staleMask(slots, now)}
              userId={me.id}
              canEdit={me.canEdit}
              actions={actions}
            />
          );
        })}
      </div>

      {libOpen && (
        <Library
          me={me}
          onOpen={(p) => {
            setLibOpen(false);
            openWindow(p);
          }}
          onClose={() => setLibOpen(false)}
        />
      )}
    </div>
  );
}
