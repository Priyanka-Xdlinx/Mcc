import { TextField as MuiTextField } from "@mui/material";

/** A labelled text input. `inputProps` reach the <input> itself (autoComplete, maxLength...). */
export default function TextField({ size = "small", fullWidth = true, inputProps, ...rest }) {
  return <MuiTextField size={size} fullWidth={fullWidth} slotProps={{ htmlInput: inputProps }} {...rest} />;
}
