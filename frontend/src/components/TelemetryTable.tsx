import { useMemo, useState } from 'react'
import type { Row } from '../types'

const TIERS = ['VH', 'H', 'M', 'L'] as const

export function TelemetryTable({ rows }: { rows: Row[] }) {
  const [q, setQ] = useState('')
  const [subsystem, setSubsystem] = useState('')
  const [tier, setTier] = useState('')
  const [onlyProblems, setOnlyProblems] = useState(false)

  const subsystems = useMemo(
    () => Array.from(new Set(rows.map((r) => r.subsystem))).sort(),
    [rows],
  )

  const shown = useMemo(() => {
    const needle = q.trim().toLowerCase()
    return rows.filter((r) => {
      if (subsystem && r.subsystem !== subsystem) return false
      if (tier && r.tier !== tier) return false
      if (onlyProblems && (r.quality === 'OK' || r.quality === null)) return false
      if (!needle) return true
      return (
        r.name.toLowerCase().includes(needle) ||
        r.message.toLowerCase().includes(needle) ||
        r.entry.toLowerCase().includes(needle)
      )
    })
  }, [rows, q, subsystem, tier, onlyProblems])

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
          <option value="">All tiers</option>
          {TIERS.map((t) => (
            <option key={t} value={t}>{t}</option>
          ))}
        </select>
        <label style={{ display: 'flex', gap: 6, alignItems: 'center', color: 'var(--dim)' }}>
          <input
            type="checkbox"
            checked={onlyProblems}
            onChange={(e) => setOnlyProblems(e.target.checked)}
          />
          Only flagged
        </label>
        <div className="count">
          {shown.length.toLocaleString()} of {rows.length.toLocaleString()} parameters
        </div>
      </div>

      <div className="tablewrap">
        {rows.length === 0 ? (
          <div className="empty">
            No parameters loaded. Build the decode schema first:
            <br />
            <code>python3 mcc-app/schema/build_schema.py</code>
          </div>
        ) : (
          <table>
            <colgroup>
              <col style={{ width: 54 }} />
              <col style={{ width: 92 }} />
              <col style={{ width: 84 }} />
              <col style={{ width: 210 }} />
              <col />
              <col style={{ width: 190 }} />
              <col style={{ width: 130 }} />
              <col style={{ width: 56 }} />
              <col style={{ width: 48 }} />
            </colgroup>
            <thead>
              <tr>
                <th>Tier</th>
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
                      <span style={{ color: 'var(--dimmer)', marginLeft: 6 }} title="not byte aligned">∿</span>
                    )}
                  </td>
                  <td className={`num val ${r.value == null ? 'stale' : ''}`}
                      title={r.value ?? undefined}>
                    {r.value ?? '—'}
                  </td>
                  <td className={`q ${r.quality ?? ''}`}>{r.quality ?? 'no data'}</td>
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
