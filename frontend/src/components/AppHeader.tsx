import { useEffect, useState } from 'react'
import type { Status } from '../types'
import { FRESH_MS } from '../format'
import type { ThemeChoice } from '../theme'
import { SimControl } from './SimControl'

export type Tab = 'dashboard' | 'telemetry' | 'charts' | 'track' | 'decode' | 'docs' | 'xdlinx'

export const TABS: { id: Tab; label: string; hint: string }[] = [
  { id: 'dashboard', label: 'Dashboard', hint: 'Mission overview' },
  { id: 'telemetry', label: 'Telemetry', hint: 'Every parameter the LUT downlinks, live' },
  { id: 'charts', label: 'Charts', hint: 'Values over time' },
  { id: 'track', label: 'Ground track', hint: 'Spacecraft position on a globe and a map, from GPS telemetry' },
  { id: 'decode', label: 'Decode trace', hint: 'From payload bytes to engineering value' },
  { id: 'docs', label: 'Docs', hint: 'How it is built, database schema, REST API (Swagger), live stream' },
  { id: 'xdlinx', label: 'XDLINX', hint: 'XDSAT satellite platforms and links' },
]

const THEMES: { id: ThemeChoice; label: string }[] = [
  { id: 'system', label: 'Auto' },
  { id: 'light', label: 'Light' },
  { id: 'dark', label: 'Dark' },
]

export function AppHeader({
  tab,
  onTab,
  status,
  live,
  lastMsgAt,
  theme,
  onTheme,
  onFeedChanged,
}: {
  tab: Tab
  onTab: (t: Tab) => void
  status: Status | null
  live: boolean
  lastMsgAt: number | null
  theme: ThemeChoice
  onTheme: (t: ThemeChoice) => void
  onFeedChanged: () => void
}) {
  const [now, setNow] = useState(Date.now())
  useEffect(() => {
    const t = setInterval(() => setNow(Date.now()), 1000)
    return () => clearInterval(t)
  }, [])
  const connected = !!status?.feed.connected && live
  const receiving = connected && lastMsgAt != null && now - lastMsgAt < FRESH_MS
  const pill = receiving ? 'on' : connected ? 'idle' : 'off'
  return (
    <header className="bar">
      <div className="bar-in">
        <div className="brandmark">
          <img className="logo-ink" src="/xdlinx/logo-ink-800.png" alt="XDLINX Space Labs" />
          <img className="logo-white" src="/xdlinx/logo-white-800.png" alt="XDLINX Space Labs" />
          <span className="sep" />
          <div className="brand-title">
            <b>OrbLinx MCC</b>
            <span>Mission control · XMCC telemetry</span>
          </div>
        </div>
        <div className="grow" />
        <span className={`linkpill ${pill}`} role="status">
          <span className={`dot ${pill}`} />
          {receiving ? 'Receiving' : connected ? 'No data' : status ? 'Feed stopped' : 'Offline'}
        </span>
        <div className="themeswitch" role="group" aria-label="Theme">
          {THEMES.map((t) => (
            <button
              key={t.id}
              className={theme === t.id ? 'on' : ''}
              aria-pressed={theme === t.id}
              onClick={() => onTheme(t.id)}
            >
              {t.label}
            </button>
          ))}
        </div>
        <SimControl status={status} onChanged={onFeedChanged} />
      </div>
      <nav className="nav" aria-label="Pages">
        {TABS.map((t) => (
          <button
            key={t.id}
            className={tab === t.id ? 'on' : ''}
            aria-current={tab === t.id ? 'page' : undefined}
            onClick={() => onTab(t.id)}
            title={t.hint}
          >
            {t.label}
          </button>
        ))}
      </nav>
    </header>
  )
}
