import { useEffect, useMemo } from "react";
import { useSelector } from "react-redux";
import { BrowserRouter } from "react-router";
import { CssBaseline } from "@mui/material";
import { ThemeProvider } from "@mui/material/styles";
import BootGate from "./components/BootGate.jsx";
import ErrorBoundary from "./components/ErrorBoundary.jsx";
import NotificationCenter from "./components/NotificationCenter.jsx";
import useResolvedTheme from "./hooks/useResolvedTheme.js";
import { selectTheme, THEME_KEY } from "./redux/app/index.js";
import AppRoutes from "./routes/AppRoutes.jsx";
import { makeTheme } from "./theme/index.js";
import { writeString } from "./utils/storage.js";

/**
 * The MUI theme and the CSS tokens follow the same choice: `data-theme` on
 * <html> drives the feature stylesheets, the MUI theme drives the kit.
 */
function ThemeRoot({ children }) {
  const choice = useSelector(selectTheme);
  const resolved = useResolvedTheme();
  const theme = useMemo(() => makeTheme(resolved), [resolved]);

  useEffect(() => {
    const root = document.documentElement;
    if (choice === "system") delete root.dataset.theme;
    else root.dataset.theme = choice;
    writeString(THEME_KEY, choice === "system" ? null : choice);
  }, [choice]);

  return (
    <ThemeProvider theme={theme}>
      <CssBaseline />
      {children}
    </ThemeProvider>
  );
}

export default function App() {
  return (
    <ThemeRoot>
      <BrowserRouter>
        <ErrorBoundary>
          <BootGate>
            <AppRoutes />
          </BootGate>
        </ErrorBoundary>
        <NotificationCenter />
      </BrowserRouter>
    </ThemeRoot>
  );
}
