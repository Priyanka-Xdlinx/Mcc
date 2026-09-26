import { useEffect, useState } from 'react'
import { AppHeader, TABS, type Tab } from './components/AppHeader'
import { StatusBar } from './components/StatusBar'
import { Dashboard, type TelemetryFilter } from './components/Dashboard'
import { TelemetryTable } from './components/TelemetryTable'
import { ChartsView } from './components/ChartsView'
import { DecodeView } from './components/DecodeView'
import { useTelemetryStream } from './hooks/useTelemetryStream'
import { useTheme } from './theme'

export default function App() {
  const { rows, status, live, lastMsgAt, movers, reload, refreshStatus } = useTelemetryStream()
  const theme = useTheme()
  const [tab, setTab] = useState<Tab>('dashboard')
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
        onTab={setTab}
        status={status}
        live={live}
        theme={theme.choice}
        onTheme={theme.set}
        onFeedChanged={() => {
          void refreshStatus()
          void reload()
        }}
      />
      {tab !== 'dashboard' && <StatusBar status={status} live={live} />}

      {tab === 'dashboard' && (
        <Dashboard
          rows={rows}
          status={status}
          live={live}
          lastMsgAt={lastMsgAt}
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
          initialSubsystem={filter.subsystem}
          initialOnlyProblems={filter.onlyProblems}
        />
      )}
      {tab === 'charts' && (
        <ChartsView rows={rows} live={live} movers={movers} initial={chartIds} />
      )}
      {tab === 'decode' && <DecodeView rows={rows} live={live} />}
    </div>
  )
}
