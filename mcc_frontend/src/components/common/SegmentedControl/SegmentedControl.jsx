import { ToggleButton, ToggleButtonGroup, Tooltip } from "@mui/material";

/**
 * One choice out of a few, side by side. `options` is `[{ value, label, icon? }]`.
 * `iconOnly` shows just the icons, with the label as tooltip and accessible name.
 */
export default function SegmentedControl({ value, onChange, options, label, size = "small", iconOnly = false }) {
  return (
    <ToggleButtonGroup
      exclusive
      size={size}
      value={value}
      aria-label={label}
      onChange={(_e, v) => v !== null && onChange(v)}
    >
      {options.map((o) => {
        const button = (
          <ToggleButton
            key={o.value}
            value={o.value}
            aria-label={iconOnly ? o.label : undefined}
            sx={{ gap: 0.75, px: iconOnly ? 0.75 : 1.25, py: 0.5, textTransform: "none" }}
          >
            {o.icon}
            {!iconOnly && o.label}
          </ToggleButton>
        );
        return iconOnly ? (
          <Tooltip key={o.value} title={o.label}>
            {button}
          </Tooltip>
        ) : (
          button
        );
      })}
    </ToggleButtonGroup>
  );
}
