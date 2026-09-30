export type Quality =
  | 'OK'
  | 'NO_ANSWER'
  | 'NOT_DOWNLINKED'
  | 'BLOCK_INCOMPLETE'
  | 'WARMUP'

export interface Row {
  parameter_id: number
  subsystem: string
  message: string
  entry: string
  tier: 'VH' | 'H' | 'M' | 'L'
  name: string
  unit: string | null
  data_type: string
  byte_aligned: number
  value: string | null
  value_num: number | null
  quality: Quality | null
  master_frame: number | null
  subframe: number | null
  spacecraft_time: string | null
  received_at: string | null
}

export interface FeedStatus {
  feedHost: string
  feedPort: number
  connected: boolean
  subframesReceived: number
  masterFrame: number
  warmupMasterFrame: number
  spacecraftTime: string | null
  lutCrc16: number
  lutEntries: number
  decodedValues: number
  incompleteBlocks: number
  subframeGaps: number
  historyRows: number
  /** ISO-8601 UTC instant the newest subframe was received (null before the first). */
  lastReceivedAt?: string | null
  /** Subframes whose header parsed OK since backend start. */
  framesValid?: number
  /** Headers rejected (bad magic / implausible length) since backend start. */
  framesRejected?: number
  /** Subframes received in the current session. */
  sessionSubframes?: number
}

export interface SimStatus {
  running: boolean
  adopted: boolean
  pid: number | null
  startedAt: string | null
  xdosHome: string
  processes: string[]
  error: string | null
}

export interface Status {
  feed: FeedStatus
  simulator: SimStatus
}
