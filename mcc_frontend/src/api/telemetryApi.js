import apiClient from "./client.js";

const get = (url, params) => apiClient.get(url, { params }).then(({ data }) => data);

/** Optional frame position: omitted means "the newest frame". */
const at = (mf, sf) => (mf != null && sf != null ? { mf, sf } : {});

/** Reads of decoded telemetry, the feed status and the decode internals. */
const telemetryApi = {
  /** Every downlinked parameter with its newest reading. */
  latest: () => get("/api/telemetry/latest"),
  /** Feed and simulator status. */
  status: () => get("/api/status"),
  history: (parameterId, limit) => get(`/api/telemetry/${parameterId}/history`, { limit }),
  /** Expected range of each parameter that has one (parameter_limit); the charts check against it. */
  limits: () => get("/api/telemetry/limits"),

  /** The subframes the backend has buffered, newest first. */
  frames: (limit = 320) => get("/api/frames", { limit }),
  frameDecode: (mf, sf) => get("/api/frame-decode", at(mf, sf)),
  trace: (parameterId, mf, sf) => get("/api/trace", { parameterId, ...at(mf, sf) }),

  /** The live database schema, for the Docs page. */
  schema: () => get("/api/schema"),
};

export default telemetryApi;
