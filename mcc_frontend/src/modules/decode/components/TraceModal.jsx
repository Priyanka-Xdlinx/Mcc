import { useEffect, useState } from "react";
import telemetryApi from "../../../api/telemetryApi.js";
import { errorMessage } from "../../../api/apiError.js";
import { layerOf } from "../utils/trace.js";
import HexDump from "./HexDump.jsx";

const BADGE = { 1: "1", 2: "2", 3: "✓" };
const CLS = { 1: "L1", 2: "L2", 3: "LR" };

/**
 * The full decode of one parameter, opened over the frame it came from.
 * Fetched for the exact frame being viewed, so it explains the value on
 * screen and not a later reading.
 */
export default function TraceModal({ parameterId, mf, sf, onClose }) {
  const [trace, setTrace] = useState(null);

  useEffect(() => {
    let dead = false;
    telemetryApi
      .trace(parameterId, mf, sf)
      .then((t) => !dead && setTrace(t))
      .catch((e) => !dead && setTrace({ error: errorMessage(e, "Could not load the trace.") }));
    return () => {
      dead = true;
    };
  }, [parameterId, mf, sf]);

  useEffect(() => {
    const onKey = (e) => e.key === "Escape" && onClose();
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [onClose]);

  return (
    <div className="tm-backdrop" onClick={onClose}>
      <div className="tm-panel" onClick={(e) => e.stopPropagation()} role="dialog" aria-modal aria-label="Decode trace">
        <div className="tm-bar">
          <div>
            <div className="tm-title">{trace?.parameter ?? "Decode trace"}</div>
            {trace?.entry && (
              <div className="tm-sub">
                {trace.entry} · {trace.subsystem} · {trace.message}
                {trace.tier && <span className={`tier ${trace.tier}`}> {trace.tier}</span>}
                {" · "}MF#{trace.masterFrame} SF{trace.subframe} · {trace.spacecraftTime}
              </div>
            )}
          </div>
          <div className="tm-result">
            <div className="dv-k">Decoded value</div>
            <div className={`tm-value q-${trace?.quality ?? ""}`}>{trace?.value ?? "—"}</div>
          </div>
          <button className="tm-close" onClick={onClose} title="Close (Esc)" aria-label="Close">
            ✕
          </button>
        </div>

        <div className="tm-body">
          {!trace ? (
            <div className="tin-loading">loading trace…</div>
          ) : trace.error ? (
            <div className="tin-loading">{trace.error}</div>
          ) : (
            <div className="tin-steps">
              {(trace.steps ?? []).map((s, i) => {
                const layer = layerOf(s.title);
                return (
                  <div className={`tin-step ${CLS[layer]}`} key={i}>
                    <div className="tin-step-head">
                      <span className={`tin-badge ${CLS[layer]}`}>{BADGE[layer]}</span>
                      <span className="tin-step-title">{s.title}</span>
                    </div>
                    {s.lines.length > 0 && (
                      <table className="tin-lines">
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
                      <HexDump key={j} {...h} maxRows={8} />
                    ))}
                  </div>
                );
              })}
            </div>
          )}
        </div>
      </div>
    </div>
  );
}
