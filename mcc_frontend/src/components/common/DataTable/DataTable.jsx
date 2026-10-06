import {
  Table as MuiTable,
  TableBody,
  TableCell,
  TableContainer as MuiTableContainer,
  TableHead,
  TableRow,
} from "@mui/material";

/**
 * Table parts, compact by default. Long live tables (the parameter list)
 * memoise their own rows; these are only the building blocks.
 */
export function Table({ children, ...rest }) {
  return (
    <MuiTable size="small" stickyHeader sx={{ tableLayout: "fixed" }} {...rest}>
      {children}
    </MuiTable>
  );
}

export function TableContainer({ children, sx }) {
  return <MuiTableContainer sx={[{ flex: 1, overflow: "auto" }, ...(Array.isArray(sx) ? sx : [sx])]}>{children}</MuiTableContainer>;
}

export { TableBody, TableCell, TableHead, TableRow };
