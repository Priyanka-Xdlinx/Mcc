import { useEffect, useRef, useState } from 'react'
import type { Status } from '../types'
import { FRESH_MS } from '../format'
import type { ThemeChoice } from '../theme'
import { SimControl } from './SimControl'
import { Icon, type IconName } from './Icon'
import type { AuthUser } from '../auth/auth'
import { UserMenu } from '../auth/UserMenu'

export type Tab = 'telemetry' | 'charts' | 'decode' | 'display' | 'docs'

export const TABS: { id: Tab; label: string; hint: string }[] = [
  { id: 'telemetry', label: 'Telemetry', hint: 'Every parameter the LUT downlinks, live' },
  { id: 'charts', label: 'Charts', hint: 'Values over time' },
  { id: 'decode', label: 'Decode trace', hint: 'From payload bytes to engineering value' },
  { id: 'display', label: 'Display', hint: 'Display pages: floating windows of 20 parameters' },
  { id: 'docs', label: 'Docs', hint: 'How it is built, database schema, REST API (Swagger), live stream' },
]

const THEMES: { id: ThemeChoice; label: string; icon: IconName }[] = [
  { id: 'system', label: 'Auto', icon: 'auto' },
  { id: 'light', label: 'Light', icon: 'sun' },
  { id: 'dark', label: 'Dark', icon: 'moon' },
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
  user,
  onUser,
  onSignOut,
  signingOut,
  collapsed = false,
  onToggleCollapse,
}: {
  /** Display page only: hide the header so the windows get the whole screen. */
  collapsed?: boolean
  onToggleCollapse?: () => void
  tab: Tab
  onTab: (t: Tab) => void
  status: Status | null
  live: boolean
  lastMsgAt: number | null
  theme: ThemeChoice
  onTheme: (t: ThemeChoice) => void
  onFeedChanged: () => void
  user: AuthUser
  onUser: (u: AuthUser) => void
  onSignOut: () => void
  signingOut: boolean
}) {
  const [now, setNow] = useState(Date.now())
  useEffect(() => {
    const t = setInterval(() => setNow(Date.now()), 1000)
    return () => clearInterval(t)
  }, [])
  const connected = !!status?.feed.connected && live
  const receiving = connected && lastMsgAt != null && now - lastMsgAt < FRESH_MS
  const pill = receiving ? 'on' : connected ? 'idle' : 'off'
  if (collapsed && onToggleCollapse) {
    return (
      <CollapsedMenu
        tab={tab}
        onTab={onTab}
        theme={theme}
        onTheme={onTheme}
        pill={pill}
        onExpand={onToggleCollapse}
        onSignOut={onSignOut}
      />
    )
  }
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
              <Icon name={t.icon} size={14} />
              {t.label}
            </button>
          ))}
        </div>
        <SimControl status={status} onChanged={onFeedChanged} />
        <UserMenu user={user} onUser={onUser} onSignOut={onSignOut} signingOut={signingOut} />
      </div>
      <nav className="nav" aria-label="Pages">
        {onToggleCollapse && (
          <button
            type="button"
            className="bar-toggle"
            onClick={onToggleCollapse}
            aria-expanded={true}
            title="Hide the header and toolbar to focus on the display windows"
          >
            <Icon name="up" size={16} />
            <span className="sr">Hide header</span>
          </button>
        )}
        {TABS.map((t) => (
          <button
            key={t.id}
            className={tab === t.id ? 'on' : ''}
            aria-current={tab === t.id ? 'page' : undefined}
            onClick={() => onTab(t.id)}
            title={t.hint}
          >
            <Icon name={t.id} size={16} />
            {t.label}
          </button>
        ))}
      </nav>
    </header>
  )
}

/** Header collapsed on the Display page: a hamburger whose layover holds the icons. */
function CollapsedMenu({
  tab, onTab, theme, onTheme, pill, onExpand, onSignOut,
}: {
  tab: Tab
  onTab: (t: Tab) => void
  theme: ThemeChoice
  onTheme: (t: ThemeChoice) => void
  pill: string
  onExpand: () => void
  onSignOut: () => void
}) {
  const [open, setOpen] = useState(false)
  const ref = useRef<HTMLDivElement>(null)
  useEffect(() => {
    if (!open) return
    const key = (e: KeyboardEvent) => e.key === 'Escape' && setOpen(false)
    const down = (e: PointerEvent) => {
      if (!ref.current?.contains(e.target as Node)) setOpen(false)
    }
    window.addEventListener('keydown', key)
    window.addEventListener('pointerdown', down)
    return () => {
      window.removeEventListener('keydown', key)
      window.removeEventListener('pointerdown', down)
    }
  }, [open])
  return (
    <div className="hamb" ref={ref}>
      <button
        type="button"
        className={`hamb-btn${open ? ' on' : ''}`}
        onClick={() => setOpen((v) => !v)}
        aria-haspopup="menu"
        aria-expanded={open}
        aria-label="Menu"
        title="Menu"
      >
        <Icon name="menu" size={18} />
        <span className={`dot ${pill}`} />
      </button>
      <div className={`hamb-menu${open ? ' open' : ''}`} role="menu" aria-hidden={!open}>
        {TABS.map((t, i) => (
          <button
            key={t.id}
            type="button"
            role="menuitem"
            tabIndex={open ? 0 : -1}
            className={tab === t.id ? 'on' : ''}
            style={{ transitionDelay: open ? `${40 + i * 25}ms` : '0ms' }}
            onClick={() => {
              setOpen(false)
              onTab(t.id)
            }}
          >
            <Icon name={t.id} size={17} />
            {t.label}
          </button>
        ))}
        <div className="hamb-sep" />
        <div className="hamb-themes" role="group" aria-label="Theme">
          {THEMES.map((t) => (
            <button
              key={t.id}
              type="button"
              tabIndex={open ? 0 : -1}
              className={theme === t.id ? 'on' : ''}
              aria-pressed={theme === t.id}
              title={t.label}
              aria-label={t.label}
              onClick={() => onTheme(t.id)}
            >
              <Icon name={t.icon} size={16} />
            </button>
          ))}
        </div>
        <div className="hamb-sep" />
        <button
          type="button"
          role="menuitem"
          tabIndex={open ? 0 : -1}
          onClick={() => {
            setOpen(false)
            onExpand()
          }}
        >
          <Icon name="down" size={17} />
          Show header
        </button>
        <button
          type="button"
          role="menuitem"
          tabIndex={open ? 0 : -1}
          onClick={() => {
            setOpen(false)
            onSignOut()
          }}
        >
          Sign out
        </button>
      </div>
    </div>
  )
}
