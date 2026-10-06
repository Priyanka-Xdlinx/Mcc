import { Checkbox, FormControlLabel } from "@mui/material";

export default function CheckboxField({ label, checked, onChange, ...rest }) {
  return (
    <FormControlLabel
      label={label}
      control={<Checkbox size="small" checked={checked} onChange={(e) => onChange(e.target.checked)} {...rest} />}
      sx={{ "& .MuiFormControlLabel-label": { fontSize: 13 } }}
    />
  );
}
