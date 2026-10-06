import { layerOf } from "../utils/trace.js";
import HexDump from "./HexDump.jsx";

const LANES = [
  {
    n: 1,
    title: "LAYER 1 — which message do these bytes belong to?",
    note: "wire framing, tier split, block assembly, and the LUT. Needs the lookup table; needs nothing from the MIB.",
  },
  { n: 2, title: "LAYER 2 — what does this byte mean?", note: "the mission database: offsets, type, endianness, scale, enums." },
  { n: 3, title: "RESULT", note: "what reaches the dashboard and the database." },
];

function Lane({ n, title, note, steps }) {
  return (
    <div className={`lane lane-${n}`}>
      <div className="lane-head">
        <span className="lane-n">{n}</span>
        <div>
          <div className="lane-title">{title}</div>
          <div className="lane-note">{note}</div>
        </div>
      </div>
      {steps.map((s, i) => (
        <div className="tstep" key={i}>
          <div className="tstep-title">{s.title}</div>
          {s.lines.length > 0 && (
            <table className="tstep-lines">
              <tbody>
                {s.lines.map((l, j) => (
                  <tr key={j}>
                    <td className="tl-k">{l.label}</td>
                    <td className="tl-v">{l.value}</td>
                    <td className="tl-n">{l.note ?? ""}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
          {s.hex.map((h, j) => (
            <HexDump key={j} {...h} />
          ))}
        </div>
      ))}
      {steps.length === 0 && <div className="tstep dim">—</div>}
    </div>
  );
}

/** One parameter's decode, in three columns: framing, meaning, result. */
export default function TraceLanes({ trace }) {
  const steps = trace.steps ?? [];
  return (
    <>
      <div className="verdict">
        <div>
          <div className="dv-k">Parameter</div>
          <div className="verdict-v">{trace.parameter}</div>
        </div>
        <div>
          <div className="dv-k">Entry · tier</div>
          <div className="verdict-v">
            {trace.entry} · <span className={`tier ${trace.tier}`}>{trace.tier}</span>
          </div>
        </div>
        <div>
          <div className="dv-k">Frame</div>
          <div className="verdict-v">
            MF#{trace.masterFrame} SF{trace.subframe}
          </div>
        </div>
        <div>
          <div className="dv-k">Spacecraft time</div>
          <div className="verdict-v">{trace.spacecraftTime}</div>
        </div>
        <div className="verdict-result">
          <div className="dv-k">Decoded value</div>
          <div className={`verdict-big q-${trace.quality ?? ""}`}>{trace.value ?? "—"}</div>
          {trace.quality && trace.quality !== "OK" && <div className={`q ${trace.quality}`}>{trace.quality}</div>}
        </div>
      </div>
      <div className="lanes">
        {LANES.map((l) => (
          <Lane key={l.n} {...l} steps={steps.filter((s) => layerOf(s.title) === l.n)} />
        ))}
      </div>
    </>
  );
}
