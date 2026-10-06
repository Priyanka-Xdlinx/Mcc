/** The layout is saved this long after the last change. */
export const SAVE_DELAY_MS = 300;
/** A failed save is tried again after this long. */
export const RETRY_MS = 5000;

/**
 * Saves the signed-in user's window layout: debounced, retried until it succeeds
 * (nothing else holds the layout), and sent with keepalive when the page closes.
 * Plain JavaScript with no React, so its timing is easy to test.
 *
 *   save(json)          -> Promise   the normal PUT
 *   saveOnUnload(json)               fire-and-forget PUT that outlives the page
 *   onState(kind, msg)               "pending" | "saved" | "error"
 */
export function createWorkspaceSaver({ save, saveOnUnload, onState }) {
  let pending = null;
  let timer = null;
  let lastSaved = null;

  const clear = () => {
    if (timer) clearTimeout(timer);
    timer = null;
  };

  async function flush() {
    clear();
    const json = pending;
    if (json === null) return;
    pending = null;
    try {
      await save(json);
      lastSaved = json;
      if (pending === null) onState("saved");
    } catch (e) {
      // Keep it and try again, unless a newer layout is already waiting.
      if (pending === null) pending = json;
      timer = setTimeout(flush, RETRY_MS);
      onState("error", e);
    }
  }

  return {
    /** The layout as last confirmed by the server; a change equal to it needs no save. */
    get lastSaved() {
      return lastSaved;
    },
    /** Marks a layout as already on the server (after a restore). */
    markSaved(json) {
      lastSaved = json;
    },
    /** Forgets what was saved, so the next queue sends even an unchanged layout. */
    forget() {
      lastSaved = null;
    },
    queue(json) {
      if (json === lastSaved) return;
      pending = json;
      onState("pending");
      clear();
      timer = setTimeout(flush, SAVE_DELAY_MS);
    },
    flush,
    flushOnUnload() {
      clear();
      if (pending !== null) saveOnUnload(pending);
      pending = null;
    },
  };
}
