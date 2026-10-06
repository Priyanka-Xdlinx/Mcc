import { MenuItem, TextField } from "@mui/material";

/** A dropdown. `options` is `[{ value, label }]`. */
export default function SelectField({ options, size = "small", minWidth = 160, sx, ...rest }) {
  return (
    <TextField select size={size} sx={[{ minWidth }, ...(Array.isArray(sx) ? sx : [sx])]} {...rest}>
      {options.map((o) => (
        <MenuItem key={o.value} value={o.value}>
          {o.label}
        </MenuItem>
      ))}
    </TextField>
  );
}
