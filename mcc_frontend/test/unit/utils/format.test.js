import { ago, crcHex, displayValue, plural, spacecraftMs, utcClock } from "../../../src/utils/format.js";

describe("displayValue", () => {
  it("keeps short and ordinary values as the decoder wrote them", () => {
    expect(displayValue("3.732 V")).toBe("3.732 V");
    expect(displayValue("123456789012 (Aborted)")).toBe("123456789012 (Aborted)");
  });

  it("shortens pattern-filled floats to scientific notation, keeping the unit", () => {
    expect(displayValue("-250316724809270461596968490379509760.000 V")).toBe("-2.503e+35 V");
  });

  it("shows a dash for no value", () => {
    expect(displayValue(null)).toBe("—");
  });
});

describe("crcHex", () => {
  it("prints like XDOS does", () => {
    expect(crcHex(0x1a2b)).toBe("0x1A2B");
    expect(crcHex(5)).toBe("0x0005");
    expect(crcHex(-1)).toBe("—");
    expect(crcHex(null)).toBe("—");
  });
});

describe("ago", () => {
  it("reads naturally at each scale", () => {
    expect(ago(null, 0)).toBe("never");
    expect(ago(0, 400)).toBe("0.4 s ago");
    expect(ago(0, 12_000)).toBe("12 s ago");
    expect(ago(0, 180_000)).toBe("3 min ago");
  });
});

describe("spacecraftMs", () => {
  it("reads spacecraft time as UTC", () => {
    expect(spacecraftMs("2026-09-26 18:52:56.883")).toBe(Date.UTC(2026, 8, 26, 18, 52, 56, 883));
  });

  it("falls back to the receive time", () => {
    expect(spacecraftMs(null, "2026-09-26T00:00:00Z")).toBe(Date.UTC(2026, 8, 26));
    expect(spacecraftMs(null, null)).toBeNaN();
  });
});

it("utcClock keeps the time of day", () => {
  expect(utcClock("2026-09-26T18:52:56.883Z")).toBe("18:52:56.883 UTC");
  expect(utcClock(null)).toBeNull();
});

it("plural", () => {
  expect(plural(1, "row")).toBe("1 row");
  expect(plural(1200, "row")).toBe(`${(1200).toLocaleString()} rows`);
});
