import axios from "axios";

import env from "../config/env.js";

const SAFE_METHODS = ["get", "head", "options"];
const isWrite = (config) => !SAFE_METHODS.includes(String(config.method ?? "get").toLowerCase());

/**
 * The one HTTP client. Session tokens live in HttpOnly cookies, so this code
 * never sees them; it only has to send cookies and the CSRF header.
 *
 * Double-submit CSRF: axios copies the readable XSRF-TOKEN cookie into an
 * X-XSRF-TOKEN header (its defaults match the backend's names). Writes only -
 * the backend ignores the header on reads.
 */
const apiClient = axios.create({
  baseURL: env.apiBaseUrl,
  withCredentials: true,
  timeout: env.requestTimeoutMs,
  withXSRFToken: isWrite,
});

export function xsrfToken() {
  const m = document.cookie.match(/(?:^|;\s*)XSRF-TOKEN=([^;]*)/);
  return m ? decodeURIComponent(m[1]) : null;
}

// A page served by the Vite dev server has no XSRF-TOKEN cookie until the
// backend has set one. Ask for it before the first write; axios reads the
// cookie in its adapter, which runs after this interceptor.
apiClient.interceptors.request.use(async (config) => {
  if (isWrite(config) && !xsrfToken()) {
    try {
      await axios.get(`${env.apiBaseUrl}/auth/csrf`, { withCredentials: true, timeout: env.requestTimeoutMs });
    } catch {
      /* the request it guards will fail and say so */
    }
  }
  return config;
});

export default apiClient;
