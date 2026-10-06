import { useMemo, useState } from "react";
import { useSelector } from "react-redux";
import { CheckboxField, EmptyState, SelectField, Stack, Text, TextField } from "../../../components/common/index.js";
import { selectBackendUp, selectRows } from "../../../redux/telemetry/index.js";
import { TIERS } from "../../../utils/format.js";
import ParameterTable from "../components/ParameterTable.jsx";

const ATTENTION = new Set(["NO_ANSWER", "BLOCK_INCOMPLETE"]);
const TIER_OPTIONS = [{ value: "", label: "All parts (VH/H/M/L)" }, ...TIERS.map((t) => ({ value: t, label: t }))];

/** Every parameter the LUT downlinks, with its newest value, filterable. */
export default function TelemetryPage() {
  const rows = useSelector(selectRows);
  const backendUp = useSelector(selectBackendUp);
  const [q, setQ] = useState("");
  const [subsystem, setSubsystem] = useState("");
  const [tier, setTier] = useState("");
  const [onlyProblems, setOnlyProblems] = useState(false);

  const subsystems = useMemo(() => {
    const names = Array.from(new Set(rows.map((r) => r.subsystem))).sort();
    return [{ value: "", label: "All subsystems" }, ...names.map((s) => ({ value: s, label: s }))];
  }, [rows]);

  // The backend orders parts alphabetically (H, L, M, VH); operators read them
  // fastest-first. The sort is stable, so the LUT order within a part is kept.
  const ordered = useMemo(() => rows.slice().sort((a, b) => TIERS.indexOf(a.tier) - TIERS.indexOf(b.tier)), [rows]);

  const shown = useMemo(() => {
    const needle = q.trim().toLowerCase();
    return ordered.filter((r) => {
      if (subsystem && r.subsystem !== subsystem) return false;
      if (tier && r.tier !== tier) return false;
      if (onlyProblems && !ATTENTION.has(r.quality)) return false;
      if (!needle) return true;
      return (
        r.name.toLowerCase().includes(needle) ||
        r.message.toLowerCase().includes(needle) ||
        r.entry.toLowerCase().includes(needle)
      );
    });
  }, [ordered, q, subsystem, tier, onlyProblems]);

  return (
    <>
      <Stack
        direction="row"
        gap={1}
        align="center"
        wrap
        sx={{ px: 2, py: 1, bgcolor: "background.paper", borderBottom: 1, borderColor: "divider" }}
      >
        <TextField
          type="search"
          placeholder="Search parameter, message or entry…"
          value={q}
          fullWidth={false}
          sx={{ minWidth: 280 }}
          onChange={(e) => setQ(e.target.value)}
        />
        <SelectField value={subsystem} options={subsystems} onChange={(e) => setSubsystem(e.target.value)} />
        <SelectField value={tier} options={TIER_OPTIONS} minWidth={190} onChange={(e) => setTier(e.target.value)} />
        <CheckboxField label="Only needs attention" checked={onlyProblems} onChange={setOnlyProblems} />
        <Text variant="mono" muted sx={{ ml: "auto" }}>
          {shown.length.toLocaleString()} of {rows.length.toLocaleString()} parameters
        </Text>
      </Stack>

      {rows.length === 0 ? (
        <EmptyState>
          {backendUp === false ? (
            "Waiting for the MCC backend. This page loads the parameters as soon as it is reachable."
          ) : (
            <>
              No parameters loaded. Build the decode schema first:
              <br />
              <code>python schema/build_schema.py</code>
            </>
          )}
        </EmptyState>
      ) : shown.length === 0 ? (
        <EmptyState>
          No parameters match these filters.{onlyProblems && " Nothing currently needs attention."}
        </EmptyState>
      ) : (
        <ParameterTable rows={shown} />
      )}
    </>
  );
}
