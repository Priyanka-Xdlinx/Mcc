import { useState } from 'react'
import type { Status } from '../types'

/**
 * Start and stop the XDOS simulator from the browser.
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
  const [busy, setBusy] = useState(false)
  const sim = status?.simulator
  const running = sim?.running ?? false

  const call = async (path: string) => {
    setBusy(true)
    try {
      await fetch(path, { method: 'POST' })
      onChanged()
    } finally {
      setBusy(false)
    }
  }

  return (
    <div className="controls">
      {sim?.error && <span className="err">{sim.error}</span>}
      {!sim?.error && running && (
        <span className="err" style={{ color: 'var(--dim)' }}>
          {sim?.adopted ? 'already running' : `pid ${sim?.pid}`} ·{' '}
          {sim?.processes.length ?? 0} processes
        </span>
      )}
      <button
        className="primary"
        disabled={busy || running}
        onClick={() => call('/api/sim/start')}
        title="Runs scripts/run_stack.sh --power-on --no-gs --no-windows"
      >
        {busy && !running ? 'Starting…' : 'Start simulator'}
      </button>
      <button
        className="danger"
        disabled={busy || !running}
        onClick={() => call('/api/sim/stop')}
        title="Stops the stack, including xdos_tm_view which kill_all.sh misses"
      >
        Stop
      </button>
    </div>
  )
}
