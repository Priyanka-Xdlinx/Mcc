/**
 * Hex viewer with an optional highlighted byte range.
 *
 * The highlight is the whole point: it is what makes "offset 0, length 4" and
 * "bit 68, width 4" concrete rather than a claim.
 */
export function HexDump({
  title,
  bytes,
  baseOffset = 0,
  markFrom,
  markTo,
  width = 16,
  maxRows = 24,
}: {
  title?: string
  bytes: string[]
  baseOffset?: number
  markFrom?: number | null
  markTo?: number | null
  width?: number
  maxRows?: number
}) {
  const rows: string[][] = []
  for (let i = 0; i < bytes.length; i += width) rows.push(bytes.slice(i, i + width))

  // A 1400-byte block would bury the interesting bytes; keep the window around
  // the highlight when there is one.
  let start = 0
  let truncated = false
  if (rows.length > maxRows) {
    truncated = true
    if (markFrom != null) {
      const markRow = Math.floor(markFrom / width)
      start = Math.max(0, Math.min(markRow - Math.floor(maxRows / 2), rows.length - maxRows))
    }
  }
  const shown = rows.slice(start, start + (truncated ? maxRows : rows.length))

  return (
    <div className="hexdump">
      {title && <div className="hex-title">{title}</div>}
      {truncated && start > 0 && <div className="hex-skip">… {start * width} bytes above</div>}
      <div className="hex-body">
        {shown.map((row, ri) => {
          const rowStart = (start + ri) * width
          return (
            <div className="hex-row" key={ri}>
              <span className="hex-off">{String(baseOffset + rowStart).padStart(5, ' ')}</span>
              {row.map((b, ci) => {
                const pos = rowStart + ci
                const on =
                  markFrom != null && markTo != null && pos >= markFrom && pos < markTo
                return (
                  <span className={on ? 'hex-b on' : 'hex-b'} key={ci}>
                    {b}
                  </span>
                )
              })}
            </div>
          )
        })}
      </div>
      {truncated && start + maxRows < rows.length && (
        <div className="hex-skip">… {(rows.length - start - maxRows) * width} bytes below</div>
      )}
    </div>
  )
}
