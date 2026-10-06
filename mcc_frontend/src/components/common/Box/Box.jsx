import { Box as MuiBox } from "@mui/material";

/**
 * Box with layout props that still work: MUI v9 dropped system props, so
 * `display`, `gap`, `p` and friends are folded into `sx` here.
 */
const LAYOUT_PROPS = new Set([
  "display", "flexDirection", "position", "overflow", "justifyContent", "alignItems",
  "flexWrap", "flex", "gap", "width", "minWidth", "maxWidth", "height", "minHeight",
  "p", "px", "py", "m", "mx", "my", "mt", "mb", "ml", "mr",
]);

export default function Box({ sx, children, ...rest }) {
  const layout = {};
  const props = {};
  for (const [key, value] of Object.entries(rest)) {
    if (LAYOUT_PROPS.has(key)) {
      if (value !== undefined) layout[key] = value;
    } else props[key] = value;
  }
  return (
    <MuiBox sx={[layout, ...(Array.isArray(sx) ? sx : [sx])]} {...props}>
      {children}
    </MuiBox>
  );
}
