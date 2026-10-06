/**
 * Can a plot's series share one y axis? Each parameter's expected range (parameter_limit) gives its size:
 * the larger of |lo| and |hi|. On one axis, sizes this far apart hide the small series, so the plot is not formed.
 */

/** Largest size ratio, on one axis, that still shows every series (100× makes the small one ~1% of the big one). */
export const MAX_SCALE_RATIO = 100;

/**
 * Why these series cannot share a plot, or null when they can. Series on one axis (same unit, as Chart.jsx
 * groups them) are compared, and only when every one has a limit and a non-zero size. Otherwise: null.
 *
 * @param {{ name: string, unit?: string, limit: { lo: number, hi: number } | null }[]} series
 * @returns {string | null}
 */
export function sharedAxisProblem(series) {
  if (series.length < 2) return null;
  const groups = new Map();
  for (const s of series) {
    const key = s.unit ?? "";
    groups.set(key, [...(groups.get(key) ?? []), s]);
  }
  for (const members of groups.values()) {
    if (members.length < 2) continue;
    if (members.some((m) => !m.limit)) continue;
    const sized = members.map((s) => ({ s, size: Math.max(Math.abs(s.limit.lo), Math.abs(s.limit.hi)) }));
    if (sized.some((x) => x.size === 0)) continue;
    const big = sized.reduce((a, b) => (b.size > a.size ? b : a));
    const small = sized.reduce((a, b) => (b.size < a.size ? b : a));
    const ratio = big.size / small.size;
    if (ratio > MAX_SCALE_RATIO) {
      const unit = big.s.unit ? ` ${big.s.unit}` : "";
      return `${big.s.name} can reach ${big.size.toPrecision(3)}${unit} and ${small.s.name} only ${small.size.toPrecision(3)}${unit}, a ratio of ${Math.round(ratio)}×. On one axis the smaller one would be invisible.`;
    }
  }
  return null;
}
