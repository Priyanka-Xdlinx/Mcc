import Chart from "../../../components/Chart.jsx";
import { fmtNumber } from "../../../utils/chart.js";
import { displayValue, PERIOD_S } from "../../../utils/format.js";
import { isPlottable, MAX_SERIES, stats, WIDGET_TYPES } from "../utils/dashboard.js";
import ParameterSelect from "./ParameterSelect.jsx";

/** A reading counts as stale after this many of its part's periods without an update. */
const STALE_PERIODS = 3;

function isStale(row, now, live) {
  if (!live || !row?.received_at) return false;
  const lastReceived = Date.parse(row.received_at);
  return Number.isFinite(lastReceived) && now - lastReceived > STALE_PERIODS * 1000 * (PERIOD_S[row.tier] ?? 8);
}

function PlotBody({ rows, series }) {
  return (
    <Chart
      x={series.x}
      ys={series.ys}
      series={rows.map((r, i) => ({ label: r?.name ?? `#${i}`, unit: r?.unit ?? "" }))}
      height={130}
      spanGaps
      ariaLabel={`${rows.map((r) => r?.name).join(", ")} over time`}
    />
  );
}

function StatBody({ row, y }) {
  const s = stats(y);
  return (
    <div className="cw-stat">
      <div className="cw-big">
        {row?.value_num != null ? fmtNumber(row.value_num) : "—"}
        {row?.unit && <small> {row.unit}</small>}
      </div>
      <dl>
        <dt>min</dt>
        <dd>{s ? fmtNumber(s.min) : "—"}</dd>
        <dt>max</dt>
        <dd>{s ? fmtNumber(s.max) : "—"}</dd>
        <dt>mean</dt>
        <dd>{s ? fmtNumber(s.avg) : "—"}</dd>
      </dl>
    </div>
  );
}

function ValueBody({ row }) {
  return (
    <div className="cw-value" title={row?.value ?? undefined}>
      {displayValue(row?.value)}
      {row?.quality && row.quality !== "OK" && <span className={`q ${row.quality}`}> <span>{row.quality}</span></span>}
    </div>
  );
}

/**
 * One widget card: its parameters' names, a type switch, extra series for a
 * plot, and the body. `series` is `{ x, ys }` from the buffer for the current window.
 */
export default function Widget({ w, rowById, allRows, series, now, live, cannot, onChange, onRemove }) {
  const rows = w.ids.map((id) => rowById.get(id));
  const primary = rows[0];
  const plottable = isPlottable(primary);
  const types = WIDGET_TYPES.filter((t) => t.value === "value" || plottable);
  const stale = isStale(primary, now, live);

  return (
    <section className={`cw ${w.type === "plot" ? "wide" : ""} ${stale ? "stale" : ""}`} aria-label={primary?.name ?? "Missing parameter"}>
      <header className="cw-head">
        <div className="cw-names">
          {rows.map((r, i) => (
            <span key={w.ids[i]} className={`cw-name s${i}`} title={r ? `${r.subsystem} › ${r.message} › ${r.name}` : undefined}>
              {r?.name ?? `#${w.ids[i]} (not in this LUT)`}
              {w.type === "plot" && i > 0 && (
                <button
                  type="button"
                  className="cw-x"
                  aria-label={`Remove ${r?.name ?? w.ids[i]} from the plot`}
                  onClick={() => onChange({ ids: w.ids.filter((_, j) => j !== i) })}
                >
                  ×
                </button>
              )}
            </span>
          ))}
          {stale && <span className="cw-stale">stale</span>}
        </div>
        <select value={w.type} aria-label="Widget type" onChange={(e) => onChange({ type: e.target.value, ids: e.target.value === "plot" ? w.ids : w.ids.slice(0, 1) })}>
          {types.map((t) => (
            <option key={t.value} value={t.value}>
              {t.label}
            </option>
          ))}
        </select>
        <button type="button" className="cw-x" aria-label="Remove widget" title="Remove widget" onClick={onRemove}>
          ✕
        </button>
      </header>

      {w.type === "plot" &&
        (cannot ? (
          <div className="cw-cannot" role="alert">
            <b>This chart is not possible.</b> {cannot}
          </div>
        ) : (
          <PlotBody rows={rows} series={series} />
        ))}
      {w.type === "stat" && <StatBody row={primary} y={series.ys[0] ?? []} />}
      {w.type === "value" && <ValueBody row={primary} />}

      {w.type === "plot" && w.ids.length < MAX_SERIES && (
        <div className="cw-foot">
          <ParameterSelect
            rows={allRows}
            label="Add a series to this plot"
            placeholder="+ Add series…"
            filter={(r) => isPlottable(r) && !w.ids.includes(r.parameter_id)}
            onPick={(id) => onChange({ ids: [...w.ids, id] })}
          />
        </div>
      )}
    </section>
  );
}
