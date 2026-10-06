import { useEffect, useState } from "react";
import telemetryApi from "../../../api/telemetryApi.js";
import { errorMessage } from "../../../api/apiError.js";
import ByteMap from "./ByteMap.jsx";
import EntryRow from "./EntryRow.jsx";
import TraceModal from "./TraceModal.jsx";

/**
 * Everything in one subframe: the payload byte map, then each tier with its
 * entries and parameters. `mf`/`sf` null means "the newest frame", refreshed
 * every `refreshMs` while live.
 */
export default function FrameView({ mf, sf, live, refreshMs }) {
  const [frame, setFrame] = useState(null);
  const [open, setOpen] = useState(null);
  const [traceId, setTraceId] = useState(null);
  const [onlyDecoded, setOnlyDecoded] = useState(false);

  useEffect(() => {
    let dead = false;
    const load = () =>
      telemetryApi
        .frameDecode(mf, sf)
        .then((f) => !dead && setFrame(f))
        .catch((e) => !dead && setFrame({ error: errorMessage(e, "Could not decode the frame.") }));
    void load();
    const t = mf === null && live ? setInterval(load, refreshMs) : null;
    return () => {
      dead = true;
      if (t) clearInterval(t);
    };
  }, [mf, sf, live, refreshMs]);

  if (!frame) return <div className="empty">loading frame…</div>;
  if (frame.error) return <div className="empty">{frame.error}</div>;

  return (
    <div className="frameview">
      <ByteMap frame={frame} />

      <div className="fv-tiers">
        {frame.tiers.map((t) => (
          <section className="fv-card" key={t.tier}>
            <div className="fv-head">
              <div>
                <div className="fv-title">
                  <span className={`tier ${t.tier}`}>{t.tier}</span> {t.entries.length} entries ·{" "}
                  {t.entries.reduce((n, e) => n + e.parameters.length, 0)} parameters
                </div>
                <div className="fv-sub">
                  payload[{t.segOffset}:{t.segOffset + t.segBytes}] · slice {t.sliceIndex + 1} of {t.blockSpan}
                  {t.blockComplete ? (
                    <span className="ok-chip"> block complete</span>
                  ) : (
                    <span className="warn-chip">
                      {" "}
                      block filling — {t.blockSpan - t.slicesPresent.length} slice(s) missing
                    </span>
                  )}
                </div>
              </div>
            </div>
            <div className="entries">
              {t.entries.map((e) => {
                const id = `${t.tier}-${e.entry}`;
                return (
                  <EntryRow
                    key={id}
                    entry={e}
                    open={open === id}
                    onToggle={() => setOpen(open === id ? null : id)}
                    onlyDecoded={onlyDecoded}
                    onTrace={setTraceId}
                  />
                );
              })}
            </div>
          </section>
        ))}
      </div>

      {traceId !== null && <TraceModal parameterId={traceId} mf={mf} sf={sf} onClose={() => setTraceId(null)} />}

      <label className="fv-foot">
        <input type="checkbox" checked={onlyDecoded} onChange={(e) => setOnlyDecoded(e.target.checked)} />
        hide parameters that did not decode
      </label>
    </div>
  );
}
