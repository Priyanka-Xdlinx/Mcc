import { makeStore } from "../../../src/redux/store.js";
import { loadLatest, numberOf, telemetryReceived } from "../../../src/redux/telemetry/index.js";

const row = (id, extra = {}) => ({
  parameter_id: id,
  subsystem: "EPS",
  message: "M",
  entry: "EP-1",
  tier: "VH",
  name: `p${id}`,
  unit: "V",
  data_type: "FLOAT",
  byte_aligned: 1,
  value: null,
  value_num: null,
  quality: null,
  master_frame: null,
  subframe: null,
  spacecraft_time: null,
  received_at: null,
  ...extra,
});

function storeWithRows(rows) {
  const store = makeStore();
  store.dispatch({ type: loadLatest.fulfilled.type, payload: rows });
  return store;
}

it("numberOf reads only trustworthy values", () => {
  expect(numberOf("3.73 V", "OK")).toBe(3.73);
  expect(numberOf("true", "WARMUP")).toBe(1);
  expect(numberOf("3.73 V", "NO_ANSWER")).toBeNull();
  expect(numberOf("n/a", "OK")).toBeNull();
});

describe("telemetryReceived", () => {
  it("patches only the named rows and leaves the others identical", () => {
    const store = storeWithRows([row(1), row(2)]);
    const before = store.getState().telemetry.rows;
    store.dispatch(
      telemetryReceived({
        masterFrame: 4,
        subframe: 7,
        spacecraftTime: "2026-09-26 00:00:01.000",
        receivedAt: "2026-09-26T00:00:01.100Z",
        updates: [{ parameterId: 2, value: "3.70 V", quality: "OK" }],
        at: 1000,
      }),
    );
    const { rows, lastFrame, lastMsgAt } = store.getState().telemetry;
    expect(rows[0]).toBe(before[0]);
    expect(rows[1]).toMatchObject({ value: "3.70 V", value_num: 3.7, quality: "OK", master_frame: 4, subframe: 7, seen_at: 1000 });
    expect(lastFrame).toEqual({ masterFrame: 4, subframe: 7, spacecraftTime: "2026-09-26 00:00:01.000", receivedAt: "2026-09-26T00:00:01.100Z" });
    expect(lastMsgAt).toBe(1000);
  });

  it("ignores parameters the table does not have", () => {
    const store = storeWithRows([row(1)]);
    store.dispatch(telemetryReceived({ updates: [{ parameterId: 99, value: "1", quality: "OK" }], at: 5 }));
    expect(store.getState().telemetry.rows).toHaveLength(1);
  });
});
