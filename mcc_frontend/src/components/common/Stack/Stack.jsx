import { Stack as MuiStack } from "@mui/material";

/** A flex row or column. `gap` is in theme spacing units (8px). */
export default function Stack({ direction = "column", gap = 1, align, justify, wrap = false, sx, children, ...rest }) {
  return (
    <MuiStack
      direction={direction}
      sx={[
        {
          gap,
          ...(align && { alignItems: align }),
          ...(justify && { justifyContent: justify }),
          ...(wrap && { flexWrap: "wrap" }),
        },
        ...(Array.isArray(sx) ? sx : [sx]),
      ]}
      {...rest}
    >
      {children}
    </MuiStack>
  );
}
