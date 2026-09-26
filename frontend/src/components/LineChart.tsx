import { useMemo, useRef, useState } from 'react'

export interface Point {
  x: number // monotonic frame index: masterFrame * 16 + subframe
  y: number
  label: string // spacecraft time, for the tooltip
}

export interface Series {
  id: number
  name: string
  color: string
  points: Point[]
}

/**
 * Line chart for telemetry over time.
 *
 * One y-axis only. Series of different units are never drawn together - the
 * caller facets them into separate charts - because a second scale makes two
 * unrelated shapes look comparable.
 */
export function LineChart({
  series,
  unit,
  height = 260,
  step = false,
  area = false,
  compact = false,
}: {
  series: Series[]
  unit: string | null
  height?: number
  /** Hold the previous value until the next sample: right for states, wrong for continuous quantities. */
  step?: boolean
  /** Fill under a single series. Only ever for one - overlapping fills hide each other. */
  area?: boolean
  /** Small-multiple mode: no direct labels or legend, tighter margins. */
  compact?: boolean
}) {
  const [hoverX, setHoverX] = useState<number | null>(null)
  const wrap = useRef<HTMLDivElement>(null)

  const PAD = compact
    ? { top: 10, right: 12, bottom: 20, left: 52 }
    : { top: 16, right: 96, bottom: 28, left: 64 }
  const W = compact ? 420 : 900
  const H = height

  const { xs, xMin, xMax, yMin, yMax } = useMemo(() => {
    const all = series.flatMap((s) => s.points)
    const xsSet = Array.from(new Set(all.map((p) => p.x))).sort((a, b) => a - b)
    let lo = Math.min(...all.map((p) => p.y))
    let hi = Math.max(...all.map((p) => p.y))
    if (!isFinite(lo) || !isFinite(hi)) {
      lo = 0
      hi = 1
    }
    if (lo === hi) {
      // A dead-flat series still deserves a readable band rather than a
      // zero-height axis — which is most of this simulator's telemetry.
      const pad = Math.abs(lo) * 0.05 || 1
      lo -= pad
      hi += pad
    } else {
      const pad = (hi - lo) * 0.08
      lo -= pad
      hi += pad
    }
    return {
      xs: xsSet,
      xMin: xsSet[0] ?? 0,
      xMax: xsSet[xsSet.length - 1] ?? 1,
      yMin: lo,
      yMax: hi,
    }
  }, [series])

  if (series.length === 0 || series.every((s) => s.points.length === 0)) {
    return <div className="chart-empty">no samples yet for this parameter</div>
  }

  const px = (x: number) =>
    PAD.left + ((x - xMin) / Math.max(1, xMax - xMin)) * (W - PAD.left - PAD.right)
  const py = (y: number) =>
    PAD.top + (1 - (y - yMin) / (yMax - yMin)) * (H - PAD.top - PAD.bottom)

  const ticks = compact ? 2 : 4
  const yTicks = Array.from({ length: ticks + 1 }, (_, i) => yMin + ((yMax - yMin) * i) / ticks)

  const fmt = (v: number) => {
    const span = Math.abs(yMax - yMin)
    if (span >= 1000 || Math.abs(v) >= 100000) return v.toExponential(2)
    if (span < 1) return v.toFixed(3)
    return v.toFixed(2)
  }

  const onMove = (e: React.MouseEvent<SVGSVGElement>) => {
    const r = e.currentTarget.getBoundingClientRect()
    const rel = ((e.clientX - r.left) / r.width) * W
    if (rel < PAD.left || rel > W - PAD.right) return setHoverX(null)
    const frac = (rel - PAD.left) / (W - PAD.left - PAD.right)
    const target = xMin + frac * (xMax - xMin)
    let best = xs[0]
    for (const x of xs) if (Math.abs(x - target) < Math.abs(best - target)) best = x
    setHoverX(best)
  }

  const hovered = hoverX == null ? [] : series
    .map((s) => ({ s, p: s.points.find((p) => p.x === hoverX) }))
    .filter((h): h is { s: Series; p: Point } => !!h.p)

  return (
    <div className="chart" ref={wrap}>
      <svg
        viewBox={`0 0 ${W} ${H}`}
        onMouseMove={onMove}
        onMouseLeave={() => setHoverX(null)}
        role="img"
        aria-label={`${series.map((s) => s.name).join(', ')} over time`}
      >
        {/* recessive grid */}
        {yTicks.map((v, i) => (
          <g key={i}>
            <line
              x1={PAD.left}
              x2={W - PAD.right}
              y1={py(v)}
              y2={py(v)}
              className="grid"
            />
            <text x={PAD.left - 10} y={py(v) + 4} className="axis" textAnchor="end">
              {fmt(v)}
            </text>
          </g>
        ))}

        {/* x axis: frame numbers at the ends, so the span is readable without clutter */}
        <text x={PAD.left} y={H - 8} className="axis" textAnchor="start">
          MF {Math.floor(xMin / 16)}·{(xMin % 16) + 1}
        </text>
        <text x={W - PAD.right} y={H - 8} className="axis" textAnchor="end">
          MF {Math.floor(xMax / 16)}·{(xMax % 16) + 1}
        </text>
        {unit && (
          <text x={PAD.left - 10} y={PAD.top - 4} className="axis unit" textAnchor="end">
            {unit}
          </text>
        )}

        {hoverX != null && (
          <line
            x1={px(hoverX)}
            x2={px(hoverX)}
            y1={PAD.top}
            y2={H - PAD.bottom}
            className="crosshair"
          />
        )}

        {series.map((s) => {
          const pts = s.points.filter((p) => isFinite(p.y))
          if (pts.length === 0) return null
          const d = pts
            .map((p, i) => {
              const X = px(p.x).toFixed(1)
              const Y = py(p.y).toFixed(1)
              if (i === 0) return `M${X},${Y}`
              // A state holds its value until it changes; drawing a slope between
              // two enum codes invents readings that never existed.
              return step ? `H${X}V${Y}` : `L${X},${Y}`
            })
            .join(' ')
          const last = pts[pts.length - 1]
          const first = pts[0]
          return (
            <g key={s.id}>
              {area && series.length === 1 && (
                <path
                  d={`${d} L${px(last.x).toFixed(1)},${py(yMin).toFixed(1)} L${px(first.x).toFixed(1)},${py(yMin).toFixed(1)} Z`}
                  style={{ fill: s.color }}
                  opacity={0.12}
                  stroke="none"
                />
              )}
              <path d={d} className="line" style={{ stroke: s.color }} />
              {pts.length === 1 && (
                <circle cx={px(last.x)} cy={py(last.y)} r={4} style={{ fill: s.color }} />
              )}
              {/* direct label at the last point, for small series counts */}
              {!compact && series.length <= 4 && (
                <text
                  x={px(last.x) + 8}
                  y={py(last.y) + 4}
                  className="direct-label"
                  style={{ fill: s.color }}
                >
                  {s.name.length > 14 ? s.name.slice(0, 13) + '…' : s.name}
                </text>
              )}
              {hoverX != null &&
                pts.find((p) => p.x === hoverX) && (
                  <circle
                    cx={px(hoverX)}
                    cy={py(pts.find((p) => p.x === hoverX)!.y)}
                    r={4.5}
                    style={{ fill: s.color }}
                    className="hover-dot"
                  />
                )}
            </g>
          )
        })}
      </svg>

      {hovered.length > 0 && (
        <div className="tooltip" style={{ left: `${(px(hoverX!) / W) * 100}%` }}>
          <div className="tt-head">{hovered[0].p.label}</div>
          {hovered.map((h) => (
            <div className="tt-row" key={h.s.id}>
              <span className="swatch" style={{ background: h.s.color }} />
              <span className="tt-name">{h.s.name}</span>
              <span className="tt-val">
                {fmt(h.p.y)}
                {unit ? ` ${unit}` : ''}
              </span>
            </div>
          ))}
        </div>
      )}

      {/* identity is never colour alone: a legend is always present for 2+ series */}
      {!compact && series.length > 1 && (
        <div className="legend">
          {series.map((s) => (
            <span className="legend-item" key={s.id}>
              <span className="swatch" style={{ background: s.color }} />
              {s.name}
            </span>
          ))}
        </div>
      )}
    </div>
  )
}
