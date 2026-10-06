import { useSyncExternalStore } from "react";

/**
 * Feed-backed header values shared by every open window. They change on every
 * subframe; routing them through this tiny store means only the header cells
 * of each window re-render, never the 20-parameter tables around them.
 *
 *   frameTime, frameNo, receivedAt   the newest subframe
 *   framesValid, framesRejected      header checks since the backend started
 *   session                          subframes in the current session
 */
const EMPTY = { frameTime: null, frameNo: null, receivedAt: null, framesValid: null, framesRejected: null, session: null };
const KEYS = Object.keys(EMPTY);

let current = EMPTY;
const listeners = new Set();

export function setHeader(next) {
  if (KEYS.every((k) => current[k] === next[k])) return;
  current = next;
  listeners.forEach((l) => l());
}

function subscribe(l) {
  listeners.add(l);
  return () => listeners.delete(l);
}

const get = () => current;

export function useHeader() {
  return useSyncExternalStore(subscribe, get, get);
}
