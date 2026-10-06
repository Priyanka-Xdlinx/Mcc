import { useCallback, useEffect, useState } from "react";
import displayApi from "../../../api/displayApi.js";
import { errorMessage } from "../../../api/apiError.js";
import {
  clampAll,
  emptyParams,
  newScreenId,
  parseWorkspace,
  serializeWorkspace,
  toWinStates,
  uniquePageNos,
  UNTITLED,
} from "../utils/layout.js";
import { createWorkspaceSaver } from "../utils/workspaceSaver.js";

/**
 * The signed-in user's whole workspace - one or more screens, each its own
 * independent set of windows - restored from and saved to the backend, the
 * only copy kept. The caller remounts this per user (a React `key`), so one
 * user's workspace is never saved as another's.
 *
 * `save.kind`: "loading" | "pending" | "saved" | "error" (with `msg`,
 * and `restore: true` when it was the restore that failed - the layout must
 * then not be overwritten) | "readonly" (no DISPLAY_EDIT: restored, never saved).
 */
export default function useWorkspace(getDeskWidth, canEdit) {
  const [screens, setScreens] = useState([]);
  const [activeId, setActiveId] = useState(null);
  const [save, setSave] = useState({ kind: "loading" });
  const [restored, setRestored] = useState(false);
  const [attempt, setAttempt] = useState(0);
  const [saver] = useState(() =>
    createWorkspaceSaver({
      save: (json) => displayApi.saveWorkspace(JSON.parse(json)),
      saveOnUnload: (json) => displayApi.saveWorkspaceOnUnload(JSON.parse(json)),
      onState: (kind, error) => setSave(kind === "error" ? { kind, msg: `Not saved: ${errorMessage(error)}` } : { kind }),
    }),
  );

  /** This screen's windows; setting them (array or updater) touches only this screen. */
  const windows = screens.find((s) => s.id === activeId)?.windows ?? [];
  const setWindows = useCallback(
    (update) => {
      setScreens((ss) =>
        ss.map((s) => {
          if (s.id !== activeId) return s;
          const next = typeof update === "function" ? update(s.windows) : update;
          return next === s.windows ? s : { ...s, windows: next };
        }),
      );
    },
    [activeId],
  );

  // Fetch the saved pages behind restored windows, on every screen.
  const loadPages = useCallback((ids) => {
    for (const id of new Set(ids)) {
      displayApi
        .page(id)
        .then((p) =>
          setScreens((ss) =>
            ss.map((s) => ({ ...s, windows: s.windows.map((w) => (w.pageId === id ? { ...w, page: p, loading: false } : w)) })),
          ),
        )
        .catch((e) => {
          const gone = e?.response?.status === 404;
          setScreens((ss) =>
            ss.map((s) => ({
              ...s,
              windows: s.windows.map((w) => {
                if (w.pageId !== id) return w;
                if (!gone) return { ...w, loading: false, error: `Could not load page #${id}: ${errorMessage(e)}` };
                return {
                  ...w,
                  loading: false,
                  pageId: null,
                  draft: w.draft ?? { name: UNTITLED, params: emptyParams() },
                  error: `Saved page #${id} no longer exists; this is now an unsaved copy.`,
                };
              }),
            })),
          );
        });
    }
  }, []);

  // Restore.
  useEffect(() => {
    let cancelled = false;
    displayApi
      .workspace()
      .then((res) => {
        if (cancelled) return;
        const parsed = parseWorkspace(res?.layout ?? null);
        const deskW = getDeskWidth();
        const ss = parsed
          ? parsed.screens.map((s) => ({ ...s, windows: uniquePageNos(clampAll(toWinStates(s.windows), deskW)) }))
          : [{ id: newScreenId(), name: "Screen 1", windows: [] }];
        const active = parsed?.active ?? ss[0].id;
        saver.markSaved(serializeWorkspace(ss, active));
        setScreens(ss);
        setActiveId(active);
        setRestored(true);
        setSave({ kind: canEdit ? "saved" : "readonly" });
        loadPages(ss.flatMap((s) => s.windows.map((w) => w.pageId)).filter((x) => x != null));
      })
      .catch((e) => {
        if (!cancelled) setSave({ kind: "error", msg: `Could not load your layout: ${errorMessage(e)}`, restore: true });
      });
    return () => {
      cancelled = true;
    };
  }, [attempt, saver, getDeskWidth, loadPages, canEdit]);

  // Save after every change, once restored. Without DISPLAY_EDIT the layout is never sent.
  useEffect(() => {
    if (restored && canEdit) saver.queue(serializeWorkspace(screens, activeId));
  }, [screens, activeId, restored, canEdit, saver]);

  // Send what is waiting when the page closes, or when this desk goes away.
  useEffect(() => {
    const onUnload = () => saver.flushOnUnload();
    window.addEventListener("beforeunload", onUnload);
    return () => {
      window.removeEventListener("beforeunload", onUnload);
      void saver.flush();
    };
  }, [saver]);

  /** After an error: restore again, or send the current workspace again. */
  const retry = useCallback(() => {
    if (save.restore) {
      setSave({ kind: "loading" });
      setAttempt((n) => n + 1);
    } else {
      saver.forget();
      saver.queue(serializeWorkspace(screens, activeId));
    }
  }, [save.restore, saver, screens, activeId]);

  /** A new, empty screen, switched to at once. */
  const addScreen = useCallback(() => {
    const id = newScreenId();
    setScreens((ss) => [...ss, { id, name: `Screen ${ss.length + 1}`, windows: [] }]);
    setActiveId(id);
  }, []);

  const switchScreen = useCallback((id) => setActiveId(id), []);

  const renameScreen = useCallback((id, name) => {
    setScreens((ss) => ss.map((s) => (s.id === id ? { ...s, name } : s)));
  }, []);

  /** Pages can be opened and saved only once the layout is back, or it would be overwritten. */
  const ready = restored;

  return { windows, setWindows, screens, activeId, addScreen, switchScreen, renameScreen, save, ready, retry };
}
