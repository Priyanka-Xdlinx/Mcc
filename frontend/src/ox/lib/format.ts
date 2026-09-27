export type Dtype = 'float' | 'int' | 'enum' | 'hex' | 'version' | 'bool';

export interface ChannelFormat {
  dtype: Dtype;
  enumLabels?: Record<string, string> | null;
  hexDigits?: number | null;
}

export const NO_DATA = 'No data';

/** Engineering number with precision by magnitude, as the prototype codec's fmt(). */
export function fmtNumber(x: number | null | undefined): string {
  if (x === null || x === undefined || !Number.isFinite(x)) return '—';
  const a = Math.abs(x);
  return a >= 100 ? x.toFixed(1) : a >= 10 ? x.toFixed(2) : x.toFixed(3);
}

export function fmtHex(v: number, digits: number): string {
  const n = Math.trunc(v);
  if (n < 0) return String(v);
  return '0x' + n.toString(16).toUpperCase().padStart(Math.max(1, digits), '0');
}

/** A 1-byte version: high nibble major, low nibble minor ("V2.3"). */
export function fmtVersion(v: number): string {
  const n = Math.trunc(v);
  return `V${n >> 4}.${n & 15}`;
}

/**
 * The text an operator sees for one sample. Missing values (null/undefined/NaN) are "No data", never 0 (A8).
 */
export function formatValue(ch: ChannelFormat, v: number | null | undefined): string {
  if (v === null || v === undefined || !Number.isFinite(v)) return NO_DATA;
  switch (ch.dtype) {
    case 'enum': {
      const label = ch.enumLabels?.[String(Math.trunc(v))];
      return label ?? `Unknown (${v})`;
    }
    case 'hex':
      return fmtHex(v, ch.hexDigits ?? 2);
    case 'version':
      return fmtVersion(v);
    case 'int':
      return Number.isInteger(v) ? String(v) : fmtNumber(v);
    case 'bool':
      return v ? 'True' : 'False';
    case 'float':
    default:
      return fmtNumber(v);
  }
}

/** True for dtypes whose numeric value is meaningful on an axis. */
export function isPlottable(dtype: Dtype): boolean {
  return dtype === 'float' || dtype === 'int';
}

/** Units such as "-" or "bitmap" add nothing next to a value. */
export function displayUnit(unit: string | null | undefined): string {
  if (!unit || unit === '-' || unit === 'bitmap') return '';
  return unit;
}
