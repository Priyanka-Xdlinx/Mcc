/** Live telemetry: the parameter table, the feed status and the socket state. */
export { default as telemetryReducer } from "./telemetrySlice.js";
export {
  loadLatest,
  refreshStatus,
  socketOpened,
  socketClosed,
  feedStatusReceived,
  telemetryReceived,
  numberOf,
} from "./telemetrySlice.js";

export const selectRows = (state) => state.telemetry.rows;
export const selectStatus = (state) => state.telemetry.status;
export const selectLive = (state) => state.telemetry.live;
export const selectBackendUp = (state) => state.telemetry.backendUp;
export const selectLastMsgAt = (state) => state.telemetry.lastMsgAt;
export const selectLastFrame = (state) => state.telemetry.lastFrame;
/** The LUT in flight; layouts keyed by parameter id are only valid for one LUT. */
export const selectLutCrc = (state) => state.telemetry.status?.feed?.lutCrc16 ?? null;
