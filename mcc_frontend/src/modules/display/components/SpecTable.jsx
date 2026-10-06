import { memo, useMemo } from "react";
import { displayValue, utcClock } from "../../../utils/format.js";
import { useHeader } from "../utils/headerStore.js";

const NOT_IN_FEED = "not in the XMCC v1 feed";

const QLABEL = {
  NO_ANSWER: "no answer",
  BLOCK_INCOMPLETE: "incomplete",
  WARMUP: "warm-up",
  NOT_DOWNLINKED: "not downlinked",
};

function HCell({ k, v, title, span }) {
  const missing = v == null || v === "";
  return (
    <div className={`dp-hc ${span ? "span2" : ""}`} title={title}>
      <div className="dp-hk">{k}</div>
      <div className={`dp-hv ${missing ? "none" : ""}`}>{missing ? "—" : v}</div>
    </div>
  );
}

const Missing = ({ k }) => <HCell k={k} v={null} title={NOT_IN_FEED} />;

/** HEADER 1 and HEADER 2: the only part of a window that changes on every subframe. */
export const HeaderBlock = memo(function HeaderBlock({ pageNo }) {
  const h = useHeader();
  return (
    <div className="dp-head" role="group" aria-label="Frame header">
      <Missing k="SAT ID" />
      <HCell k="Time of frame" v={h.frameTime} title="Spacecraft time of the newest subframe" />
      <HCell k="Frame no" v={h.frameNo} title="Master frame and subframe of the newest subframe" />
      <Missing k="Source UHF/Sband" />
      <Missing k="Beacon" />
      <Missing k="HM" />
      <HCell
        k="Cont TM counter"
        v={h.session == null ? null : h.session.toLocaleString()}
        title="Subframes received in the current session"
        span
      />
      <Missing k="Last TC" />
      <Missing k="Last U/L Cmd" />
      <HCell
        k="Frame valid cnt"
        v={h.framesValid == null ? null : h.framesValid.toLocaleString()}
        title={`Subframes with a valid header since the backend started${
          h.framesRejected != null ? ` · ${h.framesRejected.toLocaleString()} rejected` : ""
        }`}
      />
      <HCell k="Time of recd frame" v={utcClock(h.receivedAt)} title={h.receivedAt ?? "When the backend received the newest subframe"} />
      <div className="dp-hc blank" aria-hidden="true" />
      <HCell k="Page no" v={pageNo} title="This window's page number (kept when other windows close)" />
    </div>
  );
});

function tip(r) {
  return `${r.parameter_id} · ${r.name}\n${r.subsystem} › ${r.message} › ${r.entry}\ntier ${r.tier}${
    r.unit ? ` · unit ${r.unit}` : ""
  }${r.quality ? ` · ${r.quality}` : ""}${r.spacecraft_time ? `\nspacecraft time ${r.spacecraft_time}` : ""}`;
}

function ValueCell({ row, stale }) {
  if (!row) return <td className="dp-v none">—</td>;
  const q = row.quality;
  if (row.value == null) {
    if (q && q !== "OK") {
      return (
        <td className={`dp-v q ${q}`} title={tip(row)}>
          <span>{QLABEL[q] ?? q}</span>
        </td>
      );
    }
    return (
      <td className="dp-v none" title="no data received yet">
        —
      </td>
    );
  }
  const flagged = q && q !== "OK";
  return (
    <td
      className={`dp-v ${flagged ? `q ${q}` : ""} ${stale ? "stale" : ""}`}
      title={`${row.value}${flagged ? ` · ${q}` : ""}${stale ? " · stale: no update for over 3 tier periods" : ""}`}
    >
      {displayValue(row.value)}
      {flagged && <span className="dp-qtag">{QLABEL[q] ?? q}</span>}
    </td>
  );
}

function SlotCells({ i, id, row, stale, dup, editing, active, onSlot }) {
  const cls = `${editing ? "pick" : ""} ${active ? "active" : ""} ${i % 2 ? "right" : "left"}`;
  const click = editing ? () => onSlot(i) : undefined;
  const name =
    id == null ? (
      <span className="dp-emptyslot">{editing ? "+ choose parameter" : ""}</span>
    ) : row ? (
      row.name
    ) : (
      <span className="dp-unknown">unknown parameter</span>
    );
  return (
    <>
      <td
        className={`dp-id ${cls} ${dup ? "dup" : ""}`}
        onClick={click}
        title={dup ? "This parameter appears more than once on this page" : undefined}
      >
        {id ?? ""}
      </td>
      <td className={`dp-n ${cls}`} onClick={click} title={row ? tip(row) : undefined}>
        {editing ? (
          <button type="button" className="dp-slotbtn" aria-label={`Slot ${i + 1}: choose parameter`}>
            {name}
          </button>
        ) : (
          name
        )}
      </td>
      {id == null ? <td className={`dp-v none ${cls}`} onClick={click} /> : <ValueCell row={row} stale={stale} />}
    </>
  );
}

function SpecTableImpl({ params, slots, stale, editing, activeSlot, onSlot }) {
  const dups = useMemo(() => {
    const seen = new Map();
    for (const p of params) if (p != null) seen.set(p, (seen.get(p) ?? 0) + 1);
    return params.map((p) => p != null && seen.get(p) > 1);
  }, [params]);

  const lines = [];
  for (let r = 0; r < 10; r++) {
    const a = r * 2;
    const b = a + 1;
    lines.push(
      <tr key={r}>
        {[a, b].map((i) => (
          <SlotCells
            key={i}
            i={i}
            id={params[i]}
            row={slots[i]}
            stale={stale[i] === "1"}
            dup={dups[i]}
            editing={editing}
            active={activeSlot === i}
            onSlot={onSlot}
          />
        ))}
      </tr>,
    );
  }
  return (
    <table className="dp-table">
      <colgroup>
        <col className="c-id" />
        <col className="c-n" />
        <col className="c-v" />
        <col className="c-id" />
        <col className="c-n" />
        <col className="c-v" />
      </colgroup>
      <thead>
        <tr>
          <th>Parameter ID</th>
          <th>Parameter name</th>
          <th>Engineering value</th>
          <th>Parameter ID</th>
          <th>Parameter name</th>
          <th>Engineering value</th>
        </tr>
      </thead>
      <tbody>{lines}</tbody>
    </table>
  );
}

const sameSlots = (a, b) => a.length === b.length && a.every((s, i) => s === b[i]);

/** Two columns of ten slots. Re-renders only when one of its 20 rows (by reference) or its own state changes. */
const SpecTable = memo(
  SpecTableImpl,
  (a, b) =>
    a.params === b.params &&
    a.stale === b.stale &&
    a.editing === b.editing &&
    a.activeSlot === b.activeSlot &&
    a.onSlot === b.onSlot &&
    sameSlots(a.slots, b.slots),
);

export default SpecTable;
