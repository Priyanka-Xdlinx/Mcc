import { useMemo, useState } from "react";
import { tierOfByte } from "../utils/trace.js";

const COLS = 64;

/**
 * The 1280-byte payload, one cell per byte, coloured by the part it belongs
 * to. A solid byte was claimed by a lookup-table pick; a faint one is space no
 * parameter asked for.
 */
export default function ByteMap({ frame }) {
  const [hover, setHover] = useState(null);

  const rows = useMemo(() => {
    const out = [];
    for (let i = 0; i < frame.payloadLength; i += COLS) {
      out.push(Array.from({ length: Math.min(COLS, frame.payloadLength - i) }, (_, k) => i + k));
    }
    return out;
  }, [frame.payloadLength]);

  const owner = hover !== null && frame.byteOwner[hover] >= 0 ? frame.legend[frame.byteOwner[hover]] : null;

  return (
    <section className="fv-card">
      <div className="fv-head">
        <div>
          <div className="fv-title">The 1280-byte payload of this subframe</div>
          <div className="fv-sub">
            Every byte is coloured by the tier it belongs to. A solid byte was claimed by a lookup-table pick; a faint
            one is space no parameter asked for, sent as zero.
          </div>
        </div>
        <div className="fv-stat">
          <div className="dv-k">Parameters in frame</div>
          <div className="fv-stat-v">
            {frame.decodedOk} / {frame.totalParameters}
          </div>
        </div>
      </div>

      <div className="bytemap-legend">
        {["VH", "H", "M", "L"].map((t) => (
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
            "hover a byte"
          ) : (
            <>
              byte <b>{hover}</b> · {tierOfByte(hover)} · <b>{frame.payloadHex[hover]}</b>
              {owner
                ? ` · ${owner.entry} (${owner.subsystem})`
                : frame.byteOwner[hover] === -2
                  ? " · reserved"
                  : " · not claimed by any pick"}
            </>
          )}
        </span>
      </div>

      <div className="bytemap" onMouseLeave={() => setHover(null)}>
        {rows.map((row, ri) => (
          <div className="bm-row" key={ri}>
            <span className="bm-off">{row[0]}</span>
            {row.map((i) => {
              const t = tierOfByte(i);
              const o = frame.byteOwner[i];
              const cls = o === -2 ? "reserved" : o >= 0 ? `t-${t} picked` : `t-${t} unclaimed`;
              return <span key={i} className={`bm-b ${cls} ${hover === i ? "hov" : ""}`} onMouseEnter={() => setHover(i)} />;
            })}
          </div>
        ))}
      </div>
    </section>
  );
}
