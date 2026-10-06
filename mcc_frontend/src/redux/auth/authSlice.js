import { createSlice } from "@reduxjs/toolkit";
import { restoreSession, signIn, signOut } from "./authThunks.js";

/**
 * Who is signed in, and how the auth requests are going.
 *
 *   user             { id, username, displayName, role: "ADMIN" | "OPERATOR", mustChangePassword,
 *                      permissions: ["TELEMETRY_VIEW", ...] }   (see auth/permissions.js)
 *   bootStatus       the session check on page load
 *   bootUnreachable  that check never reached the backend (not the same as "signed out")
 *   signedOutByUser  after a deliberate sign-out the next sign-in does not return to the old page
 */
const initialState = {
  user: null,
  error: "",
  signedOutByUser: false,
  bootStatus: "idle",
  bootUnreachable: false,
  signInStatus: "idle",
  signOutStatus: "idle",
};

function applySignedOut(state) {
  state.user = null;
  state.signedOutByUser = true;
  state.signOutStatus = "idle";
}

const authSlice = createSlice({
  name: "auth",
  initialState,
  reducers: {
    /** A fresh user object from the server: after a password change, or a token refresh. */
    signedIn(state, action) {
      state.user = action.payload;
      state.error = "";
    },
    /** The session ended behind the page's back (expired, revoked, account disabled). */
    sessionEnded(state) {
      state.user = null;
      state.signedOutByUser = false;
    },
    clearAuthError(state) {
      state.error = "";
    },
  },
  extraReducers: (builder) => {
    builder
      .addCase(restoreSession.pending, (state) => {
        state.bootStatus = "pending";
        state.bootUnreachable = false;
        state.error = "";
      })
      .addCase(restoreSession.fulfilled, (state, action) => {
        state.bootStatus = "succeeded";
        if (action.payload) state.user = action.payload;
      })
      .addCase(restoreSession.rejected, (state, action) => {
        state.bootStatus = "failed";
        state.bootUnreachable = Boolean(action.payload?.unreachable);
        state.error = action.payload?.message ?? "";
      })

      .addCase(signIn.pending, (state) => {
        state.signInStatus = "pending";
        state.error = "";
      })
      .addCase(signIn.fulfilled, (state, action) => {
        state.signInStatus = "idle";
        state.user = action.payload;
        state.signedOutByUser = false;
        state.bootUnreachable = false;
      })
      .addCase(signIn.rejected, (state, action) => {
        state.signInStatus = "failed";
        state.error = action.payload?.message ?? "Sign-in failed.";
      })

      .addCase(signOut.pending, (state) => {
        state.signOutStatus = "pending";
      })
      .addCase(signOut.fulfilled, applySignedOut);
  },
});

export const { signedIn, sessionEnded, clearAuthError } = authSlice.actions;

export default authSlice.reducer;
