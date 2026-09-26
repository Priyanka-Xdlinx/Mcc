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

/** A parameter whose value has changed at least once since the page opened. */
export interface Mover {
  id: number
  at: number
  changes: number
  points: number[]
}

interface SocketMessage {
  type: 'telemetry' | 'status' | 'blockIncomplete'
  masterFrame?: number
  subframe?: number
  spacecraftTime?: string
  updates?: Update[]
  status?: Status['feed']
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
  const [lastMsgAt, setLastMsgAt] = useState<number | null>(null)
  const [movers, setMovers] = useState<Mover[]>([])
  const byId = useRef(new Map<number, number>())
  const current = useRef<Row[]>([])
  const moving = useRef(new Map<number, Mover>())
  const lastMf = useRef<number | null>(null)

  const reload = useCallback(async () => {
    try {
      const r = await fetch('/api/telemetry/latest')
      const data: Row[] = await r.json()
      byId.current = new Map(data.map((row, i) => [row.parameter_id, i]))
      current.current = data
      setRows(data)
    } catch {
      /* backend restarting; the socket reconnect reloads */
    }
  }, [])

  const refreshStatus = useCallback(async () => {
    try {
      const r = await fetch('/api/status')
      setStatus(await r.json())
    } catch {
      /* backend restarting; the next tick will pick it up */
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
      socket = new WebSocket(`${proto}://${location.host}/ws/telemetry`)
      socket.onopen = () => setLive(true)
      socket.onclose = () => {
        setLive(false)
        if (!closed) retry = setTimeout(connect, 1500)
      }
      socket.onmessage = (ev) => {
        const msg: SocketMessage = JSON.parse(ev.data)
        if (msg.type === 'status' && msg.status) {
          setStatus((s) => (s ? { ...s, feed: msg.status! } : s))
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
          const before = next[i].value
          // Warm-up values (first master frame after boot) are not trustworthy.
          if (!rebooted && u.quality !== 'WARMUP' && next[i].quality !== 'WARMUP'
              && before !== null && before !== u.value) {
            const m = moving.current.get(u.parameterId) ?? {
              id: u.parameterId,
              at: now,
              changes: 0,
              points: [parseFloat(before)].filter(Number.isFinite),
            }
            const y = parseFloat(u.value)
            m.at = now
            m.changes += 1
            if (Number.isFinite(y)) m.points = [...m.points, y].slice(-60)
            moving.current.set(u.parameterId, m)
            moved = true
          }
          next[i] = {
            ...next[i],
            value: u.value,
            quality: u.quality,
            master_frame: msg.masterFrame ?? next[i].master_frame,
            subframe: msg.subframe ?? next[i].subframe,
            spacecraft_time: msg.spacecraftTime ?? next[i].spacecraft_time,
          }
        }
        current.current = next
        setRows(next)
        setLastMsgAt(now)
        if (moved) setMovers(Array.from(moving.current.values()))
      }
    }

    connect()
    return () => {
      closed = true
      if (retry) clearTimeout(retry)
      socket?.close()
    }
  }, [])

  return { rows, status, live, lastMsgAt, movers, reload, refreshStatus }
}
