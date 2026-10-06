import apiClient, { xsrfToken } from "./client.js";
import env from "../config/env.js";

/**
 * Display pages and the signed-in user's saved window layout. The backend
 * takes the owner from the session: pages saved here belong to whoever is
 * signed in.
 */
const data = (p) => p.then((r) => r.data);

const WORKSPACE_URL = "/api/me/workspace";

const displayApi = {
  pages: () => data(apiClient.get("/api/pages")),
  page: (id) => data(apiClient.get(`/api/pages/${id}`)),
  createPage: (draft) => data(apiClient.post("/api/pages", draft)),
  updatePage: (id, draft) => data(apiClient.put(`/api/pages/${id}`, draft)),
  deletePage: (id) => apiClient.delete(`/api/pages/${id}`),

  workspace: () => data(apiClient.get(WORKSPACE_URL)),
  saveWorkspace: (layout) => apiClient.put(WORKSPACE_URL, { layout }),

  /**
   * The same save, for a page that is closing: `fetch` with keepalive outlives
   * the tab, which an axios request does not.
   */
  saveWorkspaceOnUnload(layout) {
    const token = xsrfToken();
    try {
      void fetch(`${env.apiBaseUrl}${WORKSPACE_URL}`, {
        method: "PUT",
        keepalive: true,
        credentials: "include",
        headers: { "Content-Type": "application/json", ...(token ? { "X-XSRF-TOKEN": token } : {}) },
        body: JSON.stringify({ layout }),
      });
    } catch {
      /* the page is going away; nothing more to do */
    }
  },
};

export default displayApi;
