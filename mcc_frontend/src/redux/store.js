import { configureStore } from "@reduxjs/toolkit";
import { authReducer } from "./auth/index.js";
import { notificationReducer, uiReducer } from "./app/index.js";
import { telemetryReducer } from "./telemetry/index.js";

/**
 * The parameter table is replaced up to four times a second and holds ~750
 * rows of plain server JSON. The dev-only serializability and immutability
 * checks would walk all of it on every dispatch, so they skip it.
 */
const BULK_PATHS = ["telemetry.rows", "telemetry.indexById"];

/** Exported so a test builds a store with the same shape the app has. */
export const rootReducer = {
  auth: authReducer,
  ui: uiReducer,
  notification: notificationReducer,
  telemetry: telemetryReducer,
};

export function makeStore(preloadedState) {
  return configureStore({
    reducer: rootReducer,
    preloadedState,
    middleware: (getDefaultMiddleware) =>
      getDefaultMiddleware({
        serializableCheck: { ignoredPaths: BULK_PATHS },
        immutableCheck: { ignoredPaths: BULK_PATHS },
      }),
  });
}

export const store = makeStore();
