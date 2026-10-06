import {
  arrangeGeometry,
  clampAll,
  MIN_W,
  nextPageNo,
  normParams,
  parseLayout,
  parseWorkspace,
  raise,
  serializeLayout,
  serializeWorkspace,
  SLOTS,
  toWinStates,
  uniquePageNos,
} from "../../../src/modules/display/utils/layout.js";

describe("parseLayout", () => {
  it("reads a stored layout, from an object or a JSON string", () => {
    const stored = { v: 1, windows: [{ key: "a", pageId: 3, draft: null, x: 10, y: 20, w: 800, h: 500, z: 2, pageNo: 1 }] };
    expect(parseLayout(stored)).toEqual(stored.windows);
    expect(parseLayout(JSON.stringify(stored))).toEqual(stored.windows);
  });

  it("drops windows with neither a saved page nor a draft, and fills missing fields", () => {
    const out = parseLayout({ windows: [{ key: "x" }, { draft: { params: [1, "two", 3.5] } }] });
    expect(out).toHaveLength(1);
    expect(out[0].draft.name).toBe("Untitled page");
    expect(out[0].draft.params).toHaveLength(SLOTS);
    expect(out[0].draft.params.slice(0, 3)).toEqual([1, null, null]);
    expect(out[0].pageNo).toBe(1);
  });

  it("returns null for anything that is not a layout", () => {
    expect(parseLayout(null)).toBeNull();
    expect(parseLayout("{not json")).toBeNull();
    expect(parseLayout({ windows: "nope" })).toBeNull();
  });

  it("round-trips through serializeLayout without the client-only fields", () => {
    const ws = toWinStates(parseLayout({ windows: [{ key: "k", pageId: 9, x: 1, y: 2, w: 900, h: 600, z: 1, pageNo: 2 }] }));
    expect(ws[0]).toMatchObject({ page: null, loading: true });
    expect(parseLayout(serializeLayout(ws))).toEqual([{ key: "k", pageId: 9, draft: null, x: 1, y: 2, w: 900, h: 600, z: 1, pageNo: 2 }]);
  });
});

describe("parseWorkspace", () => {
  it("reads a v2 workspace: several screens plus which one was active", () => {
    const stored = {
      v: 2,
      active: "b",
      screens: [
        { id: "a", name: "Screen 1", windows: [] },
        { id: "b", name: "Mine", windows: [{ key: "k", pageId: 1, draft: null, x: 1, y: 2, w: 900, h: 600, z: 1, pageNo: 1 }] },
      ],
    };
    const ws = parseWorkspace(stored);
    expect(ws.active).toBe("b");
    expect(ws.screens.map((s) => s.name)).toEqual(["Screen 1", "Mine"]);
    expect(ws.screens[1].windows).toHaveLength(1);
  });

  it("reads a v1 layout (no screens array) as a single screen", () => {
    const ws = parseWorkspace({ v: 1, windows: [{ key: "k", pageId: 1, draft: null, x: 0, y: 0, w: 800, h: 500, z: 1, pageNo: 1 }] });
    expect(ws.screens).toHaveLength(1);
    expect(ws.screens[0].name).toBe("Screen 1");
    expect(ws.active).toBe(ws.screens[0].id);
    expect(ws.screens[0].windows).toHaveLength(1);
  });

  it("falls back to the first screen when the active id names none of them", () => {
    const ws = parseWorkspace({ v: 2, active: "nope", screens: [{ id: "a", name: "A", windows: [] }] });
    expect(ws.active).toBe("a");
  });

  it("returns null for anything unusable", () => {
    expect(parseWorkspace(null)).toBeNull();
    expect(parseWorkspace("{not json")).toBeNull();
    expect(parseWorkspace({ v: 2, screens: [] })).toBeNull();
  });

  it("round-trips through serializeWorkspace", () => {
    const screens = [{ id: "a", name: "Screen 1", windows: [] }];
    const again = parseWorkspace(serializeWorkspace(screens, "a"));
    expect(again).toEqual({ active: "a", screens: [{ id: "a", name: "Screen 1", windows: [] }] });
  });
});

describe("page numbers", () => {
  it("gives the smallest free number", () => {
    expect(nextPageNo([{ pageNo: 1 }, { pageNo: 3 }])).toBe(2);
    expect(nextPageNo([])).toBe(1);
  });

  it("renumbers only missing and repeated numbers", () => {
    expect(uniquePageNos([{ pageNo: 2 }, { pageNo: 2 }, { pageNo: 0 }]).map((w) => w.pageNo)).toEqual([2, 1, 3]);
  });
});

it("normParams always gives 20 integer-or-null slots", () => {
  expect(normParams(undefined)).toEqual(Array(SLOTS).fill(null));
  expect(normParams([7, null, "x"]).slice(0, 3)).toEqual([7, null, null]);
});

describe("geometry", () => {
  const win = (key, extra) => ({ key, x: 0, y: 0, w: 600, h: 400, z: 1, ...extra });

  it("clampAll pulls windows back onto a narrower desk and leaves good ones alone", () => {
    const good = win("a");
    const out = clampAll([good, win("b", { x: 2000, y: -5 })], 1000);
    expect(out[0]).toBe(good);
    expect(out[1]).toMatchObject({ x: 400, y: 0 });
  });

  it("raise brings a window to the front only when it is not already alone there", () => {
    const ws = [win("a", { z: 1 }), win("b", { z: 2 })];
    expect(raise(ws, "b")).toBe(ws);
    expect(raise(ws, "a").find((w) => w.key === "a").z).toBe(3);
  });

  it("tile packs every window into a grid without overlap", () => {
    const ws = [win("a"), win("b"), win("c")];
    const geo = arrangeGeometry(ws, "tile", { w: 1300, h: 800, top: 0 });
    expect([...geo.keys()]).toEqual(["a", "b", "c"]);
    const [a, b] = [geo.get("a"), geo.get("b")];
    expect(a.w).toBeGreaterThanOrEqual(MIN_W);
    expect(b.x).toBeGreaterThanOrEqual(a.x + a.w);
  });
});
