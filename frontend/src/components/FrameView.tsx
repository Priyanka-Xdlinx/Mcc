import { useEffect, useMemo, useState } from 'react'
import { TraceModal } from './TraceModal'

interface Pick {
  src: number
  size: number
  block: number
  payloadFrom: number | null
  payloadTo: number | null
}
interface Param {
  seq: number | null
  uid: string | null
  name: string
  subsystem: string
  parameterId: number | null
  byteFrom?: number
  byteTo?: number
  bitOffset?: number
  bitLength?: number
  byteAligned?: boolean
  dataType?: string
  unit?: string | null
  scale?: number | null
  endian?: string
  rawHex?: string
  rawBits?: string
  rawComplete?: boolean
  value: string | null
  quality: string | null
}
interface Entry {
  entry: string
  subsystem: string
  message: string
  tmRawLen: number
  bitAddressed: boolean
  picks: Pick[]
  parameters: Param[]
  replyHex: string[]
  replyHave: number[]
}
interface TierBlock {
  tier: 'VH' | 'H' | 'M' | 'L'
  segOffset: number
  segBytes: number
  blockSpan: number
  sliceIndex: number
  completesHere: boolean
  blockComplete: boolean
  slicesPresent: number[]
  entries: Entry[]
  hex: string[]
}
export interface FrameDecode {
  error?: string
  masterFrame: number
  subframe: number
  spacecraftTime: string
  payloadLength: number
  tiers: TierBlock[]
  legend: { entry: string; subsystem: string; tier: string }[]
  byteOwner: number[]
  payloadHex: string[]
  totalParameters: number
  decodedOk: number
}

const TIER_OF_BYTE = (i: number): 'VH' | 'H' | 'M' | 'L' | 'rsv' =>
  i < 400 ? 'VH' : i < 800 ? 'H' : i < 1100 ? 'M' : i < 1275 ? 'L' : 'rsv'

const COLS = 64

export function FrameView({
  mf,
  sf,
  live,
  refreshMs,
}: {
  mf: number | null
  sf: number | null
  live: boolean
  refreshMs: number
}) {
  const [data, setData] = useState<FrameDecode | null>(null)
  const [hover, setHover] = useState<number | null>(null)
  const [open, setOpen] = useState<string | null>(null)
  const [openParam, setOpenParam] = useState<number | null>(null)
  const [onlyDecoded, setOnlyDecoded] = useState(false)

  useEffect(() => {
    let dead = false
    const load = async () => {
      const qs = mf !== null && sf !== null ? `?mf=${mf}&sf=${sf}` : ''
      const r = await fetch(`/api/frame-decode${qs}`)
      const j = await r.json()
      if (!dead) setData(j)
    }
    void load()
    if (mf === null && live) {
      const t = setInterval(load, refreshMs)
      return () => {
        dead = true
        clearInterval(t)
      }
    }
    return () => {
      dead = true
    }
  }, [mf, sf, live, refreshMs])

  const rows = useMemo(() => {
    if (!data) return []
    const out: number[][] = []
    for (let i = 0; i < data.payloadLength; i += COLS) {
      out.push(Array.from({ length: Math.min(COLS, data.payloadLength - i) }, (_, k) => i + k))
    }
    return out
  }, [data])

  if (!data) return <div className="empty">loading frame…</div>
  if (data.error) return <div className="empty">{data.error}</div>

  const hoveredOwner =
    hover !== null && data.byteOwner[hover] >= 0 ? data.legend[data.byteOwner[hover]] : null

  return (
    <div className="frameview">
      {/* ── the payload, byte by byte ─────────────────────────────────────── */}
      <section className="fv-card">
        <div className="fv-head">
          <div>
            <div className="fv-title">The 1280-byte payload of this subframe</div>
            <div className="fv-sub">
              Every byte is coloured by the tier it belongs to. A solid byte was
              claimed by a lookup-table pick; a faint one is space no parameter
              asked for, sent as zero.
            </div>
          </div>
          <div className="fv-stat">
            <div className="dv-k">Parameters in frame</div>
            <div className="fv-stat-v">
              {data.decodedOk} / {data.totalParameters}
            </div>
          </div>
        </div>

        <div className="bytemap-legend">
          {(['VH', 'H', 'M', 'L'] as const).map((t) => (
            <span key={t} className="bl-item">
              <span className={`bl-sw t-${t}`} /> {t}
            </span>
          ))}
          <span className="bl-item">
            <span className="bl-sw unclaimed" /> unclaimed
          </span>
          <span className="bl-item">
            <span className="bl-sw reserved" /> reserved
          </span>
          <span className="bl-hover">
            {hover === null ? (
              'hover a byte'
            ) : (
              <>
                byte <b>{hover}</b> · {TIER_OF_BYTE(hover)} ·{' '}
                <b>{data.payloadHex[hover]}</b>
                {hoveredOwner ? (
                  <>
                    {' '}
                    · {hoveredOwner.entry} ({hoveredOwner.subsystem})
                  </>
                ) : data.byteOwner[hover] === -2 ? (
                  ' · reserved'
                ) : (
                  ' · not claimed by any pick'
                )}
              </>
            )}
          </span>
        </div>

        <div className="bytemap" onMouseLeave={() => setHover(null)}>
          {rows.map((row, ri) => (
            <div className="bm-row" key={ri}>
              <span className="bm-off">{row[0]}</span>
              {row.map((i) => {
                const t = TIER_OF_BYTE(i)
                const o = data.byteOwner[i]
                const cls =
                  o === -2 ? 'reserved' : o >= 0 ? `t-${t} picked` : `t-${t} unclaimed`
                return (
                  <span
                    key={i}
                    className={`bm-b ${cls} ${hover === i ? 'hov' : ''}`}
                    onMouseEnter={() => setHover(i)}
                  />
                )
              })}
            </div>
          ))}
        </div>
      </section>

      {/* ── tiers and their entries ───────────────────────────────────────── */}
      <div className="fv-tiers">
        {data.tiers.map((t) => (
          <section className="fv-card" key={t.tier}>
            <div className="fv-head">
              <div>
                <div className="fv-title">
                  <span className={`tier ${t.tier}`}>{t.tier}</span>{' '}
                  {t.entries.length} entries ·{' '}
                  {t.entries.reduce((n, e) => n + e.parameters.length, 0)} parameters
                </div>
                <div className="fv-sub">
                  payload[{t.segOffset}:{t.segOffset + t.segBytes}] · slice{' '}
                  {t.sliceIndex + 1} of {t.blockSpan}
                  {t.blockComplete ? (
                    <span className="ok-chip"> block complete</span>
                  ) : (
                    <span className="warn-chip">
                      {' '}
                      block filling — {t.blockSpan - t.slicesPresent.length} slice(s) missing
                    </span>
                  )}
                </div>
              </div>
            </div>

            <div className="entries">
              {t.entries.map((e) => {
                const id = `${t.tier}-${e.entry}`
                const isOpen = open === id
                const ok = e.parameters.filter((p) => p.quality === 'OK').length
                return (
                  <div className={`entry ${isOpen ? 'open' : ''}`} key={id}>
                    <button className="entry-head" onClick={() => setOpen(isOpen ? null : id)}>
                      <span className="caret">{isOpen ? '▾' : '▸'}</span>
                      <span className="e-id">{e.entry}</span>
                      <span className="e-sub">{e.subsystem}</span>
                      <span className="e-msg" title={e.message}>
                        {e.message}
                      </span>
                      <span className="e-bytes">
                        {e.picks.reduce((n, p) => n + p.size, 0)}/{e.tmRawLen} B
                      </span>
                      <span className="e-count">
                        {ok}/{e.parameters.length}
                      </span>
                    </button>

                    {isOpen && (
                      <div className="entry-body">
                        <div className="picks">
                          {e.picks.map((p, i) => (
                            <span className="pick-chip" key={i}>
                              block[{p.block}:{p.block + p.size}] → reply[{p.src}:
                              {p.src + p.size}]
                              {p.payloadFrom !== null && (
                                <em>
                                  {' '}
                                  · in this subframe at {p.payloadFrom}–{p.payloadTo}
                                </em>
                              )}
                            </span>
                          ))}
                          {e.bitAddressed && <span className="pick-chip bit">bit-addressed</span>}
                        </div>
                        {e.replyHex && (
                          <div className="replydump">
                            <div className="hex-title">
                              reconstructed {e.message} reply — {e.tmRawLen} bytes,
                              {' '}{e.replyHave.reduce((n, x) => n + x, 0)} downlinked
                              {'  '}<span className="dimcol">(faint = never sent)</span>
                            </div>
                            <div className="hex-body">
                              {Array.from(
                                { length: Math.ceil(e.replyHex.length / 16) },
                                (_, ri) => (
                                  <div className="hex-row" key={ri}>
                                    <span className="hex-off">
                                      {String(ri * 16).padStart(5, ' ')}
                                    </span>
                                    {e.replyHex
                                      .slice(ri * 16, ri * 16 + 16)
                                      .map((b, ci) => (
                                        <span
                                          key={ci}
                                          className={
                                            e.replyHave[ri * 16 + ci] ? 'hex-b' : 'hex-b absent'
                                          }
                                        >
                                          {b}
                                        </span>
                                      ))}
                                  </div>
                                ),
                              )}
                            </div>
                          </div>
                        )}
                        <table className="ptable">
                          <thead>
                            <tr>
                              <th>#</th>
                              <th>Parameter</th>
                              <th>Bytes</th>
                              <th>Bits</th>
                              <th>Type</th>
                              <th>Binary</th>
                              <th style={{ textAlign: 'right' }}>Value</th>
                              <th>Quality</th>
                              <th />
                            </tr>
                          </thead>
                          <tbody>
                            {e.parameters
                              .filter((p) => !onlyDecoded || p.quality === 'OK')
                              .flatMap((p, i) => [
                                <tr
                                  key={i}
                                  className={p.parameterId != null ? 'traceable' : ''}
                                  title={p.parameterId != null
                                    ? 'Trace this parameter byte by byte'
                                    : undefined}
                                  onClick={() =>
                                    p.parameterId != null && setOpenParam(p.parameterId)
                                  }
                                >
                                  <td className="num dimcol">{p.seq ?? '—'}</td>
                                  <td className="p-name">
                                    {p.name}
                                    {p.byteAligned === false && (
                                      <span className="nba" title="not byte aligned">
                                        {' '}
                                        ∿
                                      </span>
                                    )}
                                    {p.unit ? <span className="p-unit"> {p.unit}</span> : null}
                                    {p.uid && <div className="p-uid">{p.uid}</div>}
                                  </td>
                                  <td className="num dimcol">
                                    {p.byteFrom !== undefined
                                      ? `${p.byteFrom}–${p.byteTo}`
                                      : '—'}
                                  </td>
                                  <td className="num dimcol">
                                    {p.bitOffset !== undefined
                                      ? `${p.bitOffset}+${p.bitLength}`
                                      : '—'}
                                  </td>
                                  <td className="dimcol">{p.dataType ?? '—'}</td>
                                  <td className="raw bits">{p.rawBits ?? '—'}</td>
                                  <td className="num">{p.value ?? '—'}</td>
                                  <td className={`q ${p.quality ?? ''}`}>{p.quality ?? '—'}</td>
                                  <td className="tracecell">
                                    <span className="trace-btn">trace</span>
                                  </td>
                                </tr>,
                              ])}
                          </tbody>
                        </table>
                      </div>
                    )}
                  </div>
                )
              })}
            </div>
          </section>
        ))}
      </div>

      {openParam !== null && (
        <TraceModal
          parameterId={openParam}
          mf={mf}
          sf={sf}
          onClose={() => setOpenParam(null)}
        />
      )}

      <label className="fv-foot">
        <input
          type="checkbox"
          checked={onlyDecoded}
          onChange={(e) => setOnlyDecoded(e.target.checked)}
        />
        hide parameters that did not decode
      </label>
    </div>
  )
}
