import apiClient from "./client.js";

/**
 * Username/password sign-in. The backend sets HttpOnly session cookies; every
 * call here returns the signed-in user, or nothing.
 *
 *   POST /auth/login    {username, password} -> {user}
 *   POST /auth/refresh  -> {user}, renews the 15-minute access token
 *   POST /auth/logout   ends the session
 */
export const REFRESH_URL = "/auth/refresh";

const authApi = {
  login: (username, password) =>
    apiClient.post("/auth/login", { username, password }).then(({ data }) => data.user),

  refresh: () => apiClient.post(REFRESH_URL).then(({ data }) => data.user),

  logout: () => apiClient.post("/auth/logout"),

  changePassword: (currentPassword, newPassword) =>
    apiClient.post("/api/me/password", { currentPassword, newPassword }).then(({ data }) => data.user),
};

export default authApi;
