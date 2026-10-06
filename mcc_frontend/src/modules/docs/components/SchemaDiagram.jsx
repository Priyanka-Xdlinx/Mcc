import { useMemo } from "react";
import { plural } from "../../../utils/format.js";
import { BH, BW, COLUMNS, POS, VIEW_H, VIEW_W } from "../config/schemaLayout.js";

/** The path of one foreign-key line: a loop beside a column, or a curve between columns. */
function fkPath(from, to, i) {
  const [fx, fy] = POS[from];
  const [tx, ty] = POS[to];
  if (fx === tx) {
    const x = fx + BW;
    const off = 16 + (i % 3) * 10;
    return `M${x},${fy + BH / 2} C${x + off},${fy + BH / 2} ${x + off},${ty + BH / 2} ${x + 2},${ty + BH / 2}`;
  }
  const leftward = fx > tx;
  const x1 = leftward ? fx : fx + BW;
  const x2 = leftward ? tx + BW : tx;
  const mid = (x1 + x2) / 2;
  return `M${x1},${fy + BH / 2} C${mid},${fy + BH / 2} ${mid},${ty + BH / 2} ${x2 + (leftward ? 2 : -2)},${ty + BH / 2}`;
}

/** Entity-relationship diagram drawn from the live schema. Click a table to open it below. */
export default function SchemaDiagram({ tables, open, onOpen }) {
  const byName = useMemo(() => new Map(tables.map((t) => [t.name, t])), [tables]);
  const links = useMemo(
    () =>
      tables.flatMap((t) =>
        t.foreignKeys.filter((fk) => POS[t.name] && POS[fk.references]).map((fk) => ({ from: t.name, to: fk.references, col: fk.column })),
      ),
    [tables],
  );

  return (
    <figure className="doc-fig">
      <svg
        viewBox={`0 0 ${VIEW_W} ${VIEW_H}`}
        role="img"
        aria-label="Entity relationship diagram of the MCC database: mission database, downlink map, telemetry, accounts and display tables, with foreign keys drawn as lines."
      >
        <defs>
          <marker id="erarrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto">
            <path d="M0,0 L10,5 L0,10 z" className="d-arrowhead" />
          </marker>
        </defs>
        {COLUMNS.map((c) => (
          <text key={c.title} className="d-label" x={c.x} y="20">
            {c.title.toUpperCase()}
          </text>
        ))}
        {links.map((l, i) => (
          <path key={i} className="d-fk" d={fkPath(l.from, l.to, i)} markerEnd="url(#erarrow)">
            <title>{`${l.from}.${l.col} → ${l.to}`}</title>
          </path>
        ))}
        {Object.entries(POS).map(([name, [x, y]]) => {
          const t = byName.get(name);
          return (
            <g
              key={name}
              className={`d-table ${open === name ? "on" : ""}`}
              onClick={() => onOpen(name)}
              role="button"
              aria-label={`${name}: ${t?.rows ?? 0} rows`}
            >
              <rect x={x} y={y} width={BW} height={BH} rx="7" />
              <text className="d-title" x={x + 12} y={y + 19}>
                {name}
              </text>
              <text className="d-sub" x={x + 12} y={y + 35}>
                {t ? `${plural(t.rows, "row")} · ${plural(t.columns.length, "column")}` : "missing"}
              </text>
            </g>
          );
        })}
      </svg>
      <figcaption>
        Each line is a foreign key, drawn from the live schema; the arrow points to the referenced table. Click a table to
        see its columns below.
      </figcaption>
    </figure>
  );
}
