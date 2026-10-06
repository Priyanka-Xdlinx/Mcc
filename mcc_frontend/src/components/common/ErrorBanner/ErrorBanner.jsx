import { Alert } from "@mui/material";

/** An inline message. `severity`: "error" (default) | "warning" | "info" | "success". */
export default function ErrorBanner({ severity = "error", action, onClose, sx, children }) {
  if (!children) return null;
  return (
    <Alert severity={severity} action={action} onClose={onClose} sx={sx} role={severity === "error" ? "alert" : "status"}>
      {children}
    </Alert>
  );
}
