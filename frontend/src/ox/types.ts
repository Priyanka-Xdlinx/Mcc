// Local stand-ins for the Orblinx platform's API types, filled from mcc-app telemetry rows.
import type { Dtype } from './lib/format'

/** One plottable parameter, as the widget framework expects a "channel". */
export interface Channel {
  /** Short id used in widgets and formulas, e.g. "P1980". */
  key: string
  name: string
  unit: string | null
  dtype: Dtype
  group: string | null
  hidden: boolean
  enumLabels?: Record<string, string> | null
  hexDigits?: number | null
}

export interface Stat {
  min: number
  max: number
  avg: number
  count: number
}

export type Level = 'ok' | 'warn' | 'crit'

/** A section of the default dashboard: [widget id, widget type] pairs. */
export interface RoleSection {
  key: string
  title: string
  widgets: [string, string][]
}
