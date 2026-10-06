import { CircularProgress } from "@mui/material";

export default function Spinner({ size = 16, label }) {
  return <CircularProgress size={size} color="inherit" aria-label={label} />;
}
