import { Icon } from '../../components/Icon';
import { useMemo, useState, type FormEvent } from 'react';
import type { Channel } from '../types';
import { Section } from '../components/Section';
import {
  BINARY_OPS,
  binaryExpr,
  binaryUnit,
  nextDerivedId,
  validateBinary,
  validateExpr,
  FUNCS,
  type BinaryOp,
  type DerivedDef,
} from '../lib/derived';
import { displayUnit, isPlottable } from '../lib/format';

function BinaryBuilder({
  derived,
  known,
  onAdd,
}: {
  derived: DerivedDef[];
  known: Map<string, Channel>;
  onAdd: (d: Omit<DerivedDef, 'id'>) => void;
}) {
  const numeric = useMemo(
    () => [...known.values()].filter((c) => isPlottable(c.dtype) && !c.hidden),
    [known],
  );
  const [a, setA] = useState('');
  const [b, setB] = useState('');
  const [op, setOp] = useState<BinaryOp>('*');
  const [factor, setFactor] = useState('');
  const [name, setName] = useState('');
  const [unit, setUnit] = useState<string | null>(null); // null = use the suggestion
  const [touched, setTouched] = useState(false);
  const unitA = displayUnit(known.get(a)?.unit);
  const unitB = displayUnit(known.get(b)?.unit);
  const input = { a, b, op, factor, unitA, unitB };
  const { error, notes } = validateBinary(input);
  const expr = a && b ? binaryExpr(input) : '';
  const suggested = a && b ? binaryUnit(input) : '';
  const finalUnit = unit ?? suggested;
  const opLabel = BINARY_OPS.find((o) => o.op === op)!.label;
  const autoName =
    a && b ? `${known.get(a)?.name ?? a} ${opLabel.slice(2, 3)} ${known.get(b)?.name ?? b}` : '';
  const finalName = (name || autoName).trim();
  const nameErr = !finalName
    ? 'Give the result a name.'
    : derived.some((d) => d.name.toLowerCase() === finalName.toLowerCase())
      ? 'A derived parameter with this name exists.'
      : '';
  const err = error || nameErr;

  const submit = (e: FormEvent) => {
    e.preventDefault();
    setTouched(true);
    if (err) return;
    onAdd({ name: finalName.slice(0, 80), expr, unit: finalUnit.slice(0, 16) });
    setA('');
    setB('');
    setFactor('');
    setName('');
    setUnit(null);
    setTouched(false);
  };
  const pick = (id: string, value: string, set: (v: string) => void, label: string) => (
    <div className="fld">
      <label htmlFor={id}>{label}</label>
      <select id={id} value={value} onChange={(e) => set(e.target.value)}>
        <option value="">Pick…</option>
        {numeric.map((c) => (
          <option key={c.key} value={c.key}>
            {c.key} · {c.name.slice(0, 32)}
            {displayUnit(c.unit) ? ` (${displayUnit(c.unit)})` : ''}
          </option>
        ))}
      </select>
    </div>
  );

  return (
    <form className="stack" onSubmit={submit} noValidate aria-label="Calculate from two parameters">
      <h3 className="small" style={{ margin: 0 }}>
        Calculate from two parameters
      </h3>
      <div className="controls" style={{ margin: 0 }}>
        {pick('bb-a', a, setA, 'Parameter A')}
        <div className="fld">
          <label htmlFor="bb-op">Operation</label>
          <select id="bb-op" value={op} onChange={(e) => setOp(e.target.value as BinaryOp)}>
            {BINARY_OPS.map((o) => (
              <option key={o.op} value={o.op}>
                {o.label}
              </option>
            ))}
          </select>
        </div>
        {pick('bb-b', b, setB, 'Parameter B')}
        <div className="fld">
          <label htmlFor="bb-f">× factor (optional)</label>
          <input
            id="bb-f"
            className="mono"
            size={7}
            inputMode="decimal"
            placeholder="1"
            value={factor}
            onChange={(e) => setFactor(e.target.value)}
          />
        </div>
        <div className="fld">
          <label htmlFor="bb-n">Name</label>
          <input
            id="bb-n"
            size={18}
            placeholder={autoName || 'Result name'}
            value={name}
            onChange={(e) => setName(e.target.value)}
          />
        </div>
        <div className="fld">
          <label htmlFor="bb-u">Unit</label>
          <input id="bb-u" size={6} value={finalUnit} onChange={(e) => setUnit(e.target.value)} />
        </div>
        <button className="btn primary" type="submit">
          <Icon name="calc" size={15} />
          Add result
        </button>
      </div>
      <div className="small" role="status" aria-live="polite">
        {expr && (
          <span>
            Formula <code>{expr}</code>
            {finalUnit ? ` → ${finalUnit}` : ''}.{' '}
          </span>
        )}
        {(touched || (a && b)) && err ? (
          <span className="err" role="alert">
            {err}
          </span>
        ) : (
          a && b && <span className="muted">{notes.join(' ')}</span>
        )}
      </div>
    </form>
  );
}

export function DerivedEditor({
  derived,
  known,
  onChange,
}: {
  derived: DerivedDef[];
  known: Map<string, Channel>;
  onChange: (d: DerivedDef[]) => void;
}) {
  const [name, setName] = useState('');
  const [expr, setExpr] = useState('');
  const [unit, setUnit] = useState('');
  const [touched, setTouched] = useState(false);
  const keys = new Set(known.keys());
  const notNumeric = new Set([...known.values()].filter((c) => !isPlottable(c.dtype)).map((c) => c.key));
  const exprErr = expr ? validateExpr(expr, keys, notNumeric) : 'Enter a formula.';
  const nameErr = name.trim() ? '' : 'Give the parameter a name.';
  const err = exprErr || nameErr;
  const addDef = (d: Omit<DerivedDef, 'id'>) =>
    onChange([...derived, { id: nextDerivedId(derived, keys), ...d }]);
  // An example from this run's own numeric channels (ids come from the MIB, never hardcoded).
  const numeric = [...known.values()].filter((c) => isPlottable(c.dtype) && !c.hidden);
  const [ea, eb] = [...numeric.filter((c) => c.dtype === 'float'), ...numeric].map((c) => c.key);
  const example = ea && eb ? `${ea}*${eb}` : ea ? `${ea}*2` : 'A*B';

  const add = (e: FormEvent) => {
    e.preventDefault();
    setTouched(true);
    if (err) return;
    addDef({ name: name.trim(), expr: expr.trim(), unit: unit.trim() });
    setName('');
    setExpr('');
    setUnit('');
    setTouched(false);
  };

  return (
    <Section
      title="Derived parameters"
      persistId="tm.derived"
      count={`calculations between parameters, e.g. ${example.replace('*', ' × ')}`}
    >
      <div className="secbody stack">
        <div className="tblwrap">
          <table>
            <thead>
              <tr>
                <th>ID</th>
                <th>Name</th>
                <th>Formula</th>
                <th>Unit</th>
                <th>
                  <span className="sr-only">Actions</span>
                </th>
              </tr>
            </thead>
            <tbody>
              {derived.length === 0 && (
                <tr>
                  <td colSpan={5} className="muted">
                    No derived parameters.
                  </td>
                </tr>
              )}
              {derived.map((d) => {
                const bad = validateExpr(d.expr, keys, notNumeric);
                return (
                  <tr key={d.id}>
                    <td className="num">{d.id}</td>
                    <td>{d.name}</td>
                    <td className="num">
                      {d.expr} {bad && <span className="chip crit wrap">{bad}</span>}
                    </td>
                    <td>{d.unit}</td>
                    <td className="actions">
                      <button
                        type="button"
                        className="btn sm"
                        onClick={() => onChange(derived.filter((x) => x.id !== d.id))}
                        aria-label={`Remove ${d.name}`}
                      >
                        <Icon name="trash" size={14} />
                        Remove
                      </button>
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
        <BinaryBuilder derived={derived} known={known} onAdd={addDef} />
        <form className="stack" onSubmit={add} noValidate aria-label="Free formula">
          <h3 className="small" style={{ margin: 0 }}>
            Or write a formula
          </h3>
          <div className="controls" style={{ margin: 0 }}>
            <div className="fld">
              <label htmlFor="d-name">Name</label>
              <input id="d-name" value={name} size={18} onChange={(e) => setName(e.target.value)} />
            </div>
            <div className="fld">
              <label htmlFor="d-expr">Formula</label>
              <input
                id="d-expr"
                className="mono"
                value={expr}
                size={24}
                placeholder={`${example}/1000`}
                aria-invalid={touched && !!exprErr}
                aria-describedby="d-err"
                onChange={(e) => setExpr(e.target.value)}
              />
            </div>
            <div className="fld">
              <label htmlFor="d-unit">Unit</label>
              <input id="d-unit" value={unit} size={5} onChange={(e) => setUnit(e.target.value)} />
            </div>
            <button className="btn primary" type="submit">
              <Icon name="func" size={15} />
              Add derived parameter
            </button>
            <span className="err" id="d-err" role="alert">
              {(touched || expr) && err ? err : ''}
            </span>
          </div>
        </form>
        <p className="muted small" style={{ margin: 0 }}>
          Use parameter ids (P + number, as in the add menus), numbers, + − × ÷ ^ ( ) and the functions below, e.g.{' '}
          <code>hypot(P2195, P2196)</code>, <code>atan2(P537, P536)</code>, <code>round(P1988*P1989, 2)</code>. States and codes (enum, hex, version)
          are rejected. If any input is missing for a sample, or a division hits 0, the result is missing
          (never 0 or infinity). Add the result to a section with its “Add widget” menu, or as an extra line
          in any plot with “+ parameter”.
        </p>
        <div className="fnpal" role="group" aria-label="Math functions: click to insert into the formula">
          {Object.entries(FUNCS).map(([f, spec]) => (
            <button key={f} type="button" className="fnchip" title={spec[3]} onClick={() => setExpr((e) => `${e}${f}(`)}>
              {f}
            </button>
          ))}
          {['^', 'pi'].map((f) => (
            <button key={f} type="button" className="fnchip" title={f === '^' ? 'power: a^b' : 'π = 3.14159…'} onClick={() => setExpr((e) => `${e}${f}`)}>
              {f}
            </button>
          ))}
        </div>
      </div>
    </Section>
  );
}
