import { Dialog as MuiDialog, DialogContent, DialogTitle, IconButton } from "@mui/material";

/** A modal with a title bar and a close button. Esc and a backdrop click close it. */
export default function Dialog({ open = true, title, onClose, maxWidth = "sm", children }) {
  return (
    <MuiDialog open={open} onClose={onClose} maxWidth={maxWidth} fullWidth aria-label={title}>
      <DialogTitle sx={{ display: "flex", alignItems: "center", fontSize: 16, pr: 1 }}>
        <span style={{ flex: 1 }}>{title}</span>
        <IconButton aria-label="Close" size="small" onClick={onClose}>
          ✕
        </IconButton>
      </DialogTitle>
      <DialogContent dividers>{children}</DialogContent>
    </MuiDialog>
  );
}
