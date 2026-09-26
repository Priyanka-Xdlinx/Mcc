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
  const byId = useRef(new Map<number, number>())

  const reload = useCallback(async () => {
    const r = await fetch('/api/telemetry/latest')
    const data: Row[] = await r.json()
    byId.current = new Map(data.map((row, i) => [row.parameter_id, i]))
    setRows(data)
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
        setRows((prev) => {
          const next = prev.slice()
          for (const u of msg.updates!) {
            const i = byId.current.get(u.parameterId)
            if (i === undefined) continue
            next[i] = {
              ...next[i],
              value: u.value,
              quality: u.quality,
              master_frame: msg.masterFrame ?? next[i].master_frame,
              subframe: msg.subframe ?? next[i].subframe,
              spacecraft_time: msg.spacecraftTime ?? next[i].spacecraft_time,
            }
          }
          return next
        })
      }
    }

    connect()
    return () => {
      closed = true
      if (retry) clearTimeout(retry)
      socket?.close()
    }
  }, [])

  return { rows, status, live, reload, refreshStatus }
}
