import { useEffect, useMemo, useState } from "react";
import telemetryApi from "../../../api/telemetryApi.js";
import { errorMessage } from "../../../api/apiError.js";
import { GROUPS } from "../config/schemaLayout.js";
import SchemaDiagram from "./SchemaDiagram.jsx";
import TableCard from "./TableCard.jsx";

/** The database schema, read live from the running backend so it always matches what runs. */
export default function DatabaseDoc() {
  const [schema, setSchema] = useState(null);
  const [error, setError] = useState(null);
  const [open, setOpen] = useState("parameter");

  useEffect(() => {
    telemetryApi
      .schema()
      .then(setSchema)
      .catch((e) => setError(errorMessage(e, "Could not read the schema.")));
  }, []);

  const byName = useMemo(() => new Map((schema?.tables ?? []).map((t) => [t.name, t])), [schema]);

  return (
    <article className="doc">
      <h1>Database schema</h1>
      <p className="lead">
        Read live from the running database, so it always matches what the backend uses.
        {schema &&
          ` ${schema.engine}, database "${schema.database}" · ${schema.tables.length} tables · ${(schema.sizeBytes / 1048576).toFixed(0)} MB on disk.`}
      </p>
      {error && <div className="doc-error">{error} Is the backend running?</div>}
      {!schema && !error && <div className="doc-loading">Reading the schema…</div>}

      {schema && (
        <>
          <h2>Relationships</h2>
          <SchemaDiagram tables={schema.tables} open={open} onOpen={setOpen} />
          {GROUPS.map((g) => (
            <section key={g.title}>
              <h2>
                {g.title} <span className="h-note">{g.note}</span>
              </h2>
              <div className="tcards">
                {g.tables
                  .map((name) => byName.get(name))
                  .filter(Boolean)
                  .map((t) => (
                    <TableCard key={t.name} table={t} open={open === t.name} onToggle={() => setOpen(open === t.name ? null : t.name)} />
                  ))}
              </div>
            </section>
          ))}
        </>
      )}
    </article>
  );
}
