import { footprintRadiusKm, fmtLat, fmtLon, orbitMetrics, splitAtAntimeridian } from "../../../src/modules/orbit/utils/geo.js";
import { joinTrack } from "../../../src/modules/orbit/hooks/useGroundTrack.js";

it("splits a track where it wraps across ±180°", () => {
  const segs = splitAtAntimeridian([{ lon: 170 }, { lon: 179 }, { lon: -179 }, { lon: -170 }]);
  expect(segs.map((s) => s.map((p) => p.lon))).toEqual([[170, 179], [-179, -170]]);
});

it("measures period and inclination from northbound equator crossings", () => {
  // Two crossings 90 minutes apart, peaking at 51.6°.
  const track = [
    { t: 0, lat: -1, lon: 0 },
    { t: 10_000, lat: 1, lon: 1 },
    { t: 2_700_000, lat: 51.6, lon: 90 },
    { t: 5_400_000, lat: -1, lon: 0 },
    { t: 5_410_000, lat: 1, lon: 1 },
  ];
  const m = orbitMetrics(track);
  expect(m.ascendingNodes).toHaveLength(2);
  expect(m.periodSec).toBeCloseTo(5400, 0);
  expect(m.maxAbsLat).toBe(51.6);
  expect(m.heading).toBe("north");
});

it("does not time a crossing across a data gap", () => {
  expect(orbitMetrics([{ t: 0, lat: -1, lon: 0 }, { t: 120_000, lat: 1, lon: 0 }]).ascendingNodes).toEqual([]);
});

it("joins lat, lon and height on their timestamps", () => {
  const h = (v, ts = "2026-09-26 00:00:00.000") => ({ spacecraft_time: ts, received_at: null, value_num: v });
  const pts = joinTrack([h(10), h(null, "2026-09-26 00:00:01.000")], [h(20)], [h(500_000)]);
  expect(pts).toEqual([{ t: Date.UTC(2026, 8, 26), lat: 10, lon: 20, altKm: 500 }]);
});

it("formats positions and footprints", () => {
  expect(fmtLat(-12.5)).toBe("12.500° S");
  expect(fmtLon(77.25)).toBe("77.250° E");
  expect(Math.round(footprintRadiusKm(500))).toBe(2447);
});
