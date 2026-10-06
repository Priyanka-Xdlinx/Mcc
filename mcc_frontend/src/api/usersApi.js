import apiClient from "./client.js";

/** Sign-in accounts, for administrators. */
const usersApi = {
  list: () => apiClient.get("/api/admin/users").then(({ data }) => data),
  create: (account) => apiClient.post("/api/admin/users", account).then(({ data }) => data),
  update: (id, change) => apiClient.patch(`/api/admin/users/${id}`, change).then(({ data }) => data),
  resetPassword: (id, password) => apiClient.put(`/api/admin/users/${id}/password`, { password }),
  remove: (id) => apiClient.delete(`/api/admin/users/${id}`),
};

export default usersApi;
