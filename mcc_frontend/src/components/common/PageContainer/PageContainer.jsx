import { Box } from "@mui/material";

/** The scrolling area a page sits in, below the header. */
export default function PageContainer({ padded = true, maxWidth, children, sx }) {
  return (
    <Box
      sx={[
        {
          flex: 1,
          minHeight: 0,
          overflow: "auto",
          display: "flex",
          flexDirection: "column",
          ...(padded && { p: { xs: 2, md: 3 }, gap: 2 }),
          ...(maxWidth && { width: "100%", maxWidth, mx: "auto" }),
        },
        ...(Array.isArray(sx) ? sx : [sx]),
      ]}
    >
      {children}
    </Box>
  );
}
