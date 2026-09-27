// Rolling in-memory telemetry window: one sorted time axis, one column per channel. Rows arrive from live
// SSE (in order) and from history backfill (any order, possibly overlapping); both go through upsert, so
// the same timestamp is stored once. A value that never arrived for a row is null ("No data").

type Listener = () => void;

export class LiveBuffer {
  readonly times: number[] = [];
  private readonly cols = new Map<string, (number | null)[]>();
  private listeners = new Set<Listener>();
  private pending = false;
  version = 0;

  constructor(readonly capacity: number) {}

  /** Index of the first row with time >= t. */
  lowerBound(t: number): number {
    let lo = 0;
    let hi = this.times.length;
    while (lo < hi) {
      const mid = (lo + hi) >> 1;
      if (this.times[mid]! < t) lo = mid + 1;
      else hi = mid;
    }
    return lo;
  }

  private column(key: string): (number | null)[] {
    let c = this.cols.get(key);
    if (!c) {
      c = new Array<number | null>(this.times.length).fill(null);
      this.cols.set(key, c);
    }
    return c;
  }

  private rowIndex(ts: number): number {
    const n = this.times.length;
    if (n === 0 || this.times[n - 1]! < ts) {
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

  upsert(ts: number, values: Readonly<Record<string, number | null>>): void {
    const i = this.rowIndex(ts);
    for (const [k, v] of Object.entries(values))
      this.column(k)[i] = v === null || Number.isFinite(v) ? v : null;
    this.trim();
    this.changed();
  }

  /** Backfill from GET /telemetry `series`. */
  merge(series: Readonly<Record<string, readonly (readonly [number, number | null])[]>>): void {
    for (const [k, pts] of Object.entries(series)) {
      this.column(k);
      for (const [ts, v] of pts) this.column(k)[this.rowIndex(ts)] = v;
    }
    this.trim();
    this.changed();
  }

  private trim() {
    if (this.times.length <= this.capacity * 1.2) return;
    const drop = this.times.length - this.capacity;
    this.times.splice(0, drop);
    for (const c of this.cols.values()) c.splice(0, drop);
  }

  get length(): number {
    return this.times.length;
  }

  lastTs(): number | null {
    return this.times.length ? this.times[this.times.length - 1]! : null;
  }

  /** Value of `key` in the latest row (null when that row has no value for it). */
  latest(key: string): number | null {
    const c = this.cols.get(key);
    return c && c.length ? (c[c.length - 1] ?? null) : null;
  }

  /** Time of the latest row that has a value for `key` (null when none in the buffer). */
  lastValueTs(key: string): number | null {
    const c = this.cols.get(key);
    if (!c) return null;
    for (let i = c.length - 1; i >= 0; i--) if (c[i] !== null && c[i] !== undefined) return this.times[i]!;
    return null;
  }

  has(key: string): boolean {
    return this.cols.has(key);
  }

  /** Times and one column from `fromMs` on. */
  slice(key: string, fromMs: number): { x: number[]; y: (number | null)[] } {
    const i = this.lowerBound(fromMs);
    const c = this.cols.get(key);
    return {
      x: this.times.slice(i),
      y: c ? c.slice(i) : new Array<number | null>(this.times.length - i).fill(null),
    };
  }

  /**
   * Row objects for the given keys from `fromMs` on (for derived formulas).
   * MCC: parameters arrive every 1, 2, 4 or 8 s, so an input is held at its last reading for up to `holdMs`
   * (sample-and-hold); older than that it is missing and so is the result.
   */
  rows(keys: readonly string[], fromMs: number, holdMs = 0): { x: number[]; rows: Record<string, number | null>[] } {
    const i = this.lowerBound(fromMs);
    const cols = keys.map((k) => [k, this.cols.get(k)] as const);
    const last = new Map<string, [number, number]>(); // key -> [time, value]
    // prime the held values from just before the window
    for (let r = holdMs > 0 ? this.lowerBound(fromMs - holdMs) : i; r < i; r++)
      for (const [k, c] of cols) {
        const v = c?.[r];
        if (v !== null && v !== undefined) last.set(k, [this.times[r]!, v]);
      }
    const rows: Record<string, number | null>[] = [];
    for (let r = i; r < this.times.length; r++) {
      const t = this.times[r]!;
      const o: Record<string, number | null> = {};
      for (const [k, c] of cols) {
        const v = c ? (c[r] ?? null) : null;
        if (v !== null) last.set(k, [t, v]);
        const h = last.get(k);
        o[k] = v !== null ? v : h && holdMs > 0 && t - h[0] <= holdMs ? h[1] : null;
      }
      rows.push(o);
    }
    return { x: this.times.slice(i), rows };
  }

  subscribe = (l: Listener): (() => void) => {
    this.listeners.add(l);
    return () => this.listeners.delete(l);
  };

  getVersion = (): number => this.version;

  // Coalesce notifications: many upserts in one tick produce one render.
  private changed() {
    this.version++;
    if (this.pending) return;
    this.pending = true;
    queueMicrotask(() => {
      this.pending = false;
      for (const l of this.listeners) l();
    });
  }
}
