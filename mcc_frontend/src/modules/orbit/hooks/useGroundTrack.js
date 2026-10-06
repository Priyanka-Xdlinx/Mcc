import { useEffect, useMemo, useRef, useState } from "react";
import telemetryApi from "../../../api/telemetryApi.js";
import { spacecraftMs } from "../../../utils/format.js";

const REFRESH_MS = 4000;
const HISTORY_LIMIT = 5000;

const findGps = (rows, message, name) => rows.find((r) => r.subsystem === "GPS" && r.message === message && r.name === name);

/** Joins the lat, lon and height histories on their timestamp into track points. */
export function joinTrack(la, lo, al) {
  const byTime = (h) => new Map(h.filter((x) => x.value_num !== null).map((x) => [x.spacecraft_time ?? x.received_at, x]));
  const lonMap = byTime(lo);
  const altMap = byTime(al);
  const pts = [];
  for (const x of la) {
    if (x.value_num === null) continue;
    const key = x.spacecraft_time ?? x.received_at;
    const l = lonMap.get(key);
    if (!l) continue;
    const t = spacecraftMs(x.spacecraft_time, x.received_at);
    if (!Number.isFinite(t)) continue;
    pts.push({ t, lat: x.value_num, lon: l.value_num, altKm: (altMap.get(key)?.value_num ?? 0) / 1000 });
  }
  return pts.sort((a, b) => a.t - b.t);
}

/**
 * The spacecraft's track from the GPS BESTPOS position (lat, lon, hgt): the
 * stored history of this run, refreshed every few seconds, plus the live
 * reading in between. `gps` is null when the LUT does not downlink the position.
 */
export default function useGroundTrack(rows, live) {
  const lat = findGps(rows, "BESTPOS", "lat");
  const lon = findGps(rows, "BESTPOS", "lon");
  const hgt = findGps(rows, "BESTPOS", "hgt");
  const gps = lat && lon && hgt ? { lat, lon, hgt } : null;
  const ids = gps ? `${lat.parameter_id},${lon.parameter_id},${hgt.parameter_id}` : null;

  const [history, setHistory] = useState([]);
  const req = useRef(0);

  useEffect(() => {
    if (!ids) return undefined;
    let stop = false;
    const load = async () => {
      const mine = ++req.current;
      try {
        const [la, lo, al] = await Promise.all(ids.split(",").map((id) => telemetryApi.history(id, HISTORY_LIMIT)));
        if (!stop && mine === req.current) setHistory(joinTrack(la, lo, al));
      } catch {
        /* backend unreachable: keep the last track */
      }
    };
    void load();
    const t = live ? setInterval(load, REFRESH_MS) : null;
    return () => {
      stop = true;
      if (t) clearInterval(t);
    };
  }, [ids, live]);

  // The position right now, straight from the live stream.
  const current = useMemo(() => {
    const t = lat ? spacecraftMs(lat.spacecraft_time, lat.received_at) : NaN;
    const good = lat?.quality === "OK" && lon?.quality === "OK" && lat.value_num !== null && lon.value_num !== null;
    if (!good || !Number.isFinite(t)) return history[history.length - 1] ?? null;
    return { t, lat: lat.value_num, lon: lon.value_num, altKm: (hgt?.value_num ?? 0) / 1000 };
  }, [lat, lon, hgt, history]);

  const track = useMemo(() => {
    const last = history[history.length - 1];
    return current && (!last || current.t > last.t) ? [...history, current] : history;
  }, [history, current]);

  return { gps, track, current };
}
