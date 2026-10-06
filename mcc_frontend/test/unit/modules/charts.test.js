import { allIds, defaultDashboard, defaultTypeFor, parseDashboard, stats } from "../../../src/modules/charts/utils/dashboard.js";
import LiveBuffer from "../../../src/modules/charts/utils/liveBuffer.js";
import { sharedAxisProblem } from "../../../src/modules/charts/utils/limits.js";

describe("sharedAxisProblem", () => {
  const L = (lo, hi) => ({ lo, hi });

  it("allows series of similar size on one axis", () => {
    expect(sharedAxisProblem([{ name: "A", unit: "V", limit: L(0, 14) }, { name: "B", unit: "V", limit: L(-12, 12) }])).toBeNull();
  });

  it("refuses series whose sizes differ by more than 100x on one axis", () => {
    const p = sharedAxisProblem([{ name: "Bus V", unit: "V", limit: L(0, 14) }, { name: "Ripple", unit: "V", limit: L(0, 0.0001) }]);
    expect(p).toMatch(/Bus V can reach 14.0 V and Ripple only 0.000100 V/);
  });

  it("allows exactly 100x", () => {
    expect(sharedAxisProblem([{ name: "A", unit: "V", limit: L(0, 100) }, { name: "B", unit: "V", limit: L(0, 1) }])).toBeNull();
  });

  it("does not compare series with different units", () => {
    expect(sharedAxisProblem([{ name: "Volts", unit: "V", limit: L(0, 14) }, { name: "Amps", unit: "A", limit: L(0, 0.0001) }])).toBeNull();
  });

  it("cannot judge when a limit is missing or zero", () => {
    expect(sharedAxisProblem([{ name: "A", unit: "V", limit: L(0, 14) }, { name: "B", unit: "V", limit: null }])).toBeNull();
    expect(sharedAxisProblem([{ name: "A", unit: "V", limit: L(0, 0) }, { name: "B", unit: "V", limit: L(0, 1e6) }])).toBeNull();
  });

  it("needs at least two series", () => {
    expect(sharedAxisProblem([{ name: "A", unit: "V", limit: L(0, 14) }])).toBeNull();
  });
});

describe("LiveBuffer", () => {
  it("merges history and live readings on one sorted time axis, once per timestamp", () => {
    const b = new LiveBuffer(100);
    b.upsert(3000, { a: 3 });
    b.merge({ a: [[1000, 1], [3000, 30]], b: [[2000, 2]] });
    expect(b.times).toEqual([1000, 2000, 3000]);
    expect(b.slice("a", 0).y).toEqual([1, null, 30]);
    expect(b.slice("b", 1500)).toEqual({ x: [2000, 3000], y: [2, null] });
    expect(b.lastTs()).toBe(3000);
  });

  it("drops the oldest rows past its capacity", () => {
    const b = new LiveBuffer(10);
    for (let t = 0; t < 13; t++) b.upsert(t, { a: t });
    expect(b.times).toHaveLength(10);
    expect(b.times[0]).toBe(3);
  });

  it("notifies subscribers once per tick", async () => {
    const b = new LiveBuffer(10);
    const l = jest.fn();
    b.subscribe(l);
    b.upsert(1, { a: 1 });
    b.upsert(2, { a: 2 });
    await Promise.resolve();
    expect(l).toHaveBeenCalledTimes(1);
  });
});

describe("dashboard", () => {
  const rows = [
    { parameter_id: 10, subsystem: "GPS", name: "lat", data_type: "DOUBLE" },
    { parameter_id: 11, subsystem: "EPS", name: "EPS Mode", data_type: "ENUM" },
  ];

  it("builds the default sections from the parameters the LUT actually has", () => {
    const d = defaultDashboard(rows);
    expect(d.widgets.gps.map((w) => [w.type, w.ids])).toEqual([["plot", [10]]]);
    expect(d.widgets.eps.map((w) => [w.type, w.ids])).toEqual([["value", [11]]]);
    expect(allIds(d)).toEqual([10, 11]);
  });

  it("plots numbers and shows everything else as text", () => {
    expect(defaultTypeFor(rows[0])).toBe("plot");
    expect(defaultTypeFor(rows[1])).toBe("value");
    expect(defaultTypeFor(undefined)).toBe("value");
  });

  it("rejects stored layouts it cannot use", () => {
    expect(parseDashboard(undefined)).toBeNull();
    const d = parseDashboard({ sections: [{ key: "s", title: "S" }], widgets: { s: [{ uid: "u", type: "plot", ids: [1] }, { uid: "v", type: "pie", ids: [2] }] } });
    expect(d.widgets.s).toHaveLength(1);
  });

  it("stats ignore gaps", () => {
    expect(stats([1, null, 3])).toEqual({ min: 1, max: 3, avg: 2, count: 2 });
    expect(stats([null])).toBeNull();
  });
});
