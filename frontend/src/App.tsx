import { useEffect, useState } from 'react'
import { AppHeader, TABS, type Tab } from './components/AppHeader'
import { StatusBar } from './components/StatusBar'
import { TelemetryTable } from './components/TelemetryTable'
import { ChartsDashboard } from './ox/ChartsDashboard'
import { DecodeView } from './components/DecodeView'
import { Docs } from './components/Docs'
import { DisplayDesk } from './components/display/DisplayDesk'

import { useTelemetryStream } from './hooks/useTelemetryStream'
import { useTheme } from './theme'
import type { AuthUser } from './auth/auth'

function tabFromHash(): Tab {
  const id = location.hash.replace(/^#/, '').split('/')[0]
  return (TABS.some((t) => t.id === id) ? id : 'telemetry') as Tab
}

export default function App({
  user,
  onUser,
  onSignOut,
  signingOut,
}: {
  user: AuthUser
  onUser: (u: AuthUser) => void
  onSignOut: () => void
  signingOut: boolean
}) {
  const { rows, status, live, backendUp, lastMsgAt, lastFrame, reload, refreshStatus } =
    useTelemetryStream()
  const theme = useTheme()
  // The address carries the page (#telemetry, #docs/database, ...) so a page can
  // be bookmarked, opened directly, or reached with the browser's Back button.
  const [tab, setTab] = useState<Tab>(() => tabFromHash())
  useEffect(() => {
    const on = () => setTab(tabFromHash())
    window.addEventListener('hashchange', on)
    return () => window.removeEventListener('hashchange', on)
  }, [])
  useEffect(() => {
    if (tabFromHash() === tab) return // already there (keeps #docs/database intact)
    history.pushState(null, '', `#${tab}`)
  }, [tab])
  const [chartIds] = useState<number[]>([])
  // Display page focus mode: header and toolbar hidden, windows highlighted.
  const [focusMode, setFocusMode] = useState(false)
  const focused = focusMode && tab === 'display'

  useEffect(() => {
    const t = TABS.find((x) => x.id === tab)
    document.title = `${t?.label ?? 'MCC'} · OrbLinx MCC`
  }, [tab])

  return (
    <div className="app">
      <AppHeader
        tab={tab}
        onTab={(t) => {
          setTab(t)
        }}
        collapsed={focused}
        onToggleCollapse={tab === 'display' ? () => setFocusMode((v) => !v) : undefined}
        lastMsgAt={lastMsgAt}
        status={backendUp === false ? null : status}
        live={live}
        theme={theme.choice}
        onTheme={theme.set}
        user={user}
        onUser={onUser}
        onSignOut={onSignOut}
        signingOut={signingOut}
        onFeedChanged={() => {
          void refreshStatus()
          void reload()
        }}
      />
      {backendUp === false && (
        <div className="banner" role="alert">
          <b>The MCC backend is not reachable.</b> Start it with <code>./start.sh --sim</code> in
          mcc-app; this page reconnects by itself.
        </div>
      )}
      {(tab === 'telemetry' || tab === 'charts' || tab === 'decode') && <StatusBar status={status} live={live} />}

      {tab === 'telemetry' && (
        <TelemetryTable
          rows={rows}
          backendUp={backendUp}
        />
      )}
      {tab === 'charts' && (
        <ChartsDashboard rows={rows} lutCrc={status?.feed.lutCrc16 ?? (backendUp === false && rows.length ? 0 : null)} pick={chartIds} />
      )}
      {tab === 'decode' && <DecodeView rows={rows} live={live} />}
      {tab === 'display' && (
        <DisplayDesk rows={rows} status={status} live={live} lastMsgAt={lastMsgAt} lastFrame={lastFrame} focused={focused} />
      )}
      {tab === 'docs' && <Docs theme={theme.choice} />}
    </div>
  )
}
