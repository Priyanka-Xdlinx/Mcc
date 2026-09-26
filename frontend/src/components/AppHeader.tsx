import type { Status } from '../types'
import type { ThemeChoice } from '../theme'
import { SimControl } from './SimControl'

export type Tab = 'dashboard' | 'telemetry' | 'charts' | 'decode'

export const TABS: { id: Tab; label: string; hint: string }[] = [
  { id: 'dashboard', label: 'Dashboard', hint: 'Mission overview' },
  { id: 'telemetry', label: 'Telemetry', hint: 'Every parameter the LUT downlinks, live' },
  { id: 'charts', label: 'Charts', hint: 'Values over time' },
  { id: 'decode', label: 'Decode trace', hint: 'From payload bytes to engineering value' },
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
  theme,
  onTheme,
  onFeedChanged,
}: {
  tab: Tab
  onTab: (t: Tab) => void
  status: Status | null
  live: boolean
  theme: ThemeChoice
  onTheme: (t: ThemeChoice) => void
  onFeedChanged: () => void
}) {
  const connected = !!status?.feed.connected && live
  return (
    <header className="appbar">
      <div className="brandmark">
        <img src="/xdlinx/logo-white.png" alt="XDLINX Space Labs" />
        <span className="sep" />
        <div>
          <div className="app-name">OrbLinx MCC</div>
          <div className="app-sub">Mission control</div>
        </div>
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

      <div className="appbar-right">
        <span className={`linkpill ${connected ? 'on' : 'off'}`}>
          <span className={`dot ${connected ? 'on' : 'off'}`} />
          {connected ? 'Receiving' : live ? 'Feed idle' : 'Offline'}
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
    </header>
  )
}
