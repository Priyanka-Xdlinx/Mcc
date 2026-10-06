/**
 * Rolling in-memory telemetry window: one sorted time axis, one column per
 * parameter. Rows arrive live (in order) and from history backfill (any order,
 * overlapping); both go through the same insert, so a timestamp is stored once.
 * A value that never arrived for a row is null.
 *
 * Subscribable (`subscribe` / `getVersion`) for useSyncExternalStore; many
 * writes in one tick notify once.
 */
export default class LiveBuffer {
  constructor(capacity) {
    this.capacity = capacity;
    this.times = [];
    this.cols = new Map();
    this.listeners = new Set();
    this.pending = false;
    this.version = 0;
  }

  /** Index of the first row with time >= t. */
  lowerBound(t) {
    let lo = 0;
    let hi = this.times.length;
    while (lo < hi) {
      const mid = (lo + hi) >> 1;
      if (this.times[mid] < t) lo = mid + 1;
      else hi = mid;
    }
    return lo;
  }

  column(key) {
    let c = this.cols.get(key);
    if (!c) {
      c = new Array(this.times.length).fill(null);
      this.cols.set(key, c);
    }
    return c;
  }

  rowIndex(ts) {
    const n = this.times.length;
    if (n === 0 || this.times[n - 1] < ts) {
      this.times.push(ts);
      for (const c of this.cols.values()) c.push(null);
      return n;
    }
    const i = this.lowerBound(ts);
    if (this.times[i] === ts) return i;
    this.times.splice(i, 0, ts);
    for (const c of this.cols.values()) c.splice(i, 0, null);
    return i;
  }

  /** One live reading per key at time `ts` (ms). */
  upsert(ts, values) {
    const i = this.rowIndex(ts);
    for (const [k, v] of Object.entries(values)) this.column(k)[i] = v === null || Number.isFinite(v) ? v : null;
    this.trim();
    this.changed();
  }

  /** Backfill: `{ key: [[ts, value], ...] }`. */
  merge(series) {
    for (const [k, pts] of Object.entries(series)) {
      this.column(k);
      for (const [ts, v] of pts) this.column(k)[this.rowIndex(ts)] = v;
    }
    this.trim();
    this.changed();
  }

  trim() {
    if (this.times.length <= this.capacity * 1.2) return;
    const drop = this.times.length - this.capacity;
    this.times.splice(0, drop);
    for (const c of this.cols.values()) c.splice(0, drop);
  }

  lastTs() {
    return this.times.length ? this.times[this.times.length - 1] : null;
  }

  /** Times and one column from `fromMs` on. */
  slice(key, fromMs) {
    const i = this.lowerBound(fromMs);
    const c = this.cols.get(key);
    return { x: this.times.slice(i), y: c ? c.slice(i) : new Array(this.times.length - i).fill(null) };
  }

  subscribe = (l) => {
    this.listeners.add(l);
    return () => this.listeners.delete(l);
  };

  getVersion = () => this.version;

  changed() {
    this.version++;
    if (this.pending) return;
    this.pending = true;
    queueMicrotask(() => {
      this.pending = false;
      for (const l of this.listeners) l();
    });
  }
}
