import { useEffect, useState } from 'react'
import type { Status } from '../types'
import { Icon } from './Icon'

/**
 * Start and stop the XDLINX telemetry stack (the XDOS OBC, its subsystem
 * models and the XMCC forwarder) from the browser.
 *
 * The backend runs one fixed command, so nothing typed here reaches a shell.
 * Telemetry only flows once the EPS kill switch is released, which is what
 * --power-on does; without it the OBC boots unpowered and the feed stays silent.
 * Stopping asks for a second click: a restart costs ~30 s of OBC boot.
 */
export function SimControl({
  status,
  onChanged,
}: {
  status: Status | null
  onChanged: () => void
}) {
  const [busy, setBusy] = useState<'start' | 'stop' | null>(null)
  const [confirmStop, setConfirmStop] = useState(false)
  const [failure, setFailure] = useState<string | null>(null)
  const sim = status?.simulator
  const running = sim?.running ?? false
  const error = failure ?? sim?.error ?? null

  useEffect(() => {
    if (!confirmStop) return
    const t = setTimeout(() => setConfirmStop(false), 6000)
    return () => clearTimeout(t)
  }, [confirmStop])

  const call = async (what: 'start' | 'stop') => {
    setBusy(what)
    setFailure(null)
    try {
      const r = await fetch(`/api/sim/${what}`, { method: 'POST' })
      if (!r.ok) throw new Error(`${what === 'start' ? 'Start' : 'Stop'} failed (HTTP ${r.status})`)
      const body = await r.json().catch(() => null)
      if (body?.error) throw new Error(body.error)
    } catch (e) {
      setFailure(e instanceof Error ? e.message : String(e))
    } finally {
      setBusy(null)
      setConfirmStop(false)
      onChanged()
    }
  }

  const detail = sim
    ? running
      ? `Running · ${sim.processes.length} processes${sim.pid ? ` · pid ${sim.pid}` : ''}`
      : 'Stopped'
    : 'Backend not reachable'

  return (
    <div className="feedctl" title={detail}>
      {error && <span className="feed-err" role="alert" title={error}>{error}</span>}
      {!sim ? (
        <button disabled>Feed control unavailable</button>
      ) : running ? (
        confirmStop ? (
          <>
            <button onClick={() => setConfirmStop(false)}>
              <Icon name="x" size={14} />
              Cancel
            </button>
            <button className="danger" disabled={busy !== null} onClick={() => call('stop')}>
              <Icon name="stop" size={14} />
              {busy === 'stop' ? 'Stopping…' : 'Confirm stop'}
            </button>
          </>
        ) : (
          <button className="danger" disabled={busy !== null} onClick={() => setConfirmStop(true)}>
            <Icon name="stop" size={14} />
            Stop feed
          </button>
        )
      ) : (
        <button className="primary" disabled={busy !== null} onClick={() => call('start')}>
          <Icon name="satellite" size={15} />
          {busy === 'start' ? 'Starting…' : 'Start feed'}
        </button>
      )}
    </div>
  )
}
