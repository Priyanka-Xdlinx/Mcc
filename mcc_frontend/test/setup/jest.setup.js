import "@testing-library/jest-dom";
import { TextDecoder, TextEncoder } from "node:util";

// jsdom lacks these; react-router's URL handling and the password policy need them.
if (typeof global.TextEncoder === "undefined") {
  Object.assign(global, { TextEncoder, TextDecoder });
}

beforeEach(() => {
  try {
    window.localStorage?.clear();
  } catch {
    /* no storage in this environment */
  }
});
