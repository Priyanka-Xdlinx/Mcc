import { Button as MuiButton } from "@mui/material";

/**
 * The app's button. `tone="danger"` is a destructive action; `loading` shows a
 * spinner and blocks repeat clicks.
 */
export default function Button({ variant = "outlined", tone, size = "small", type = "button", children, ...rest }) {
  return (
    <MuiButton variant={variant} color={tone === "danger" ? "error" : "primary"} size={size} type={type} {...rest}>
      {children}
    </MuiButton>
  );
}
