import { useMemo, useState } from 'react'
import type { Row } from '../types'
import { displayValue } from '../format'

const TIERS = ['VH', 'H', 'M', 'L'] as const

const QUALITY_LABEL: Record<string, string> = {
  OK: 'OK',
  NO_ANSWER: 'No answer',
  BLOCK_INCOMPLETE: 'Incomplete',
  NOT_DOWNLINKED: 'Not sent',
  WARMUP: 'Warm-up',
}

const ATTENTION = ['NO_ANSWER', 'BLOCK_INCOMPLETE']

export function TelemetryTable({
  rows,
  backendUp,
  initialSubsystem,
  initialOnlyProblems,
}: {
  rows: Row[]
  backendUp: boolean | null
  initialSubsystem?: string
  initialOnlyProblems?: boolean
}) {
  const [q, setQ] = useState('')
  const [subsystem, setSubsystem] = useState(initialSubsystem ?? '')
  const [tier, setTier] = useState('')
  const [onlyProblems, setOnlyProblems] = useState(initialOnlyProblems ?? false)

  const subsystems = useMemo(
    () => Array.from(new Set(rows.map((r) => r.subsystem))).sort(),
    [rows],
  )

  // The backend orders parts alphabetically (H, L, M, VH); operators read them
  // fastest-first. The sort is stable, so the LUT order within a part is kept.
  const ordered = useMemo(
    () => rows.slice().sort((a, b) => TIERS.indexOf(a.tier) - TIERS.indexOf(b.tier)),
    [rows],
  )

  const shown = useMemo(() => {
    const needle = q.trim().toLowerCase()
    return ordered.filter((r) => {
      if (subsystem && r.subsystem !== subsystem) return false
      if (tier && r.tier !== tier) return false
      // Same definition as the dashboard's Needs attention.
      if (onlyProblems && !(r.quality && ATTENTION.includes(r.quality))) return false
      if (!needle) return true
      return (
        r.name.toLowerCase().includes(needle) ||
        r.message.toLowerCase().includes(needle) ||
        r.entry.toLowerCase().includes(needle)
      )
    })
  }, [ordered, q, subsystem, tier, onlyProblems])

  return (
    <>
      <div className="filters">
        <input
          type="search"
          placeholder="Search parameter, message or entry…"
          value={q}
          onChange={(e) => setQ(e.target.value)}
        />
        <select value={subsystem} onChange={(e) => setSubsystem(e.target.value)}>
          <option value="">All subsystems</option>
          {subsystems.map((s) => (
            <option key={s} value={s}>{s}</option>
          ))}
        </select>
        <select value={tier} onChange={(e) => setTier(e.target.value)}>
          <option value="">All parts (VH/H/M/L)</option>
          {TIERS.map((t) => (
            <option key={t} value={t}>{t}</option>
          ))}
        </select>
        <label className="checklabel">
          <input
            type="checkbox"
            checked={onlyProblems}
            onChange={(e) => setOnlyProblems(e.target.checked)}
          />
          Only needs attention
        </label>
        <div className="count">
          {shown.length.toLocaleString()} of {rows.length.toLocaleString()} parameters
        </div>
      </div>

      <div className="tablewrap">
        {rows.length === 0 ? (
          <div className="empty">
            {backendUp === false ? (
              'Waiting for the MCC backend. This page loads the parameters as soon as it is reachable.'
            ) : (
              <>
                No parameters loaded. Build the decode schema first:
                <br />
                <code>python3 mcc-app/schema/build_schema.py</code>
              </>
            )}
          </div>
        ) : shown.length === 0 ? (
          <div className="empty">
            No parameters match these filters.
            {onlyProblems && ' Nothing currently needs attention.'}
          </div>
        ) : (
          <table>
            <colgroup>
              <col style={{ width: 60 }} />
              <col style={{ width: 92 }} />
              <col style={{ width: 84 }} />
              <col style={{ width: 210 }} />
              <col />
              <col style={{ width: 190 }} />
              <col style={{ width: 118 }} />
              <col style={{ width: 56 }} />
              <col style={{ width: 48 }} />
            </colgroup>
            <thead>
              <tr>
                <th>Part</th>
                <th>Subsystem</th>
                <th>Entry</th>
                <th>Message</th>
                <th>Parameter</th>
                <th style={{ textAlign: 'right' }}>Value</th>
                <th>Quality</th>
                <th style={{ textAlign: 'right' }}>MF</th>
                <th style={{ textAlign: 'right' }}>SF</th>
              </tr>
            </thead>
            <tbody>
              {shown.map((r) => (
                <tr key={r.parameter_id}>
                  <td><span className={`tier ${r.tier}`}>{r.tier}</span></td>
                  <td className="dimcol">{r.subsystem}</td>
                  <td className="dimcol">{r.entry}</td>
                  <td className="dimcol name">{r.message}</td>
                  <td className="name" title={r.name}>
                    {r.name}
                    {/* 24% of downlinked parameters are not byte aligned; a
                        decoder that assumes alignment gets these wrong. */}
                    {r.byte_aligned === 0 && (
                      <span className="nba-mark" title="Not byte aligned: decoded bit by bit">∿</span>
                    )}
                  </td>
                  <td className={`num val ${r.value == null ? 'stale' : ''}`}
                      title={r.value ?? undefined}>
                    {displayValue(r.value)}
                  </td>
                  <td className={`q ${r.quality ?? 'none'}`}>
                    <span>{r.quality ? QUALITY_LABEL[r.quality] ?? r.quality : 'No data'}</span>
                  </td>
                  <td className="num dimcol">{r.master_frame ?? '—'}</td>
                  <td className="num dimcol">{r.subframe ?? '—'}</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>
    </>
  )
}
