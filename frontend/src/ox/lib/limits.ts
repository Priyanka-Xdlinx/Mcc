// Can a set of series share one chart axis? Each parameter's expected range (parameter_limit) gives its size;
// a bar or plot that puts sizes this far apart on one axis hides the small ones, so it is not formed.

/** Largest size ratio, on one axis, that still shows every series (100× makes the small one ~1% of the big one). */
export const MAX_SCALE_RATIO = 100;

export interface Limit {
  lo: number;
  hi: number;
}

export interface AxisSeries {
  name: string;
  unit: string;
  /** Null when the parameter has no limit row: nothing can be said about it. */
  limit: Limit | null;
}

/** Series with the same unit share an axis; a series with no unit gets its own. */
function axisKey(s: AxisSeries, i: number): string {
  return s.unit ? `unit:${s.unit}` : `own:${i}`;
}

/**
 * Why these series cannot be charted together, or null when they can. Only series on a shared axis are
 * compared, and only when every one of them has a limit and a non-zero size; otherwise the answer is null.
 */
export function sharedAxisProblem(series: readonly AxisSeries[]): string | null {
  if (series.length < 2) return null;
  const groups = new Map<string, AxisSeries[]>();
  series.forEach((s, i) => {
    const k = axisKey(s, i);
    groups.set(k, [...(groups.get(k) ?? []), s]);
  });
  for (const members of groups.values()) {
    if (members.length < 2) continue;
    if (members.some((m) => m.limit === null)) continue;
    const sized = members.map((m) => ({ s: m, size: Math.max(Math.abs(m.limit!.lo), Math.abs(m.limit!.hi)) }));
    if (sized.some((x) => x.size === 0)) continue;
    const big = sized.reduce((a, b) => (b.size > a.size ? b : a));
    const small = sized.reduce((a, b) => (b.size < a.size ? b : a));
    const ratio = big.size / small.size;
    if (ratio > MAX_SCALE_RATIO) {
      const unit = big.s.unit ? ` ${big.s.unit}` : '';
      return `${big.s.name} can reach ${big.size.toPrecision(3)}${unit} and ${small.s.name} only ${small.size.toPrecision(3)}${unit}, a ratio of ${Math.round(ratio)}×. On one axis the smaller one would be invisible.`;
    }
  }
  return null;
}
