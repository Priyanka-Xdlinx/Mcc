import { IconButton as MuiIconButton, Tooltip } from "@mui/material";

/** A square icon-only button. `label` is both its accessible name and its tooltip. */
export default function IconButton({ label, size = "small", children, ...rest }) {
  return (
    <Tooltip title={label}>
      <span>
        <MuiIconButton aria-label={label} size={size} {...rest}>
          {children}
        </MuiIconButton>
      </span>
    </Tooltip>
  );
}
