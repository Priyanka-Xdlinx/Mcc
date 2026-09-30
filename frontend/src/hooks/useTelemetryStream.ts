import { useCallback, useEffect, useRef, useState } from 'react'
import type { Row, Status } from '../types'

interface Update {
  parameterId: number
  entry: string
  tier: string
  name: string
  value: string
  quality: Row['quality']
}

/** A parameter whose engineering value has changed during the current run. */
export interface Mover {
  id: number
  first: number
  at: number
  changes: number
  points: number[]
}

/** Position and time of the newest subframe seen on the socket. */
export interface LastFrame {
  masterFrame: number
  subframe: number
  spacecraftTime: string | null
  /** When the backend received this subframe (ISO-8601 UTC), if it said. */
  receivedAt: string | null
}

interface SocketMessage {
  type: 'telemetry' | 'status' | 'blockIncomplete'
  masterFrame?: number
  subframe?: number
  spacecraftTime?: string
  receivedAt?: string
  updates?: Update[]
  status?: Status['feed']
}

/** The numeric reading behind a formatted value, or null when there is none. */
function numberOf(value: string, quality: Row['quality']): number | null {
  if (quality !== 'OK' && quality !== 'WARMUP') return null
  if (value === 'true') return 1
  if (value === 'false') return 0
  const n = parseFloat(value)
  return Number.isFinite(n) ? n : null
}

/**
 * Holds the full parameter table and patches it from the websocket.
 *
 * The table is fetched once so every parameter the LUT downlinks is listed even
 * before its first reading arrives - an empty row with no value is meaningfully
 * different from a row that is absent.
 */
export function useTelemetryStream() {
  const [rows, setRows] = useState<Row[]>([])
  const [status, setStatus] = useState<Status | null>(null)
  const [live, setLive] = useState(false)
  const [backendUp, setBackendUp] = useState<boolean | null>(null)
  const [lastMsgAt, setLastMsgAt] = useState<number | null>(null)
  const [lastFrame, setLastFrame] = useState<LastFrame | null>(null)
  const [movers, setMovers] = useState<Mover[]>([])
  const byId = useRef(new Map<number, number>())
  const current = useRef<Row[]>([])
  const moving = useRef(new Map<number, Mover>())
  const lastMf = useRef<number | null>(null)

  const reload = useCallback(async () => {
    try {
      const r = await fetch('/api/telemetry/latest')
      if (!r.ok) return
      const data: Row[] = await r.json()
      byId.current = new Map(data.map((row, i) => [row.parameter_id, i]))
      current.current = data
      setRows(data)
    } catch {
      /* backend down: the socket's next open reloads */
    }
  }, [])

  const refreshStatus = useCallback(async () => {
    try {
      const r = await fetch('/api/status')
      if (!r.ok) throw new Error(String(r.status))
      setStatus(await r.json())
      setBackendUp(true)
    } catch {
      // Backend down or restarting: say so, and let the next tick recover.
      setBackendUp(false)
    }
  }, [])

  useEffect(() => {
    void reload()
    void refreshStatus()
    const t = setInterval(refreshStatus, 2000)
    return () => clearInterval(t)
  }, [reload, refreshStatus])

  useEffect(() => {
    let socket: WebSocket | null = null
    let retry: ReturnType<typeof setTimeout> | null = null
    let closed = false

    const connect = () => {
      const proto = location.protocol === 'https:' ? 'wss' : 'ws'
      const s = new WebSocket(`${proto}://${location.host}/ws/telemetry`)
      socket = s
      // (Re)load on every connect: if the page opened while the backend was
      // down, the table is empty and updates would have nothing to patch.
      s.onopen = () => {
        setLive(true)
        void reload()
        void refreshStatus()
      }
      s.onclose = () => {
        if (closed) return
        setLive(false)
        retry = setTimeout(connect, 1500)
      }
      s.onmessage = (ev) => {
        const msg: SocketMessage = JSON.parse(ev.data)
        if (msg.type === 'status' && msg.status) {
          setStatus((st) => (st ? { ...st, feed: msg.status! } : st))
          return
        }
        if (msg.type !== 'telemetry' || !msg.updates) return
        const now = Date.now()
        const next = current.current.slice()
        let moved = false
        // An OBC reboot restarts the master frame count: every value jumps, which
        // is not the same as a parameter changing. Start the change list afresh.
        const rebooted =
          msg.masterFrame !== undefined && lastMf.current !== null && msg.masterFrame < lastMf.current
        if (msg.masterFrame !== undefined) lastMf.current = msg.masterFrame
        if (rebooted) {
          moving.current.clear()
          setMovers([])
        }
        for (const u of msg.updates) {
          const i = byId.current.get(u.parameterId)
          if (i === undefined) continue
          const prev = next[i]
          const y = numberOf(u.value, u.quality)
          // Compare the decoder's text on both sides: the loaded table carries the
          // stored full-precision float (0.2000000029...), live updates carry the
          // rounded text (0.20), and mixing the two would count a change that
          // never happened. A change is a new value between two good readings -
          // not a quality flip (value -> "no answer") and not warm-up data.
          const prevY = prev.value === null ? null : numberOf(prev.value, prev.quality)
          if (!rebooted && prev.quality === 'OK' && u.quality === 'OK'
              && prev.value !== u.value && prevY !== null && y !== null && y !== prevY) {
            const m = moving.current.get(u.parameterId) ?? {
              id: u.parameterId, first: now, at: now, changes: 0, points: [prevY],
            }
            m.at = now
            m.changes += 1
            m.points = [...m.points, y].slice(-60)
            moving.current.set(u.parameterId, m)
            moved = true
          }
          next[i] = {
            ...prev,
            value: u.value,
            value_num: y,
            quality: u.quality,
            master_frame: msg.masterFrame ?? prev.master_frame,
            subframe: msg.subframe ?? prev.subframe,
            spacecraft_time: msg.spacecraftTime ?? prev.spacecraft_time,
            received_at: msg.receivedAt ?? prev.received_at,
          }
        }
        current.current = next
        setRows(next)
        setLastMsgAt(now)
        if (msg.masterFrame !== undefined && msg.subframe !== undefined) {
          setLastFrame({
            masterFrame: msg.masterFrame,
            subframe: msg.subframe,
            spacecraftTime: msg.spacecraftTime ?? null,
            receivedAt: msg.receivedAt ?? null,
          })
        }
        if (moved) setMovers(Array.from(moving.current.values()))
      }
    }

    connect()
    return () => {
      closed = true
      if (retry) clearTimeout(retry)
      if (socket) {
        socket.onopen = socket.onclose = socket.onmessage = null
        socket.close()
      }
    }
  }, [reload, refreshStatus])

  return { rows, status, live, backendUp, lastMsgAt, lastFrame, movers, reload, refreshStatus }
}
