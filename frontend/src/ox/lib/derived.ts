// Derived parameters: formulas over the run's channel keys, e.g. "TM024*TM026/1000" or "TC195-TM024".
// Grammar (no eval, no Function): expr := term (('+'|'-') term)* ; term := unary (('*'|'/') unary)* ;
// unary := '-' unary | power ; power := primary ('^' unary)? ;
// primary := NUMBER | 'pi' | FUNC '(' expr (',' expr)* ')' | KEY | '(' expr ')'.
// MCC addition: '^' and the math functions in FUNCS (sqrt, abs, trig, log, min/max, ...).
// KEY is an identifier ([A-Za-z_][A-Za-z0-9_]*) or, when the caller passes the run's keys, any of those
// keys verbatim (so keys such as "PPU.V" or "A-1" work; the longest key wins). Which identifiers are real
// channels is decided by validateExpr against the run, not by the tokenizer: ids come from the MIB.
// Any referenced value that is missing makes the result null (missing stays missing, never 0).

export type Node =
  | { k: 'num'; v: number }
  | { k: 'ref'; id: string }
  | { k: 'neg'; a: Node }
  | { k: 'bin'; op: '+' | '-' | '*' | '/' | '^'; a: Node; b: Node }
  | { k: 'fn'; f: string; args: Node[] };

const R = Math.PI / 180;
/** Math functions a formula may call: [min args, max args, implementation]. Angles are in degrees. */
export const FUNCS: Record<string, [number, number, (...a: number[]) => number, string]> = {
  abs: [1, 1, Math.abs, 'absolute value'],
  sqrt: [1, 1, Math.sqrt, 'square root'],
  cbrt: [1, 1, Math.cbrt, 'cube root'],
  pow: [2, 2, Math.pow, 'pow(a, b) = a^b'],
  exp: [1, 1, Math.exp, 'e^x'],
  ln: [1, 1, Math.log, 'natural log'],
  log10: [1, 1, Math.log10, 'log base 10'],
  log2: [1, 1, Math.log2, 'log base 2'],
  sin: [1, 1, (d) => Math.sin(d * R), 'sine, degrees'],
  cos: [1, 1, (d) => Math.cos(d * R), 'cosine, degrees'],
  tan: [1, 1, (d) => Math.tan(d * R), 'tangent, degrees'],
  asin: [1, 1, (x) => Math.asin(x) / R, 'arcsine → degrees'],
  acos: [1, 1, (x) => Math.acos(x) / R, 'arccosine → degrees'],
  atan: [1, 1, (x) => Math.atan(x) / R, 'arctangent → degrees'],
  atan2: [2, 2, (y, x) => Math.atan2(y, x) / R, 'atan2(y, x) → degrees'],
  hypot: [2, 4, Math.hypot, 'vector length √(a²+b²+…)'],
  deg: [1, 1, (r) => r / R, 'radians → degrees'],
  rad: [1, 1, (d) => d * R, 'degrees → radians'],
  min: [2, 8, Math.min, 'smallest'],
  max: [2, 8, Math.max, 'largest'],
  avg: [2, 8, (...a) => a.reduce((s, x) => s + x, 0) / a.length, 'mean'],
  round: [1, 2, (x, n = 0) => Math.round(x * 10 ** n) / 10 ** n, 'round(x, digits)'],
  floor: [1, 1, Math.floor, 'round down'],
  ceil: [1, 1, Math.ceil, 'round up'],
  sign: [1, 1, Math.sign, '−1, 0 or 1'],
  mod: [2, 2, (a, b) => a % b, 'remainder a mod b'],
  clamp: [3, 3, (x, lo, hi) => Math.min(hi, Math.max(lo, x)), 'clamp(x, lo, hi)'],
};

type Tok = { t: 'num'; v: number } | { t: 'id'; v: string } | { t: 'op'; v: string };

/** Derived parameter ids: "D" and a number. Channel keys are resolved before derived ids everywhere. */
export const DERIVED_ID = /^D\d{1,4}$/;
export const MAX_EXPR_LENGTH = 200;
const IDENT = /^[A-Za-z_][A-Za-z0-9_]*/;

export class ExprError extends Error {}

/** Keys the plain identifier rule would split (they contain other characters), longest first. */
function specialKeys(keys: Iterable<string> | undefined): string[] {
  if (!keys) return [];
  return [...keys]
    .filter((k) => k && !new RegExp(`${IDENT.source}$`).test(k))
    .sort((a, b) => b.length - a.length);
}

function tokenize(src: string, keys?: Iterable<string>): Tok[] {
  const out: Tok[] = [];
  const special = specialKeys(keys);
  let i = 0;
  while (i < src.length) {
    const key = special.find((k) => src.startsWith(k, i));
    if (key) {
      out.push({ t: 'id', v: key });
      i += key.length;
      continue;
    }
    const c = src[i]!;
    if (c === ' ' || c === '\t') {
      i++;
      continue;
    }
    if ('+-*/()^,'.includes(c)) {
      out.push({ t: 'op', v: c });
      i++;
      continue;
    }
    const num = /^(\d+(\.\d*)?|\.\d+)([eE][+-]?\d+)?/.exec(src.slice(i));
    if (num) {
      out.push({ t: 'num', v: Number(num[0]) });
      i += num[0].length;
      continue;
    }
    const id = IDENT.exec(src.slice(i));
    if (id) {
      out.push({ t: 'id', v: id[0] });
      i += id[0].length;
      continue;
    }
    throw new ExprError('Use only parameter ids, numbers, + − × ÷ ^ ( ) and functions such as sqrt(…).');
  }
  return out;
}

/** Parses a formula. `keys` (the run's channel keys) lets keys with non-identifier characters through. */
export function parseExpr(src: string, keys?: Iterable<string>): Node {
  if (src.length > MAX_EXPR_LENGTH)
    throw new ExprError(`The formula is longer than ${MAX_EXPR_LENGTH} characters.`);
  const toks = tokenize(src, keys);
  if (!toks.length) throw new ExprError('Enter a formula.');
  let p = 0;
  const peek = () => toks[p];
  const isOp = (v: string) => {
    const t = toks[p];
    return !!t && t.t === 'op' && t.v === v;
  };
  const expr = (): Node => {
    let a = term();
    while (isOp('+') || isOp('-')) {
      const op = (toks[p++] as { v: '+' | '-' }).v;
      a = { k: 'bin', op, a, b: term() };
    }
    return a;
  };
  const term = (): Node => {
    let a = unary();
    while (isOp('*') || isOp('/')) {
      const op = (toks[p++] as { v: '*' | '/' }).v;
      a = { k: 'bin', op, a, b: unary() };
    }
    return a;
  };
  const unary = (): Node => {
    if (isOp('-')) {
      p++;
      return { k: 'neg', a: unary() };
    }
    if (isOp('+')) {
      p++;
      return unary();
    }
    return power();
  };
  const power = (): Node => {
    const a = primary();
    if (!isOp('^')) return a;
    p++;
    return { k: 'bin', op: '^', a, b: unary() };
  };
  const primary = (): Node => {
    const t = peek();
    if (!t) throw new ExprError('The formula ends too early.');
    p++;
    if (t.t === 'num') return { k: 'num', v: t.v };
    if (t.t === 'id') {
      if (isOp('(')) {
        const f = t.v.toLowerCase();
        const spec = FUNCS[f];
        if (!spec) throw new ExprError(`Unknown function "${t.v}". Try sqrt, abs, pow, sin, cos, atan2, hypot, ln, min, max, round.`);
        p++;
        const args: Node[] = [expr()];
        while (isOp(',')) {
          p++;
          args.push(expr());
        }
        if (!isOp(')')) throw new ExprError(`A closing bracket is missing after ${f}(…).`);
        p++;
        if (args.length < spec[0] || args.length > spec[1])
          throw new ExprError(`${f} takes ${spec[0] === spec[1] ? spec[0] : `${spec[0]} to ${spec[1]}`} value${spec[1] > 1 ? 's' : ''}.`);
        return { k: 'fn', f, args };
      }
      if (t.v.toLowerCase() === 'pi') return { k: 'num', v: Math.PI };
      return { k: 'ref', id: t.v };
    }
    if (t.v === '(') {
      const e = expr();
      if (!isOp(')')) throw new ExprError('A closing bracket is missing.');
      p++;
      return e;
    }
    throw new ExprError(`Unexpected "${t.v}".`);
  };
  const root = expr();
  if (p !== toks.length) {
    const t = toks[p]!;
    throw new ExprError(`Unexpected "${t.t === 'num' ? t.v : t.v}".`);
  }
  return root;
}

export function refsOf(n: Node, acc: Set<string> = new Set()): Set<string> {
  if (n.k === 'ref') acc.add(n.id);
  else if (n.k === 'neg') refsOf(n.a, acc);
  else if (n.k === 'bin') {
    refsOf(n.a, acc);
    refsOf(n.b, acc);
  } else if (n.k === 'fn') n.args.forEach((a) => refsOf(a, acc));
  return acc;
}

export type Values = Readonly<Record<string, number | null | undefined>>;

export function evaluate(n: Node, v: Values): number | null {
  switch (n.k) {
    case 'num':
      return n.v;
    case 'ref': {
      // Own properties only: an id such as "constructor" must not reach Object.prototype.
      const x = Object.prototype.hasOwnProperty.call(v, n.id) ? v[n.id] : undefined;
      return x === null || x === undefined || !Number.isFinite(x) ? null : x;
    }
    case 'neg': {
      const a = evaluate(n.a, v);
      return a === null ? null : -a;
    }
    case 'bin': {
      const a = evaluate(n.a, v);
      if (a === null) return null;
      const b = evaluate(n.b, v);
      if (b === null) return null;
      const r = n.op === '+' ? a + b : n.op === '-' ? a - b : n.op === '*' ? a * b : n.op === '^' ? a ** b : a / b;
      return Number.isFinite(r) ? r : null;
    }
    case 'fn': {
      const xs: number[] = [];
      for (const a of n.args) {
        const x = evaluate(a, v);
        if (x === null) return null;
        xs.push(x);
      }
      const r = FUNCS[n.f]![2](...xs);
      return Number.isFinite(r) ? r : null; // sqrt(-1), ln(0): missing, never NaN or infinity
    }
  }
}

export interface Compiled {
  refs: string[];
  eval: (v: Values) => number | null;
}

export function compile(src: string, keys?: Iterable<string>): Compiled {
  const ast = parseExpr(src, keys);
  return { refs: [...refsOf(ast)], eval: (v) => evaluate(ast, v) };
}

/** True when the formula divides by the literal 0 (e.g. "TM024/0", "TM024/(0)"). */
function dividesByZeroLiteral(n: Node): boolean {
  if (n.k === 'neg') return dividesByZeroLiteral(n.a);
  if (n.k === 'fn') return n.args.some(dividesByZeroLiteral);
  if (n.k !== 'bin') return false;
  if (n.op === '/' && evaluate(n.b, {}) === 0 && refsOf(n.b).size === 0) return true;
  return dividesByZeroLiteral(n.a) || dividesByZeroLiteral(n.b);
}

/**
 * '' when the formula is usable; otherwise a message for the operator.
 * `known` is the set of channel keys in the run; ids outside it are rejected. `notNumeric` holds keys whose
 * values are states or codes (enum, hex, version): arithmetic on them is meaningless, so they are rejected.
 */
export function validateExpr(
  src: string,
  known: ReadonlySet<string>,
  notNumeric: ReadonlySet<string> = new Set(),
): string {
  let ast: Node;
  try {
    ast = parseExpr(src, known);
  } catch (e) {
    return e instanceof ExprError ? e.message : 'The formula does not parse.';
  }
  const refs = [...refsOf(ast)];
  if (!refs.length) return 'Reference at least one channel id.';
  const bad = refs.filter((r) => !known.has(r));
  if (bad.length)
    return `${bad.join(', ')} ${bad.length > 1 ? 'are not channel ids' : 'is not a channel id'} in this run.`;
  const states = refs.filter((r) => notNumeric.has(r));
  if (states.length)
    return `${states.join(', ')} ${states.length > 1 ? 'are states or codes' : 'is a state or code'} (enum, hex or version), not a number; it cannot be used in a calculation.`;
  if (dividesByZeroLiteral(ast)) return 'The formula divides by 0.';
  return '';
}

// ---------- two-parameter builder: A op B, optionally scaled

export const BINARY_OPS = [
  { op: '+', label: 'A + B' },
  { op: '-', label: 'A − B' },
  { op: '*', label: 'A × B' },
  { op: '/', label: 'A ÷ B' },
] as const;
export type BinaryOp = (typeof BINARY_OPS)[number]['op'];

export interface BinaryInput {
  a: string;
  b: string;
  op: BinaryOp;
  /** Multiplies the result, e.g. 0.001 to turn V × mA into W. */
  factor: string;
  unitA: string;
  unitB: string;
}

/** The formula for A op B × factor, e.g. "(TM024*TM026)*0.001". */
export function binaryExpr(i: Pick<BinaryInput, 'a' | 'b' | 'op' | 'factor'>): string {
  const core = `${i.a}${i.op}${i.b}`;
  const f = Number(i.factor);
  return i.factor.trim() === '' || f === 1 ? core : `(${core})*${i.factor.trim()}`;
}

const PRODUCT_UNITS: Record<string, string> = { 'V·A': 'W', 'A·V': 'W', 'V·mA': 'mW', 'mA·V': 'mW' };

/** Unit of the result, when it can be told from the inputs; '' otherwise. */
export function binaryUnit(i: Pick<BinaryInput, 'op' | 'factor' | 'unitA' | 'unitB'>): string {
  const [ua, ub] = [i.unitA.trim(), i.unitB.trim()];
  const f = Number(i.factor || '1');
  let u = '';
  if (i.op === '+' || i.op === '-') u = ua === ub ? ua : '';
  else if (i.op === '*') u = PRODUCT_UNITS[`${ua}·${ub}`] ?? (ua && ub ? `${ua}·${ub}` : ua || ub);
  else u = ua && ub ? (ua === ub ? '' : `${ua}/${ub}`) : ua;
  if (f === 1) return u;
  if (u === 'mW' && f === 0.001) return 'W';
  if (u === 'W' && f === 1000) return 'mW';
  return u;
}

/** Errors (block) and notes (inform) for the two-parameter builder. */
export function validateBinary(i: BinaryInput): { error: string; notes: string[] } {
  const notes: string[] = [];
  if (!i.a || !i.b) return { error: 'Pick parameter A and parameter B.', notes };
  if (i.factor.trim() !== '' && !Number.isFinite(Number(i.factor)))
    return { error: 'The factor must be a number, e.g. 0.001.', notes };
  if (i.factor.trim() !== '' && Number(i.factor) === 0)
    return { error: 'A factor of 0 always gives 0.', notes };
  const [ua, ub] = [i.unitA.trim(), i.unitB.trim()];
  if ((i.op === '+' || i.op === '-') && ua && ub && ua !== ub)
    return {
      error: `A is in ${ua} and B is in ${ub}. Adding or subtracting needs the same unit; use × or ÷, or pick parameters with the same unit.`,
      notes,
    };
  if (i.op === '/') notes.push('Where B is 0 the result is missing (no value), never infinity.');
  if (i.a === i.b && (i.op === '-' || i.op === '/'))
    notes.push('A and B are the same parameter: the result is constant.');
  notes.push('If A or B is missing for a sample, the result is missing too.');
  return { error: '', notes };
}

export interface DerivedDef {
  id: string; // "D1", "D2"…
  name: string;
  expr: string;
  unit: string;
}

/**
 * The next free "D<n>" id after the highest one in use. `taken` holds channel keys: a channel called "D7"
 * keeps its key, so the derived parameter skips to the next number instead of shadowing it.
 */
export function nextDerivedId(defs: readonly DerivedDef[], taken: ReadonlySet<string> = new Set()): string {
  let n = defs.reduce((m, d) => Math.max(m, Number(d.id.slice(1)) || 0), 0) + 1;
  while (taken.has(`D${n}`)) n++;
  return `D${n}`;
}

export type ResolvedId<C> =
  { kind: 'channel'; channel: C } | { kind: 'derived'; def: DerivedDef } | { kind: 'missing' };

/** What a widget id refers to. Channels win: a channel key that looks like "D1" is still that channel. */
export function resolveId<C>(
  id: string,
  channels: ReadonlyMap<string, C>,
  derived: readonly DerivedDef[],
): ResolvedId<C> {
  const c = channels.get(id);
  if (c !== undefined) return { kind: 'channel', channel: c };
  const d = DERIVED_ID.test(id) ? derived.find((x) => x.id === id) : undefined;
  return d ? { kind: 'derived', def: d } : { kind: 'missing' };
}
