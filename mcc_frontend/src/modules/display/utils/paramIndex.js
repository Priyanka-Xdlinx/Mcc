/**
 * Search entries for the parameter picker, sorted by subsystem then id so
 * results come out grouped. `s` is the lower-case text a query matches against.
 */
export function buildIndex(rows) {
  return rows
    .map((row) => ({ row, s: `${row.parameter_id} ${row.name} ${row.subsystem} ${row.message} ${row.entry}`.toLowerCase() }))
    .sort((a, b) => a.row.subsystem.localeCompare(b.row.subsystem) || a.row.parameter_id - b.row.parameter_id);
}

/** Entries matching every whitespace-separated term, at most `max`, plus the total match count. */
export function search(index, q, max) {
  const terms = q.toLowerCase().split(/\s+/).filter(Boolean);
  const hits = [];
  let total = 0;
  for (const e of index) {
    if (terms.every((t) => e.s.includes(t))) {
      total++;
      if (hits.length < max) hits.push(e);
    }
  }
  return { hits, total };
}
