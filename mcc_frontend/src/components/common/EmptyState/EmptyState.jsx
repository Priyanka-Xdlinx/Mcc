import { Box } from "@mui/material";

/** The message shown where a list, table or view has nothing to show. */
export default function EmptyState({ children, sx }) {
  return (
    <Box
      role="status"
      sx={[
        { p: 6, textAlign: "center", color: "text.secondary", fontSize: 13.5, "& code": { fontFamily: "monospace" } },
        ...(Array.isArray(sx) ? sx : [sx]),
      ]}
    >
      {children}
    </Box>
  );
}
