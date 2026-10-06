import { useMemo } from "react";

/**
 * "+ Add …" as a native select, grouped by subsystem: ~750 options render far
 * faster natively than as a menu, and the browser's type-ahead works.
 */
export default function ParameterSelect({ rows, label, placeholder, onPick, filter }) {
  const groups = useMemo(() => {
    const m = new Map();
    for (const r of rows) {
      if (filter && !filter(r)) continue;
      if (!m.has(r.subsystem)) m.set(r.subsystem, []);
      m.get(r.subsystem).push(r);
    }
    return [...m.entries()].sort(([a], [b]) => a.localeCompare(b));
  }, [rows, filter]);

  return (
    <select value="" aria-label={label} onChange={(e) => e.target.value && onPick(Number(e.target.value))}>
      <option value="">{placeholder}</option>
      {groups.map(([sub, list]) => (
        <optgroup key={sub} label={sub}>
          {list.map((r) => (
            <option key={r.parameter_id} value={r.parameter_id}>
              {r.name.slice(0, 48)}
              {r.unit ? ` (${r.unit})` : ""} · {r.parameter_id}
            </option>
          ))}
        </optgroup>
      ))}
    </select>
  );
}
