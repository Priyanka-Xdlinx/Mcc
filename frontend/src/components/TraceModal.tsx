import { useEffect, useState } from 'react'
import { HexDump } from './HexDump'

interface TraceLine {
  label: string
  value: string
  note: string | null
}
interface TraceHex {
  title: string
  baseOffset: number
  bytes: string[]
  markFrom: number | null
  markTo: number | null
}
interface TraceStep {
  title: string
  lines: TraceLine[]
  hex: TraceHex[]
}
interface Trace {
  error?: string
  steps?: TraceStep[]
  parameter?: string
  entry?: string
  subsystem?: string
  message?: string
  tier?: string
  masterFrame?: number
  subframe?: number
  spacecraftTime?: string
  value?: string
  quality?: string
}

/** Which layer a step belongs to, for its badge and colour. */
function layerOf(title: string): '1' | '2' | 'R' {
  if (title.startsWith('Layer 1')) return '1'
  if (title.startsWith('Layer 2') || title === 'The arithmetic') return '2'
  return 'R'
}

/**
 * The full decode of one parameter, opened over the frame it came from.
 *
 * Fetched for the exact frame being viewed rather than the newest one, so it
 * explains the value that is on screen and not a later reading.
 */
export function TraceModal({
  parameterId,
  mf,
  sf,
  onClose,
}: {
  parameterId: number
  mf: number | null
  sf: number | null
  onClose: () => void
}) {
  const [trace, setTrace] = useState<Trace | null>(null)

  useEffect(() => {
    let dead = false
    const qs =
      mf !== null && sf !== null
        ? `?parameterId=${parameterId}&mf=${mf}&sf=${sf}`
        : `?parameterId=${parameterId}`
    fetch(`/api/trace${qs}`)
      .then((r) => r.json())
      .then((j) => !dead && setTrace(j))
      .catch(() => !dead && setTrace({ error: 'could not load the trace' }))
    return () => {
      dead = true
    }
  }, [parameterId, mf, sf])

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => e.key === 'Escape' && onClose()
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [onClose])

  return (
    <div className="tm-backdrop" onClick={onClose}>
      <div className="tm-panel" onClick={(e) => e.stopPropagation()} role="dialog" aria-modal>
        <div className="tm-bar">
          <div>
            <div className="tm-title">{trace?.parameter ?? 'Decode trace'}</div>
            <div className="tm-sub">
              {trace?.entry && (
                <>
                  {trace.entry} · {trace.subsystem} · {trace.message}
                  {trace.tier && <span className={`tier ${trace.tier}`}> {trace.tier}</span>}
                  {' · '}
                  MF#{trace.masterFrame} SF{trace.subframe} · {trace.spacecraftTime}
                </>
              )}
            </div>
          </div>
          <div className="tm-result">
            <div className="dv-k">Decoded value</div>
            <div className={`tm-value q-${trace?.quality ?? ''}`}>{trace?.value ?? '—'}</div>
          </div>
          <button className="tm-close" onClick={onClose} title="Close (Esc)">
            ✕
          </button>
        </div>

        <div className="tm-body">
          {!trace ? (
            <div className="tin-loading">loading trace…</div>
          ) : trace.error ? (
            <div className="tin-loading">{trace.error}</div>
          ) : (
            <div className="tin-steps">
              {(trace.steps ?? []).map((s, i) => {
                const layer = layerOf(s.title)
                return (
                  <div className={`tin-step L${layer}`} key={i}>
                    <div className="tin-step-head">
                      <span className={`tin-badge L${layer}`}>
                        {layer === 'R' ? '✓' : layer}
                      </span>
                      <span className="tin-step-title">{s.title}</span>
                    </div>
                    {s.lines.length > 0 && (
                      <table className="tin-lines">
                        <tbody>
                          {s.lines.map((l, j) => (
                            <tr key={j}>
                              <td className="tl-k">{l.label}</td>
                              <td className="tl-v">{l.value}</td>
                              <td className="tl-n">{l.note ?? ''}</td>
                            </tr>
                          ))}
                        </tbody>
                      </table>
                    )}
                    {s.hex.map((h, j) => (
                      <HexDump
                        key={j}
                        title={h.title}
                        bytes={h.bytes}
                        baseOffset={h.baseOffset}
                        markFrom={h.markFrom}
                        markTo={h.markTo}
                        maxRows={8}
                      />
                    ))}
                  </div>
                )
              })}
            </div>
          )}
        </div>
      </div>
    </div>
  )
}
