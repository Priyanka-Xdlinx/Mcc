import { can } from "../../../src/auth/permissions.js";
import { homeFor, pagesFor } from "../../../src/config/navigation.js";

const operator = {
  permissions: ["TELEMETRY_VIEW", "DECODE_VIEW", "DOCS_VIEW", "DISPLAY_VIEW", "DISPLAY_EDIT", "SIM_VIEW", "SIM_CONTROL"],
};
const viewerOnly = { permissions: ["DISPLAY_VIEW"] };
const nothing = { permissions: [] };

describe("can", () => {
  test("is true only for a permission the user holds", () => {
    expect(can(operator, "SIM_CONTROL")).toBe(true);
    expect(can(operator, "USERS_MANAGE")).toBe(false);
  });

  test("is false for a missing user", () => {
    expect(can(null, "TELEMETRY_VIEW")).toBe(false);
    expect(can(undefined, "TELEMETRY_VIEW")).toBe(false);
  });
});

describe("pages a user may open", () => {
  test("follow the permission each page needs", () => {
    expect(pagesFor(operator).map((p) => p.path)).toEqual(["/telemetry", "/charts", "/decode", "/display", "/docs"]);
    expect(pagesFor(viewerOnly).map((p) => p.path)).toEqual(["/display"]);
  });

  test("home is the first page the user may open, or null when there is none", () => {
    expect(homeFor(operator)).toBe("/telemetry");
    expect(homeFor(viewerOnly)).toBe("/display");
    expect(homeFor(nothing)).toBeNull();
  });
});
