import { useState } from 'react'
import type { Status } from '../types'

/**
 * Start and stop the XDLINX telemetry stack (the XDOS OBC, its subsystem
 * models and the XMCC forwarder) from the browser.
 *
 * The backend runs one fixed command, so nothing typed here reaches a shell.
 * Telemetry only flows once the EPS kill switch is released, which is what
 * --power-on does; without it the OBC boots unpowered and the feed stays silent.
 */
export function SimControl({
  status,
  onChanged,
}: {
  status: Status | null
  onChanged: () => void
}) {
  const [busy, setBusy] = useState<'start' | 'stop' | null>(null)
  const sim = status?.simulator
  const running = sim?.running ?? false
  const detail = sim
    ? running
      ? `Running · ${sim.processes.length} processes${sim.pid ? ` · pid ${sim.pid}` : ''}`
      : 'Stopped'
    : 'Status unknown'

  const call = async (what: 'start' | 'stop') => {
    setBusy(what)
    try {
      await fetch(`/api/sim/${what}`, { method: 'POST' })
      onChanged()
    } finally {
      setBusy(null)
    }
  }

  return (
    <div className="feedctl" title={sim?.error ?? detail}>
      {running ? (
        <button className="danger" disabled={busy !== null} onClick={() => call('stop')}>
          {busy === 'stop' ? 'Stopping…' : 'Stop feed'}
        </button>
      ) : (
        <button className="primary" disabled={busy !== null} onClick={() => call('start')}>
          {busy === 'start' ? 'Starting…' : 'Start feed'}
        </button>
      )}
    </div>
  )
}
