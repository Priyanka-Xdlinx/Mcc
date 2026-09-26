/**
 * A constant series is not a line chart.
 *
 * Most of this simulator's telemetry never moves - 747 of 755 parameters hold
 * one value forever. Drawing that as a flat line implies a measurement that
 * could have varied and happened not to; a stat tile says plainly that nothing
 * changed, and over how many samples.
 */
export function StatTile({
  name,
  value,
  unit,
  samples,
  color,
}: {
  name: string
  value: string
  unit: string | null
  samples: number
  color: string
}) {
  return (
    <div className="stattile">
      <div className="st-head">
        <span className="swatch" style={{ background: color }} />
        <span className="st-name" title={name}>{name}</span>
      </div>
      <div className="st-value">
        {value}
        {unit ? <span className="st-unit"> {unit}</span> : null}
      </div>
      <div className="st-note">unchanged across {samples} samples</div>
    </div>
  )
}
