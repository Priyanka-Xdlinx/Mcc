import { can } from "../auth/permissions.js";

/**
 * The signed-in pages, in tab order. `path` is the route; `icon` a kit icon name;
 * `perm` the permission that opens it. `hidden: true` keeps a page's code but
 * takes it out of the tabs and the routes.
 */
export const PAGES = [
  { path: "/telemetry", label: "Telemetry", icon: "telemetry", hint: "Every parameter the LUT downlinks, live", perm: "TELEMETRY_VIEW" },
  { path: "/charts", label: "Charts", icon: "charts", hint: "Values over time", perm: "TELEMETRY_VIEW" },
  { path: "/decode", label: "Decode trace", icon: "decode", hint: "From payload bytes to engineering value", perm: "DECODE_VIEW" },
  { path: "/display", label: "Display", icon: "display", hint: "Display pages: floating windows of 20 parameters", perm: "DISPLAY_VIEW" },
  { path: "/orbit", label: "Orbit", icon: "orbit", hint: "Ground track from the GPS position", hidden: true, perm: "TELEMETRY_VIEW" },
  { path: "/docs", label: "Docs", icon: "docs", hint: "Database schema, REST API (Swagger), live stream", perm: "DOCS_VIEW" },
];

/** The pages operators can open. */
export const VISIBLE_PAGES = PAGES.filter((p) => !p.hidden);

export const isVisible = (path) => VISIBLE_PAGES.some((p) => p.path === path);

export const HOME = VISIBLE_PAGES[0].path;

/** The visible pages this user may open, in tab order. */
export const pagesFor = (user) => VISIBLE_PAGES.filter((p) => can(user, p.perm));

/** Where this user lands: their first page, or null when they may open none. */
export const homeFor = (user) => pagesFor(user)[0]?.path ?? null;

/** The page a pathname belongs to ("/docs/api" -> "/docs"). */
export const pageOf = (pathname) => VISIBLE_PAGES.find((p) => pathname === p.path || pathname.startsWith(`${p.path}/`));
