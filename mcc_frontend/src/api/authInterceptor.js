import apiClient from "./client.js";
import { REFRESH_URL } from "./authApi.js";
import { store } from "../redux/store.js";
import { sessionEnded, signedIn } from "../redux/auth/index.js";

// Kept out of client.js to avoid a circular import (client.js is reached from
// store.js through the thunks). Imported once, by main.jsx.

// One shared refresh, so five requests failing together do one refresh, not five.
let inFlightRefresh = null;

function refreshOnce() {
  inFlightRefresh ??= apiClient
    .post(REFRESH_URL)
    .then(({ data }) => {
      store.dispatch(signedIn(data.user));
      return true;
    })
    .catch(() => false)
    .finally(() => {
      inFlightRefresh = null;
    });
  return inFlightRefresh;
}

/**
 * Access tokens live 15 minutes, so a 401 from /api/ usually means only that
 * one expired while the tab was open: refresh once and retry. If the session
 * itself has ended, the app goes back to the sign-in page.
 */
apiClient.interceptors.response.use(
  (response) => response,
  async (error) => {
    const { response, config } = error;
    if (response?.status !== 401 || !config || !String(config.url).startsWith("/api/")) {
      return Promise.reject(error);
    }
    if (!config._retriedAfterRefresh && (await refreshOnce())) {
      config._retriedAfterRefresh = true;
      return apiClient(config);
    }
    // Guarded on a user, so a first visit's expected 401 is not read as a sign-out.
    if (store.getState().auth.user) store.dispatch(sessionEnded());
    return Promise.reject(error);
  },
);
