import env from "../config/env.js";

/**
 * Console logging behind one switch. `warn` and `error` always print: they are
 * what anybody opening F12 on a deployed build is looking for.
 */
const alwaysPrinted = (level) => level === "error" || level === "warn";

function logger(level, ...args) {
  if (!env.enableConsoleLogs && !alwaysPrinted(level)) return;
  const method = typeof console[level] === "function" ? level : "log";
  console[method](...args);
}

logger.log = (...args) => logger("log", ...args);
logger.info = (...args) => logger("info", ...args);
logger.warn = (...args) => logger("warn", ...args);
logger.error = (...args) => logger("error", ...args);
logger.debug = (...args) => logger("debug", ...args);

export default logger;
