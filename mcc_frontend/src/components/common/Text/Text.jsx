import { Typography } from "@mui/material";

const VARIANTS = {
  body: { fontSize: 13.5 },
  small: { fontSize: 12 },
  caption: { fontSize: 12, color: "text.secondary" },
  label: { fontSize: 10, textTransform: "uppercase", letterSpacing: "0.07em", color: "text.secondary" },
  mono: { fontSize: 12.5, fontFamily: '"IBM Plex Mono", monospace' },
  title: { fontSize: 16, fontWeight: 600 },
  heading: { fontSize: 22, fontWeight: 600, letterSpacing: "-0.2px" },
};

/** Text in the app's type scale, so a page never reaches for Typography itself. */
export default function Text({ variant = "body", component = "p", muted = false, truncate = false, sx, children, ...rest }) {
  return (
    <Typography
      component={component}
      sx={[
        { m: 0 },
        VARIANTS[variant] ?? VARIANTS.body,
        muted && { color: "text.secondary" },
        truncate && { overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap" },
        ...(Array.isArray(sx) ? sx : [sx]),
      ]}
      {...rest}
    >
      {children}
    </Typography>
  );
}
