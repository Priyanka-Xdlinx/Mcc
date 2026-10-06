import { Chip } from "@mui/material";

const COLORS = { ok: "success", warn: "warning", bad: "error", info: "info", none: "default" };

/** A small labelled status. `tone`: "ok" | "warn" | "bad" | "info" | "none". */
export default function StatusChip({ tone = "none", label, icon, title }) {
  return (
    <Chip
      size="small"
      variant="outlined"
      color={COLORS[tone] ?? "default"}
      label={label}
      icon={icon}
      title={title}
      sx={{ fontWeight: 550 }}
    />
  );
}
