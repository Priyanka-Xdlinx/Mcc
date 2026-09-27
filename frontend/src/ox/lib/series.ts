/**
 * Inserts a null row wherever consecutive timestamps are further apart than 2.5 × the expected step, so a
 * stretch with no samples draws as a gap rather than a straight line across it.
 * MCC: parameters are downlinked every 1, 2, 4 or 8 s, so a row without a value for a series is
 * "not sent in this subframe", not an outage. Those become undefined, which the chart joins across;
 * only the inserted gap rows stay null and break the line.
 */
export function withGaps(x: readonly number[], ys: readonly (readonly (number | null)[])[], stepMs: number) {
  if (x.length < 2 || stepMs <= 0) return { x: [...x], ys: ys.map((y) => [...y]) };
  const ox: number[] = [x[0]!];
  const oys: (number | null)[][] = ys.map((y) => [(y[0] ?? undefined) as number | null]);
  for (let i = 1; i < x.length; i++) {
    if (x[i]! - x[i - 1]! > stepMs * 2.5) {
      ox.push(x[i - 1]! + stepMs);
      oys.forEach((y) => y.push(null));
    }
    ox.push(x[i]!);
    oys.forEach((y, j) => y.push((ys[j]![i] ?? undefined) as number | null));
  }
  return { x: ox, ys: oys };
}

/** A − B per sample; missing when either side is missing. */
export function difference(a: readonly (number | null)[], b: readonly (number | null)[]): (number | null)[] {
  return a.map((v, i) => {
    const u = b[i];
    return v === null || u === null || u === undefined ? null : v - u;
  });
}

export function clientStat(
  y: readonly (number | null)[],
): { min: number; max: number; avg: number; count: number } | null {
  let min = Infinity;
  let max = -Infinity;
  let sum = 0;
  let count = 0;
  for (const v of y) {
    if (v === null) continue;
    min = Math.min(min, v);
    max = Math.max(max, v);
    sum += v;
    count++;
  }
  return count ? { min, max, avg: sum / count, count } : null;
}
