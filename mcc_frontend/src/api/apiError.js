/**
 * One shape for every failed request, and the only place that reads an axios
 * error. The MCC backend answers a refusal with `{ error: "..." }`.
 *
 *   status   the HTTP status, or 0 when the request never reached the server
 *   message  what the server said, falling back to the caller's wording
 */
export const UNREACHABLE = 'The MCC backend is not reachable. Start it with "python mcc.py start", then try again.';

export function toRejection(error, fallback) {
  // Not an HTTP failure at all: an Error a caller threw itself, with its own words.
  if (error instanceof Error && !error.isAxiosError) {
    return { status: -1, message: error.message || fallback };
  }
  const status = error?.response?.status ?? 0;
  const data = error?.response?.data;
  const server = typeof data?.error === "string" && data.error ? data.error : data?.message;
  return {
    status,
    message: server || (status === 0 ? UNREACHABLE : fallback ?? `Request failed (HTTP ${status})`),
  };
}

/** The text to show an operator for a failed call. */
export const errorMessage = (error, fallback) => toRejection(error, fallback).message;

export default toRejection;
