import { useState } from 'react'
import { StatusBar } from './components/StatusBar'
import { SimControl } from './components/SimControl'
import { TelemetryTable } from './components/TelemetryTable'
import { ChartsView } from './components/ChartsView'
import { DecodeView } from './components/DecodeView'
import { useTelemetryStream } from './hooks/useTelemetryStream'

type Tab = 'telemetry' | 'charts' | 'decode'

const TABS: { id: Tab; label: string; hint: string }[] = [
  { id: 'telemetry', label: 'Telemetry', hint: 'every parameter the LUT downlinks, live' },
  { id: 'charts', label: 'Charts', hint: 'values over time' },
  { id: 'decode', label: 'Decode trace', hint: 'Layer 1 and Layer 2, byte by byte' },
]

export default function App() {
  const { rows, status, live, reload, refreshStatus } = useTelemetryStream()
  const [tab, setTab] = useState<Tab>('telemetry')

  return (
    <div className="app">
      <StatusBar status={status} live={live} />

      <div className="tabbar">
        {TABS.map((t) => (
          <button
            key={t.id}
            className={`tab ${tab === t.id ? 'on' : ''}`}
            onClick={() => setTab(t.id)}
            title={t.hint}
          >
            {t.label}
          </button>
        ))}
        <SimControl
          status={status}
          onChanged={() => {
            void refreshStatus()
            void reload()
          }}
        />
      </div>

      {tab === 'telemetry' && <TelemetryTable rows={rows} />}
      {tab === 'charts' && <ChartsView rows={rows} live={live} />}
      {tab === 'decode' && <DecodeView rows={rows} live={live} />}
    </div>
  )
}
