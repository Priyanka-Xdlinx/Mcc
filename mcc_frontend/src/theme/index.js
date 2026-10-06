import { createTheme } from "@mui/material/styles";

/**
 * The MUI theme, in both palettes. The colours match the CSS tokens in
 * assets/styles/index.css (the Orblinx platform palette), which the feature
 * stylesheets use; the two must change together.
 */
const PALETTES = {
  light: {
    mode: "light",
    primary: { main: "#007892", contrastText: "#ffffff" },
    error: { main: "#b3372a" },
    warning: { main: "#8a5710" },
    success: { main: "#256e43" },
    background: { default: "#edf0f2", paper: "#ffffff" },
    text: { primary: "#13202a", secondary: "#43535f" },
    divider: "#d5dce1",
  },
  dark: {
    mode: "dark",
    primary: { main: "#22b8d8", contrastText: "#06181b" },
    error: { main: "#ef6d5c" },
    warning: { main: "#e2a640" },
    success: { main: "#5dbe86" },
    background: { default: "#0b1115", paper: "#121a20" },
    text: { primary: "#e0e7eb", secondary: "#a8b5be" },
    divider: "#25323b",
  },
};

export const FONT_SANS = '"IBM Plex Sans", -apple-system, "Segoe UI", Roboto, sans-serif';
export const FONT_MONO = '"IBM Plex Mono", ui-monospace, Menlo, Consolas, monospace';

export function makeTheme(mode) {
  return createTheme({
    palette: PALETTES[mode] ?? PALETTES.light,
    shape: { borderRadius: 6 },
    typography: {
      fontFamily: FONT_SANS,
      fontSize: 13.5,
      button: { textTransform: "none", fontWeight: 550 },
    },
    components: {
      MuiButton: {
        defaultProps: { disableElevation: true },
        styleOverrides: { root: { gap: 6 } },
      },
      MuiTableCell: {
        styleOverrides: {
          head: { fontSize: 10, textTransform: "uppercase", letterSpacing: "0.07em", fontWeight: 600 },
        },
      },
    },
  });
}

export default makeTheme;
