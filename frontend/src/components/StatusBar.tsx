import type { Status } from '../types'
import { crcHex } from '../format'

export function StatusBar({ status, live }: { status: Status | null; live: boolean }) {
  const f = status?.feed
  return (
    <div className="statusbar">
      <div className="stat">
        <div className="k">Feed</div>
        <div className="v">
          <span className={`dot ${f?.connected ? 'on' : 'off'}`} />
          {f ? `${f.feedHost}:${f.feedPort}` : '—'}
        </div>
      </div>

      <div className="stat">
        <div className="k">Master frame</div>
        <div className="v">{f && f.masterFrame >= 0 ? `#${f.masterFrame}` : '—'}</div>
      </div>

      <div className="stat">
        <div className="k">Subframes</div>
        <div className="v">{f?.subframesReceived ?? 0}</div>
      </div>

      <div className="stat">
        <div className="k">Spacecraft time</div>
        <div className="v">{f?.spacecraftTime ?? '—'}</div>
      </div>

      {/* Position inside a tier is the only link between the OBC and the ground,
          so the CRC of the table in flight is worth keeping on screen. */}
      <div className="stat">
        <div className="k">LUT CRC-16</div>
        <div className="v" title={f ? String(f.lutCrc16) : undefined}>{crcHex(f?.lutCrc16)}</div>
      </div>

      <div className="stat">
        <div className="k">Lost subframes</div>
        <div className="v">{f?.subframeGaps ?? 0}</div>
      </div>

      <div className="stat">
        <div className="k">Readings stored</div>
        <div className="v">{(f?.historyRows ?? 0).toLocaleString()}</div>
      </div>

      <div className="stat">
        <div className="k">Dashboard stream</div>
        <div className="v">
          <span className={`dot ${live ? 'on' : 'off'}`} />
          {live ? 'live' : 'offline'}
        </div>
      </div>
    </div>
  )
}
