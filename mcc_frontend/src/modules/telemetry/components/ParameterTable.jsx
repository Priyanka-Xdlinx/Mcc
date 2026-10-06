import { memo } from "react";
import { Table, TableBody, TableCell, TableContainer, TableHead, TableRow } from "../../../components/common/index.js";
import { displayValue } from "../../../utils/format.js";

const QUALITY_LABEL = {
  OK: "OK",
  NO_ANSWER: "No answer",
  BLOCK_INCOMPLETE: "Incomplete",
  NOT_DOWNLINKED: "Not sent",
  WARMUP: "Warm-up",
};

const COLUMNS = [
  { label: "Part", width: 64 },
  { label: "Subsystem", width: 96 },
  { label: "Entry", width: 84 },
  { label: "Message", width: 210 },
  { label: "Parameter" },
  { label: "Value", width: 190, align: "right" },
  { label: "Quality", width: 118 },
  { label: "MF", width: 60, align: "right" },
  { label: "SF", width: 52, align: "right" },
];

const mono = { fontFamily: "var(--mono)", fontSize: 12.5 };
const ellipsis = { overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap" };

/** One parameter. Memoised on the row object, which the store replaces only when it changes. */
const ParameterRow = memo(function ParameterRow({ r }) {
  return (
    <TableRow hover>
      <TableCell>
        <span className={`tier ${r.tier}`}>{r.tier}</span>
      </TableCell>
      <TableCell className="dimcol">{r.subsystem}</TableCell>
      <TableCell className="dimcol">{r.entry}</TableCell>
      <TableCell className="dimcol" sx={ellipsis} title={r.message}>
        {r.message}
      </TableCell>
      <TableCell sx={ellipsis} title={r.name}>
        {r.name}
        {/* A quarter of downlinked parameters are not byte aligned; a decoder
            that assumes alignment gets these wrong. */}
        {r.byte_aligned === 0 && (
          <span title="Not byte aligned: decoded bit by bit" style={{ marginLeft: 6, color: "var(--accent)" }}>
            ∿
          </span>
        )}
      </TableCell>
      <TableCell
        align="right"
        sx={[mono, ellipsis, r.value == null && { color: "var(--dimmer)" }]}
        title={r.value ?? undefined}
      >
        {displayValue(r.value)}
      </TableCell>
      <TableCell className={`q ${r.quality ?? "none"}`}>
        <span>{r.quality ? QUALITY_LABEL[r.quality] ?? r.quality : "No data"}</span>
      </TableCell>
      <TableCell align="right" className="dimcol" sx={mono}>
        {r.master_frame ?? "—"}
      </TableCell>
      <TableCell align="right" className="dimcol" sx={mono}>
        {r.subframe ?? "—"}
      </TableCell>
    </TableRow>
  );
});

export default function ParameterTable({ rows }) {
  return (
    <TableContainer>
      <Table>
        <colgroup>
          {COLUMNS.map((c) => (
            <col key={c.label} style={c.width ? { width: c.width } : undefined} />
          ))}
        </colgroup>
        <TableHead>
          <TableRow>
            {COLUMNS.map((c) => (
              <TableCell key={c.label} align={c.align}>
                {c.label}
              </TableCell>
            ))}
          </TableRow>
        </TableHead>
        <TableBody>
          {rows.map((r) => (
            <ParameterRow key={r.parameter_id} r={r} />
          ))}
        </TableBody>
      </Table>
    </TableContainer>
  );
}
