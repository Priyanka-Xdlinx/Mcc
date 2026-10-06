import { useEffect, useMemo, useRef, useState } from "react";
import { useSelector } from "react-redux";
import { selectRows } from "../../../redux/telemetry/index.js";
import { buildIndex, search } from "../utils/paramIndex.js";

const MAX = 200;

/** Choose the parameter for one slot: type to search, arrows and Enter to pick. */
export default function ParamPicker({ slot, params, onPick, onClose }) {
  const rows = useSelector(selectRows);
  const [q, setQ] = useState("");
  const [active, setActive] = useState(0);
  const index = useMemo(() => buildIndex(rows), [rows]);
  const { hits, total } = useMemo(() => search(index, q, MAX), [index, q]);
  const listRef = useRef(null);
  const current = params[slot];

  useEffect(() => {
    listRef.current?.querySelector(`[data-i="${active}"]`)?.scrollIntoView({ block: "nearest" });
  }, [active]);

  const slotOf = (id) => {
    const at = params.findIndex((p, i) => p === id && i !== slot);
    return at < 0 ? null : at + 1;
  };

  const onKey = (e) => {
    if (e.key === "ArrowDown") {
      e.preventDefault();
      setActive((a) => Math.min(hits.length - 1, a + 1));
    } else if (e.key === "ArrowUp") {
      e.preventDefault();
      setActive((a) => Math.max(0, a - 1));
    } else if (e.key === "Enter") {
      e.preventDefault();
      if (hits[active]) onPick(hits[active].row.parameter_id);
    } else if (e.key === "Escape") {
      e.preventDefault();
      e.stopPropagation();
      onClose();
    }
  };

  return (
    <div className="dp-picker" role="dialog" aria-label={`Choose the parameter for slot ${slot + 1}`} onKeyDown={onKey}>
      <div className="dp-picker-top">
        <b>Slot {slot + 1}</b>
        <input
          type="search"
          autoFocus
          placeholder="Search id, name, subsystem, message…"
          value={q}
          onChange={(e) => {
            setQ(e.target.value);
            setActive(0);
          }}
          aria-controls="dp-picker-list"
        />
        <button type="button" onClick={() => onPick(null)} disabled={current == null}>
          Clear slot
        </button>
        <button type="button" onClick={onClose} aria-label="Close">
          Esc
        </button>
      </div>
      <div className="dp-picker-count">
        {index.length === 0
          ? "No parameter table loaded yet (is the backend running?)"
          : `${total.toLocaleString()} match${total === 1 ? "" : "es"}${total > MAX ? ` · showing the first ${MAX}` : ""} · ↑↓ Enter`}
      </div>
      <div className="dp-picker-list" id="dp-picker-list" ref={listRef} role="listbox">
        {hits.map((e, i) => {
          const r = e.row;
          const head = i === 0 || hits[i - 1].row.subsystem !== r.subsystem;
          const dupAt = slotOf(r.parameter_id);
          return (
            <div key={r.parameter_id}>
              {head && <div className="dp-picker-sub">{r.subsystem}</div>}
              <div
                data-i={i}
                role="option"
                aria-selected={i === active}
                className={`dp-picker-item ${i === active ? "on" : ""} ${r.parameter_id === current ? "cur" : ""}`}
                onMouseEnter={() => setActive(i)}
                onClick={() => onPick(r.parameter_id)}
              >
                <span className="pid">{r.parameter_id}</span>
                <span className="pname">{r.name}</span>
                <span className="pmsg">{r.message}</span>
                {dupAt != null && <span className="pdup">already in slot {dupAt}</span>}
                <span className={`tier ${r.tier}`}>{r.tier}</span>
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
}
