import { useEffect, useMemo, useState } from "react";
import { useSelector } from "react-redux";
import telemetryApi from "../../../api/telemetryApi.js";
import { errorMessage } from "../../../api/apiError.js";
import { selectLive, selectRows } from "../../../redux/telemetry/index.js";
import FrameView from "../components/FrameView.jsx";
import TraceLanes from "../components/TraceLanes.jsx";
import { SPEEDS } from "../utils/trace.js";
import "../decode.css";

const FRAMES_POLL_MS = 2000;
const MAX_OPTIONS = 300;

/**
 * From payload bytes to engineering value: either a whole subframe, or one
 * parameter traced through both decode layers. Follows the newest frame live,
 * or steps and replays through the frames the backend buffered.
 */
export default function DecodePage() {
  const rows = useSelector(selectRows);
  const live = useSelector(selectLive);
  const [mode, setMode] = useState("frame");
  const [chosenId, setParamId] = useState(null);
  const [q, setQ] = useState("");
  const [frames, setFrames] = useState([]);
  const [cursor, setCursor] = useState(null); // index into frames, 0 = newest; null = follow live
  const [playing, setPlaying] = useState(false);
  const [speed, setSpeed] = useState(1);
  const [trace, setTrace] = useState(null);
  const follow = cursor === null;
  const current = follow ? frames[0] : frames[cursor];

  // Until one is chosen, open on something whose decode is worth reading: a scaled float.
  const paramId = useMemo(() => {
    if (chosenId !== null || !rows.length) return chosenId;
    const pick = rows.find((r) => r.name === "Conv1 OP Voltage Reading") ?? rows.find((r) => r.unit) ?? rows[0];
    return pick.parameter_id;
  }, [rows, chosenId]);

  const candidates = useMemo(() => {
    const needle = q.trim().toLowerCase();
    const list = rows
      .filter(
        (r) =>
          !needle ||
          r.name.toLowerCase().includes(needle) ||
          r.entry.toLowerCase().includes(needle) ||
          r.subsystem.toLowerCase().includes(needle),
      )
      .slice(0, MAX_OPTIONS);
    // The list is capped, so keep the parameter being traced in it whatever the filter says.
    if (paramId !== null && !list.some((r) => r.parameter_id === paramId)) {
      const sel = rows.find((r) => r.parameter_id === paramId);
      if (sel) list.unshift(sel);
    }
    return list;
  }, [rows, q, paramId]);

  useEffect(() => {
    const load = () =>
      telemetryApi
        .frames()
        .then((f) => setFrames(Array.isArray(f) ? f : []))
        .catch(() => {});
    void load();
    if (!live) return undefined;
    const t = setInterval(load, FRAMES_POLL_MS);
    return () => clearInterval(t);
  }, [live]);

  // One parameter: the newest frame on a cadence you can watch, or the frame under the cursor.
  const mf = follow ? null : current?.masterFrame;
  const sf = follow ? null : current?.subframe;
  useEffect(() => {
    if (mode !== "param" || paramId === null) return undefined;
    let dead = false;
    const load = () =>
      telemetryApi
        .trace(paramId, mf, sf)
        .then((t) => !dead && setTrace(t))
        .catch((e) => !dead && setTrace({ error: errorMessage(e, "Could not load the trace.") }));
    void load();
    const t = follow && live ? setInterval(load, SPEEDS[speed].ms) : null;
    return () => {
      dead = true;
      if (t) clearInterval(t);
    };
  }, [mode, paramId, mf, sf, follow, live, speed]);

  // Replay over the buffered frames, oldest to newest.
  useEffect(() => {
    if (!playing) return undefined;
    const t = setInterval(() => {
      setCursor((c) => {
        const next = (c ?? 0) - 1;
        if (next < 0) {
          setPlaying(false);
          return 0;
        }
        return next;
      });
    }, SPEEDS[speed].ms);
    return () => clearInterval(t);
  }, [playing, speed]);

  const step = (delta) => {
    setPlaying(false);
    setCursor((c) => Math.max(0, Math.min(frames.length - 1, (c ?? 0) + delta)));
  };

  return (
    <div className="decode feature">
      <div className="decode-bar">
        <div className="dv-group seg">
          <button className={mode === "frame" ? "primary" : ""} onClick={() => setMode("frame")} title="Everything in this subframe">
            Whole frame
          </button>
          <button className={mode === "param" ? "primary" : ""} onClick={() => setMode("param")} title="Follow one parameter byte by byte">
            One parameter
          </button>
          {mode === "frame" && <span className="seg-hint">open an entry, then click a parameter to unfold its trace</span>}
        </div>

        {mode === "param" && (
          <div className="dv-group">
            <span className="dv-k">Parameter</span>
            <input type="search" placeholder="filter…" value={q} onChange={(e) => setQ(e.target.value)} style={{ minWidth: 150 }} />
            <select value={paramId ?? ""} onChange={(e) => setParamId(Number(e.target.value))} style={{ maxWidth: 320 }} aria-label="Parameter">
              {candidates.map((r) => (
                <option key={r.parameter_id} value={r.parameter_id}>
                  [{r.tier}] {r.entry} · {r.name}
                </option>
              ))}
            </select>
          </div>
        )}

        <div className="dv-group">
          <button
            className={follow ? "primary" : ""}
            onClick={() => {
              setPlaying(false);
              setCursor(null);
            }}
            title="Follow the newest frame as it arrives"
          >
            ● Live
          </button>
          <button onClick={() => step(+1)} title="One subframe older">
            ‹ Prev
          </button>
          <button
            onClick={() => {
              if (follow) setCursor(Math.max(0, frames.length - 1));
              setPlaying((p) => !p);
            }}
            disabled={frames.length === 0}
          >
            {playing ? "❚❚ Pause" : "▶ Replay"}
          </button>
          <button onClick={() => step(-1)} title="One subframe newer">
            Next ›
          </button>
        </div>

        <div className="dv-group">
          <span className="dv-k">Speed</span>
          {SPEEDS.map((s, i) => (
            <button key={s.label} className={i === speed ? "primary" : ""} onClick={() => setSpeed(i)}>
              {s.label}
            </button>
          ))}
        </div>

        <div className="count">
          {frames.length} frames buffered
          {current && (
            <>
              {" · viewing "}
              <b>
                MF#{current.masterFrame} SF{current.subframe}
              </b>
              {follow ? " (live)" : ` · ${cursor + 1} back`}
            </>
          )}
        </div>
      </div>

      <div className="timeline">
        {frames
          .slice()
          .reverse()
          .map((f, i) => {
            const idx = frames.length - 1 - i;
            const isCur = current && f.masterFrame === current.masterFrame && f.subframe === current.subframe;
            const completes = mode === "param" && f.completes.includes(trace?.tier ?? "");
            return (
              <button
                key={`${f.masterFrame}-${f.subframe}`}
                className={`tick ${isCur ? "cur" : ""} ${completes ? "completes" : ""}`}
                title={`MF#${f.masterFrame} SF${f.subframe} · ${f.spacecraftTime}${
                  f.completes.length ? ` · completes ${f.completes.join(", ")}` : ""
                }`}
                onClick={() => {
                  setPlaying(false);
                  setCursor(idx);
                }}
              >
                {f.subframe}
              </button>
            );
          })}
        {frames.length === 0 && <span className="dv-k">no frames buffered — start the feed</span>}
      </div>

      {mode === "frame" ? (
        <FrameView mf={mf ?? null} sf={sf ?? null} live={live} refreshMs={SPEEDS[speed].ms} />
      ) : trace?.error ? (
        <div className="empty">{trace.error}</div>
      ) : !trace?.steps ? (
        <div className="empty">select a parameter</div>
      ) : (
        <TraceLanes trace={trace} />
      )}
    </div>
  );
}
