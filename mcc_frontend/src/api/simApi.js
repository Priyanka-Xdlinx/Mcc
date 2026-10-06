import apiClient from "./client.js";

/**
 * Start and stop the XDLINX telemetry stack. The backend runs one fixed
 * command, so nothing sent from here reaches a shell. A 200 can still carry
 * `{ error }` when the stack refused to start.
 */
const control = (what) =>
  apiClient.post(`/api/sim/${what}`).then(({ data }) => {
    if (data?.error) throw new Error(data.error);
    return data;
  });

const simApi = {
  start: () => control("start"),
  stop: () => control("stop"),
};

export default simApi;
