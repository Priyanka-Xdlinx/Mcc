import { useSyncExternalStore } from 'react'

/** Feed-backed header values shared by every open window. */
export interface HeaderValues {
  /** Spacecraft time of the newest subframe. */
  frameTime: string | null
  /** "MF#<mf> SF<sf>" of the newest subframe. */
  frameNo: string | null
  /** Backend receive time of the newest subframe (ISO-8601 UTC). */
  receivedAt: string | null
  /** Subframes with a valid header since backend start. */
  framesValid: number | null
  /** Headers rejected since backend start (shown as a tooltip). */
  framesRejected: number | null
  /** Subframes received in the current session. */
  session: number | null
}

const EMPTY: HeaderValues = {
  frameTime: null, frameNo: null, receivedAt: null, framesValid: null, framesRejected: null, session: null,
}

// A tiny external store: the header changes on every subframe, and routing it
// through a store means only the header cells of each window re-render, never
// the 20-parameter tables or the window frames around them.
let current: HeaderValues = EMPTY
const listeners = new Set<() => void>()

export function setHeader(next: HeaderValues) {
  const c = current
  if (
    c.frameTime === next.frameTime && c.frameNo === next.frameNo && c.receivedAt === next.receivedAt &&
    c.framesValid === next.framesValid && c.framesRejected === next.framesRejected && c.session === next.session
  ) return
  current = next
  listeners.forEach((l) => l())
}

function subscribe(l: () => void) {
  listeners.add(l)
  return () => {
    listeners.delete(l)
  }
}

const get = () => current

export function useHeader(): HeaderValues {
  return useSyncExternalStore(subscribe, get, get)
}
