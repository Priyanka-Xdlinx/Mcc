import { Suspense, lazy, useEffect, useState } from 'react'
import { AppHeader, TABS, type Tab } from './components/AppHeader'
import { StatusBar } from './components/StatusBar'
import { Dashboard, type TelemetryFilter } from './components/Dashboard'
import { TelemetryTable } from './components/TelemetryTable'
import { ChartsDashboard } from './ox/ChartsDashboard'
import { DecodeView } from './components/DecodeView'
import { Docs } from './components/Docs'
import { XdlinxPage } from './components/XdlinxPage'
import { DisplayDesk } from './components/display/DisplayDesk'

// three.js is large, so the 3D page loads only when it is opened.
const OrbitPage = lazy(() => import('./components/orbit/OrbitPage'))
import { useTelemetryStream } from './hooks/useTelemetryStream'
import { useTheme } from './theme'

function tabFromHash(): Tab {
  const id = location.hash.replace(/^#/, '').split('/')[0]
  return (TABS.some((t) => t.id === id) ? id : 'dashboard') as Tab
}

export default function App() {
  const { rows, status, live, backendUp, lastMsgAt, lastFrame, movers, reload, refreshStatus } =
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
    history.pushState(null, '', tab === 'dashboard' ? location.pathname : `#${tab}`)
  }, [tab])
  const [filter, setFilter] = useState<TelemetryFilter & { n: number }>({ n: 0 })
  const [chartIds, setChartIds] = useState<number[]>([])

  useEffect(() => {
    const t = TABS.find((x) => x.id === tab)
    document.title = `${t?.label ?? 'MCC'} · OrbLinx MCC`
  }, [tab])

  return (
    <div className="app">
      <AppHeader
        tab={tab}
        onTab={(t) => {
          // The nav always opens a page unfiltered; filters come only from a
          // dashboard link that asked for them.
          if (t === 'telemetry') setFilter((prev) => ({ n: prev.n + 1 }))
          setTab(t)
        }}
        lastMsgAt={lastMsgAt}
        status={backendUp === false ? null : status}
        live={live}
        theme={theme.choice}
        onTheme={theme.set}
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

      {tab === 'dashboard' && (
        <Dashboard
          rows={rows}
          status={status}
          live={live}
          lastMsgAt={lastMsgAt}
          lastFrame={lastFrame}
          movers={movers}
          onOpenTelemetry={(f) => {
            setFilter((prev) => ({ ...f, n: prev.n + 1 }))
            setTab('telemetry')
          }}
          onOpenCharts={(ids) => {
            setChartIds(ids)
            setTab('charts')
          }}
        />
      )}
      {tab === 'telemetry' && (
        <TelemetryTable
          key={filter.n}
          rows={rows}
          backendUp={backendUp}
          initialSubsystem={filter.subsystem}
          initialOnlyProblems={filter.onlyProblems}
        />
      )}
      {tab === 'charts' && (
        <ChartsDashboard rows={rows} lutCrc={status?.feed.lutCrc16 ?? (backendUp === false && rows.length ? 0 : null)} pick={chartIds} />
      )}
      {tab === 'decode' && <DecodeView rows={rows} live={live} />}
      {tab === 'display' && (
        <DisplayDesk rows={rows} status={status} live={live} lastMsgAt={lastMsgAt} lastFrame={lastFrame} />
      )}
      {tab === 'docs' && <Docs theme={theme.choice} />}
      {tab === 'xdlinx' && <XdlinxPage />}
      {tab === 'track' && (
        <Suspense fallback={<div className="empty">Loading the 3D view…</div>}>
          <OrbitPage rows={rows} live={live} />
        </Suspense>
      )}
    </div>
  )
}
