import { Alert, Snackbar } from "@mui/material";

export const TOAST_DURATION_MS = 6000;

export default function Toast({ open, message, severity = "info", onClose }) {
  return (
    <Snackbar
      open={open}
      autoHideDuration={TOAST_DURATION_MS}
      onClose={(_e, reason) => reason !== "clickaway" && onClose()}
      anchorOrigin={{ vertical: "bottom", horizontal: "center" }}
    >
      <Alert severity={severity} onClose={onClose} variant="filled">
        {message}
      </Alert>
    </Snackbar>
  );
}
