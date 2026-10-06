import { useEffect, useMemo, useRef, useSyncExternalStore } from "react";
import telemetryApi from "../../../api/telemetryApi.js";
import { PERIOD_S, spacecraftMs } from "../../../utils/format.js";
import LiveBuffer from "../utils/liveBuffer.js";

/** Rows kept in memory, across every parameter. */
const CAPACITY = 4000;
/** History asked for per parameter: about 30 minutes at its own rate. */
const HISTORY_S = 30 * 60;

/**
 * A rolling buffer of the given parameters' numeric readings: back-filled from
 * stored history the first time a parameter is needed, then fed from the live
 * rows. Returns the buffer; components re-render whenever it changes.
 */
export default function useChartBuffer(ids, rowById) {
  const buffer = useMemo(() => new LiveBuffer(CAPACITY), []);
  useSyncExternalStore(buffer.subscribe, buffer.getVersion);
  const loaded = useRef(new Set());
  const lastSeen = useRef(new Map());

  // Backfill each parameter once.
  useEffect(() => {
    for (const id of ids) {
      if (loaded.current.has(id)) continue;
      loaded.current.add(id);
      const period = PERIOD_S[rowById.get(id)?.tier] ?? 1;
      const limit = Math.min(5000, Math.ceil(HISTORY_S / period) + 5);
      telemetryApi
        .history(id, limit)
        .then((h) => {
          const pts = h.map((x) => [spacecraftMs(x.spacecraft_time, x.received_at), x.value_num]).filter(([t]) => Number.isFinite(t));
          buffer.merge({ [id]: pts });
        })
        .catch(() => loaded.current.delete(id)); // retried on the next change
    }
  }, [ids, rowById, buffer]);

  // Feed new readings as the live rows change.
  useEffect(() => {
    for (const id of ids) {
      const r = rowById.get(id);
      if (!r?.spacecraft_time || lastSeen.current.get(id) === r.spacecraft_time) continue;
      lastSeen.current.set(id, r.spacecraft_time);
      const t = spacecraftMs(r.spacecraft_time);
      if (Number.isFinite(t)) buffer.upsert(t, { [id]: r.quality === "OK" || r.quality === "WARMUP" ? r.value_num : null });
    }
  }, [ids, rowById, buffer]);

  return buffer;
}
