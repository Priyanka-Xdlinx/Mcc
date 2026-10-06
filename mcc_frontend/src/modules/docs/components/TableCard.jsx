import { plural } from "../../../utils/format.js";
import { TABLE_NOTES } from "../config/schemaLayout.js";

/** One table of the live schema: its note, and when open its columns, keys and indexes. */
export default function TableCard({ table: t, open, onToggle }) {
  const fkCol = new Map(t.foreignKeys.map((f) => [f.column, f]));
  return (
    <div className={`tcard ${open ? "open" : ""}`} id={`t-${t.name}`}>
      <button className="tcard-head" onClick={onToggle} aria-expanded={open}>
        <span className="tc-name">{t.name}</span>
        <span className="tc-rows">{plural(t.rows, "row")}</span>
        <span className="tc-caret">{open ? "−" : "+"}</span>
      </button>
      <p className="tc-note">{TABLE_NOTES[t.name]}</p>
      {open && (
        <>
          <div className="doc-table">
            <table>
              <thead>
                <tr>
                  <th>Column</th>
                  <th>Type</th>
                  <th>Key</th>
                  <th>Null</th>
                  <th>Default</th>
                </tr>
              </thead>
              <tbody>
                {t.columns.map((c) => {
                  const fk = fkCol.get(c.name);
                  return (
                    <tr key={c.name}>
                      <td>
                        <code>{c.name}</code>
                      </td>
                      <td className="dimcol">{c.type || "—"}</td>
                      <td>
                        {c.pk > 0 && <span className="keychip pk">PK</span>}
                        {fk && (
                          <span className="keychip fk" title={`references ${fk.references}(${fk.referencesColumn})`}>
                            FK → {fk.references}
                          </span>
                        )}
                      </td>
                      <td className="dimcol">{c.notNull || c.pk > 0 ? "no" : "yes"}</td>
                      <td className="dimcol">{c.default ?? ""}</td>
                    </tr>
                  );
                })}
              </tbody>
            </table>
          </div>
          {t.indexes.length > 0 && (
            <div className="tc-idx">
              <b>Indexes</b>
              {t.indexes.map((ix) => (
                <span key={ix.name} className="idxchip">
                  {ix.origin === "pk" ? "primary key" : ix.unique ? "unique" : "index"} ({ix.columns.join(", ")})
                </span>
              ))}
            </div>
          )}
        </>
      )}
    </div>
  );
}
