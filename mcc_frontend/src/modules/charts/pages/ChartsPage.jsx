import { useEffect, useMemo, useState } from "react";
import { useSelector } from "react-redux";
import telemetryApi from "../../../api/telemetryApi.js";
import { EmptyState, Icon } from "../../../components/common/index.js";
import useNow from "../../../hooks/useNow.js";
import { selectLive, selectLutCrc, selectRows } from "../../../redux/telemetry/index.js";
import ParameterSelect from "../components/ParameterSelect.jsx";
import Widget from "../components/Widget.jsx";
import useChartBuffer from "../hooks/useChartBuffer.js";
import { allIds, defaultDashboard, defaultTypeFor, loadDashboard, newUid, saveDashboard } from "../utils/dashboard.js";
import { sharedAxisProblem } from "../utils/limits.js";
import "../charts.css";

const WINDOWS = [
  { min: 5, label: "5 min" },
  { min: 15, label: "15 min" },
  { min: 30, label: "All stored (~30 min)" },
];

/** Values over time, in sections of widgets. Waits for the parameter table: layouts are keyed by it. */
export default function ChartsPage() {
  const rows = useSelector(selectRows);
  const lutCrc = useSelector(selectLutCrc);
  if (!rows.length || lutCrc === null) return <EmptyState>Loading parameters…</EmptyState>;
  // A new LUT means new parameter ids: start the dashboard over.
  return <Dashboard key={lutCrc} rows={rows} lutCrc={lutCrc} />;
}

function Dashboard({ rows, lutCrc }) {
  const live = useSelector(selectLive);
  const now = useNow(1000);
  const rowById = useMemo(() => new Map(rows.map((r) => [r.parameter_id, r])), [rows]);
  const [dash, setDash] = useState(() => loadDashboard(lutCrc) ?? defaultDashboard(rows));
  const [windowMin, setWindowMin] = useState(15);
  const [confirmReset, setConfirmReset] = useState(false);
  // Expected ranges (parameter_limit). Without them every plot is formed, as before.
  const [limits, setLimits] = useState(() => new Map());
  useEffect(() => {
    let current = true;
    telemetryApi
      .limits()
      .then((rs) => {
        if (current) setLimits(new Map(rs.map((x) => [x.parameter_id, { lo: x.lo, hi: x.hi }])));
      })
      .catch(() => {
        /* no limits to check against */
      });
    return () => {
      current = false;
    };
  }, []);

  useEffect(() => void saveDashboard(lutCrc, dash), [lutCrc, dash]);

  const ids = useMemo(() => allIds(dash), [dash]);
  const buffer = useChartBuffer(ids, rowById);
  const fromMs = (buffer.lastTs() ?? now) - windowMin * 60_000;

  const editList = (section, f) => setDash((d) => ({ ...d, widgets: { ...d.widgets, [section]: f(d.widgets[section] ?? []) } }));
  const addWidget = (section, id) => editList(section, (l) => [...l, { uid: newUid(), type: defaultTypeFor(rowById.get(id)), ids: [id] }]);
  const addSection = () => {
    const key = `s${Date.now().toString(36)}`;
    setDash((d) => ({ sections: [...d.sections, { key, title: `My section ${d.sections.length + 1}` }], widgets: { ...d.widgets, [key]: [] } }));
  };
  const removeSection = (key) =>
    setDash((d) => ({
      sections: d.sections.filter((s) => s.key !== key),
      widgets: Object.fromEntries(Object.entries(d.widgets).filter(([k]) => k !== key)),
    }));

  const seriesFor = (w) => {
    const parts = w.ids.map((id) => buffer.slice(id, fromMs));
    return { x: parts[0]?.x ?? [], ys: parts.map((p) => p.y) };
  };

  /** Why this plot cannot be formed from its series, or null when it can. */
  const plotProblem = (w) =>
    sharedAxisProblem(
      w.ids.map((id) => {
        const r = rowById.get(id);
        return { name: r?.name ?? `#${id}`, unit: r?.unit ?? "", limit: limits.get(id) ?? null };
      }),
    );

  return (
    <div className="charts feature">
      <div className="charts-bar">
        <label>
          <Icon name="clock" size={14} /> Window{" "}
          <select value={windowMin} onChange={(e) => setWindowMin(Number(e.target.value))}>
            {WINDOWS.map((w) => (
              <option key={w.min} value={w.min}>
                {w.label}
              </option>
            ))}
          </select>
        </label>
        <span className="charts-hint">UTC · drag across a plot to zoom</span>
        <span className="grow" />
        <button type="button" onClick={addSection}>
          <Icon name="section" size={14} /> Add section
        </button>
        {confirmReset ? (
          <>
            <span>Reset to the default layout?</span>
            <button
              type="button"
              className="danger"
              onClick={() => {
                setDash(defaultDashboard(rows));
                setConfirmReset(false);
              }}
            >
              <Icon name="reset" size={14} /> Reset
            </button>
            <button type="button" onClick={() => setConfirmReset(false)}>
              Cancel
            </button>
          </>
        ) : (
          <button type="button" onClick={() => setConfirmReset(true)}>
            <Icon name="reset" size={14} /> Reset layout
          </button>
        )}
      </div>

      <div className="charts-body">
        {dash.sections.map((s) => {
          const list = dash.widgets[s.key] ?? [];
          return (
            <section className="charts-section" key={s.key}>
              <header>
                <h2>{s.title}</h2>
                <span className="count">
                  {list.length} widget{list.length === 1 ? "" : "s"}
                </span>
                <ParameterSelect rows={rows} label={`Add a widget to ${s.title}`} placeholder="+ Add widget…" onPick={(id) => addWidget(s.key, id)} />
                {list.length === 0 && (
                  <button type="button" onClick={() => removeSection(s.key)} title="Remove this empty section">
                    <Icon name="trash" size={14} />
                  </button>
                )}
              </header>
              <div className="charts-grid">
                {list.length === 0 && <p className="empty">No widgets. Add one with the menu above.</p>}
                {list.map((w) => (
                  <Widget
                    key={w.uid}
                    w={w}
                    rowById={rowById}
                    allRows={rows}
                    series={seriesFor(w)}
                    now={now}
                    live={live}
                    cannot={w.type === "plot" ? plotProblem(w) : null}
                    onChange={(patch) => editList(s.key, (l) => l.map((x) => (x.uid === w.uid ? { ...x, ...patch } : x)))}
                    onRemove={() => editList(s.key, (l) => l.filter((x) => x.uid !== w.uid))}
                  />
                ))}
              </div>
            </section>
          );
        })}
      </div>
    </div>
  );
}
