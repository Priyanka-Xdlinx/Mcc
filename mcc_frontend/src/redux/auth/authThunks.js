import { createAsyncThunk } from "@reduxjs/toolkit";
import authApi from "../../api/authApi.js";
import { toRejection, UNREACHABLE } from "../../api/apiError.js";
import logger from "../../utils/logger.js";

/**
 * Every async call the auth feature makes. Reducers live in authSlice.js;
 * thunks reject with a plain `{ status, message }` (never an Error, which is
 * not serialisable) and the slice owns the resulting state.
 */

/**
 * Exchanges the refresh cookie for an access token and the current user.
 * Resolves `null` for "nobody is signed in" - a first visit legitimately 401s.
 * Rejects with `unreachable: true` when the backend never answered, because a
 * login page is the wrong answer to that.
 */
export const restoreSession = createAsyncThunk("auth/restoreSession", async (_arg, { rejectWithValue }) => {
  try {
    return await authApi.refresh();
  } catch (error) {
    const status = error?.response?.status ?? 0;
    if (status === 401 || status === 403) return null;
    // A backend from before sign-in existed has no /auth endpoints.
    if (status === 404) {
      return rejectWithValue({
        status,
        unreachable: true,
        message:
          "The MCC backend on port 8080 is an older build without sign-in. Stop it, rebuild the jar " +
          "(mvn -DskipTests package in backend/) and start it again.",
      });
    }
    // The Vite dev server answers 500/502/504 when it cannot reach the backend.
    if (status === 0 || status >= 500) return rejectWithValue({ status, unreachable: true, message: UNREACHABLE });
    logger.error("auth/restoreSession", error);
    return rejectWithValue({ status, unreachable: true, message: `The MCC backend answered HTTP ${status}.` });
  }
});

export const signIn = createAsyncThunk(
  "auth/signIn",
  async ({ username, password }, { rejectWithValue }) => {
    try {
      return await authApi.login(username, password);
    } catch (error) {
      return rejectWithValue(toRejection(error, "Sign-in failed."));
    }
  },
  { condition: (_arg, { getState }) => getState().auth.signInStatus !== "pending" },
);

/** Ends the session. The local session is cleared even if the server call fails; the next load re-checks. */
export const signOut = createAsyncThunk(
  "auth/signOut",
  async () => {
    try {
      await authApi.logout();
    } catch (error) {
      logger.warn("auth/signOut", error);
    }
    return null;
  },
  { condition: (_arg, { getState }) => getState().auth.signOutStatus !== "pending" },
);
