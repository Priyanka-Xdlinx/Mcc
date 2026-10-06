import { createAsyncThunk, createSlice } from "@reduxjs/toolkit";
import telemetryApi from "../../api/telemetryApi.js";
import { toRejection } from "../../api/apiError.js";

/**
 * The full parameter table, patched from the telemetry socket.
 *
 * The table is fetched whole so every parameter the LUT downlinks is listed
 * even before its first reading: an empty row is meaningfully different from
 * a row that is absent. Socket updates replace only the rows they name, so an
 * unchanged row keeps its identity and memoised views skip it.
 *
 * The socket itself lives in `useTelemetryConnection`; this slice only holds
 * what it delivered.
 */
const initialState = {
  rows: [],
  /** parameter_id -> index into rows */
  indexById: {},
  /** `{ feed, simulator }` from GET /api/status */
  status: null,
  /** The socket is open. */
  live: false,
  /** null until the first status call answers. */
  backendUp: null,
  /** Browser time of the newest telemetry message. */
  lastMsgAt: null,
  /** Position and time of the newest subframe on the socket. */
  lastFrame: null,
};

/** The numeric reading behind a decoded value, or null when there is none. */
export function numberOf(value, quality) {
  if (quality !== "OK" && quality !== "WARMUP") return null;
  if (value === "true") return 1;
  if (value === "false") return 0;
  const n = parseFloat(value);
  return Number.isFinite(n) ? n : null;
}

export const loadLatest = createAsyncThunk("telemetry/loadLatest", async (_arg, { rejectWithValue }) => {
  try {
    return await telemetryApi.latest();
  } catch (error) {
    return rejectWithValue(toRejection(error));
  }
});

export const refreshStatus = createAsyncThunk("telemetry/refreshStatus", async (_arg, { rejectWithValue }) => {
  try {
    return await telemetryApi.status();
  } catch (error) {
    return rejectWithValue(toRejection(error));
  }
});

const telemetrySlice = createSlice({
  name: "telemetry",
  initialState,
  reducers: {
    socketOpened(state) {
      state.live = true;
    },
    socketClosed(state) {
      state.live = false;
    },
    feedStatusReceived(state, action) {
      if (state.status) state.status.feed = action.payload;
    },
    /** One `telemetry` socket message: `{ masterFrame, subframe, spacecraftTime, receivedAt, updates, at }`. */
    telemetryReceived(state, action) {
      const m = action.payload;
      for (const u of m.updates) {
        const i = state.indexById[u.parameterId];
        if (i === undefined) continue;
        const prev = state.rows[i];
        state.rows[i] = {
          ...prev,
          value: u.value,
          value_num: numberOf(u.value, u.quality),
          quality: u.quality,
          master_frame: m.masterFrame ?? prev.master_frame,
          subframe: m.subframe ?? prev.subframe,
          spacecraft_time: m.spacecraftTime ?? prev.spacecraft_time,
          received_at: m.receivedAt ?? prev.received_at,
          // Browser time of this reading: staleness is judged on this clock, not the backend's.
          seen_at: m.at,
        };
      }
      state.lastMsgAt = m.at;
      if (m.masterFrame !== undefined && m.subframe !== undefined) {
        state.lastFrame = {
          masterFrame: m.masterFrame,
          subframe: m.subframe,
          spacecraftTime: m.spacecraftTime ?? null,
          receivedAt: m.receivedAt ?? null,
        };
      }
    },
  },
  extraReducers: (builder) => {
    builder
      .addCase(loadLatest.fulfilled, (state, action) => {
        const rows = Array.isArray(action.payload) ? action.payload : [];
        state.rows = rows;
        state.indexById = Object.fromEntries(rows.map((r, i) => [r.parameter_id, i]));
      })
      .addCase(refreshStatus.fulfilled, (state, action) => {
        state.status = action.payload;
        state.backendUp = true;
      })
      .addCase(refreshStatus.rejected, (state) => {
        // Backend down or restarting: say so, and let the next tick recover.
        state.backendUp = false;
      });
  },
});

export const { socketOpened, socketClosed, feedStatusReceived, telemetryReceived } = telemetrySlice.actions;
export default telemetrySlice.reducer;
