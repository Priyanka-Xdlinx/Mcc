import { useEffect } from "react";
import { useSelector } from "react-redux";
import { Outlet, useLocation } from "react-router";
import AppHeader from "../components/AppHeader.jsx";
import ErrorBoundary from "../components/ErrorBoundary.jsx";
import StatusBar from "../components/StatusBar.jsx";
import { Box, ErrorBanner } from "../components/common/index.js";
import { pageOf } from "../config/navigation.js";
import useTelemetryConnection from "../hooks/useTelemetryConnection.js";
import { selectBackendUp } from "../redux/telemetry/index.js";

/** Pages that show the feed facts strip under the header. */
const WITH_STATUS = new Set(["/telemetry", "/charts", "/decode"]);

/**
 * The frame every signed-in page sits in: header, a banner when the backend is
 * down, then the page. Owns the telemetry connection, so it is opened once and
 * shared by every page.
 */
export default function AppLayout() {
  useTelemetryConnection();
  const { pathname } = useLocation();
  const backendUp = useSelector(selectBackendUp);
  const page = pageOf(pathname);

  useEffect(() => {
    document.title = `${page?.label ?? "MCC"} · OrbLinx MCC`;
  }, [page]);

  return (
    <Box display="flex" flexDirection="column" height="100svh" sx={{ bgcolor: "background.default" }}>
      {/* Always shown: focus mode on Display hides that page's toolbar, never the header. */}
      <AppHeader />
      {backendUp === false && (
        <ErrorBanner severity="warning" sx={{ borderRadius: 0 }}>
          <b>The MCC backend is not reachable.</b> Start it with <code>python mcc.py start</code>; this page
          reconnects by itself.
        </ErrorBanner>
      )}
      {WITH_STATUS.has(page?.path) && <StatusBar />}
      <Box component="main" flex={1} minHeight={0} display="flex" flexDirection="column">
        <ErrorBoundary key={page?.path}>
          <Outlet />
        </ErrorBoundary>
      </Box>
    </Box>
  );
}
