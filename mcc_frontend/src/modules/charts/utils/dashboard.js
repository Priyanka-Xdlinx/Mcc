import { readJson, writeJson } from "../../../utils/storage.js";

/**
 * The Charts layout: sections of widgets, saved per browser and per LUT
 * (parameter ids are only meaningful for one LUT).
 *
 *   { sections: [{ key, title }], widgets: { [sectionKey]: [{ uid, type, ids: [parameterId] }] } }
 *
 * type: "plot" (up to MAX_SERIES numeric ids), "stat" (one numeric id: latest,
 * min, max, mean), "value" (one id of any type, shown as decoded text).
 */
export const WIDGET_TYPES = [
  { value: "plot", label: "Plot" },
  { value: "stat", label: "Stat card" },
  { value: "value", label: "Value" },
];
export const MAX_SERIES = 3;

const NUMERIC = new Set(["FLOAT", "DOUBLE", "INT", "UINT"]);

/** A parameter whose value means something on an axis. */
export const isPlottable = (row) => Boolean(row) && NUMERIC.has(row.data_type);

export const defaultTypeFor = (row) => (isPlottable(row) ? "plot" : "value");

export const newUid = () => `u${Date.now().toString(36)}${Math.random().toString(36).slice(2, 6)}`;

const storageKey = (lutCrc) => `mcc.charts.v1.lut${lutCrc}`;

/** The default layout: a few meaningful sections, found by name so they follow the MIB. */
export function defaultDashboard(rows) {
  const find = (sub, name) => rows.find((r) => r.subsystem === sub && r.name === name);
  const w = (sub, name, type) => {
    const r = find(sub, name);
    return r ? { uid: newUid(), type: isPlottable(r) ? type : "value", ids: [r.parameter_id] } : null;
  };
  const sections = [
    { key: "gps", title: "Position (GPS)", list: [w("GPS", "lat", "plot"), w("GPS", "lon", "plot"), w("GPS", "hgt", "stat"), w("GPS", "num_svs", "stat")] },
    {
      key: "eps",
      title: "Power (EPS)",
      list: [
        w("EPS", "Conv1 OP Voltage Reading", "plot"),
        w("EPS", "Total Battery Voltage Reading", "stat"),
        w("EPS", "Total Battery Current Reading", "stat"),
        w("EPS", "EPS Mode", "value"),
      ],
    },
    { key: "uhf", title: "Radio (UHF)", list: [w("UHF", "Last RSSI", "plot"), w("UHF", "Last LQI", "stat"), w("UHF", "Uptime", "value")] },
    {
      key: "adcs",
      title: "Attitude (ADCS)",
      list: [w("ADCS", "Control mode", "value"), w("ADCS", "Estimated roll angle", "plot"), w("ADCS", "Estimated pitch angle", "plot")],
    },
  ];
  return {
    sections: sections.map(({ key, title }) => ({ key, title })),
    widgets: Object.fromEntries(sections.map((s) => [s.key, s.list.filter(Boolean)])),
  };
}

/** A stored layout, checked; null when absent or unusable. */
export function parseDashboard(raw) {
  if (!raw || typeof raw !== "object" || !Array.isArray(raw.sections) || !raw.widgets || typeof raw.widgets !== "object") return null;
  const sections = raw.sections.filter((s) => s && typeof s.key === "string" && typeof s.title === "string");
  const widgets = {};
  for (const s of sections) {
    const list = Array.isArray(raw.widgets[s.key]) ? raw.widgets[s.key] : [];
    widgets[s.key] = list.filter(
      (w) =>
        w &&
        typeof w.uid === "string" &&
        WIDGET_TYPES.some((t) => t.value === w.type) &&
        Array.isArray(w.ids) &&
        w.ids.length > 0 &&
        w.ids.every(Number.isInteger),
    );
  }
  return { sections, widgets };
}

export const loadDashboard = (lutCrc) => parseDashboard(readJson(storageKey(lutCrc)));
export const saveDashboard = (lutCrc, d) => writeJson(storageKey(lutCrc), d);

/** Every parameter id any widget shows. */
export const allIds = (d) => [...new Set(Object.values(d.widgets).flatMap((list) => list.flatMap((w) => w.ids)))];

/** Min, max and mean of the non-null values, or null when there are none. */
export function stats(y) {
  let min = Infinity;
  let max = -Infinity;
  let sum = 0;
  let count = 0;
  for (const v of y) {
    if (v === null || v === undefined) continue;
    min = Math.min(min, v);
    max = Math.max(max, v);
    sum += v;
    count++;
  }
  return count ? { min, max, avg: sum / count, count } : null;
}
