/**
 * Runtime configuration: every environment-dependent value the app reads, in
 * one place, so no service or component touches `import.meta.env` itself.
 *
 * Every value has a fallback, so a checkout with no `.env` runs against the
 * backend through the Vite proxy.
 */

// Vite replaces `import.meta.env` at build time; under Jest, Babel rewrites
// `import.meta` to `process` (see babel.config.cjs).
const source = import.meta.env;

const asBoolean = (value, fallback) =>
  value === undefined || value === "" ? fallback : String(value).toLowerCase() === "true";

export const env = {
  /** REST and auth base URL. Empty: same origin (the dev proxy, or the backend serving the build). */
  apiBaseUrl: source.VITE_API_BASE_URL ?? "",

  /** axios waits forever by default; a hung session check would leave the boot screen up for good. */
  requestTimeoutMs: Number(source.VITE_REQUEST_TIMEOUT_MS) || 15000,

  /** Whether logger.log/info/debug print. On in a dev server, off in a build. */
  enableConsoleLogs: asBoolean(source.VITE_ENABLE_CONSOLE_LOGS, Boolean(source.DEV)),
};

/** The telemetry WebSocket, on the same host as the page (or the configured API host). */
export function telemetrySocketUrl() {
  const base = env.apiBaseUrl ? new URL(env.apiBaseUrl) : window.location;
  const proto = base.protocol === "https:" ? "wss" : "ws";
  return `${proto}://${base.host}/ws/telemetry`;
}

export default env;
