import { lazy, Suspense } from "react";
import { Route, Routes } from "react-router";
import { isVisible } from "../config/navigation.js";
import AppLayout from "../layouts/AppLayout.jsx";
import PermissionRoute, { HomeRedirect } from "./PermissionRoute.jsx";
import PrivateRoute from "./PrivateRoute.jsx";

const Login = lazy(() => import("../pages/Login.jsx"));
const ChangePassword = lazy(() => import("../pages/ChangePassword.jsx"));

// Every feature page is its own chunk: the 3D globe and the chart library are
// only downloaded by somebody who opens those pages.
const TelemetryPage = lazy(() => import("../modules/telemetry/pages/TelemetryPage.jsx"));
const ChartsPage = lazy(() => import("../modules/charts/pages/ChartsPage.jsx"));
const DecodePage = lazy(() => import("../modules/decode/pages/DecodePage.jsx"));
const DisplayPage = lazy(() => import("../modules/display/pages/DisplayPage.jsx"));
const OrbitPage = lazy(() => import("../modules/orbit/pages/OrbitPage.jsx"));
const DocsPage = lazy(() => import("../modules/docs/pages/DocsPage.jsx"));

export default function AppRoutes() {
  return (
    <Suspense fallback={null}>
      <Routes>
        <Route path="/login" element={<Login />} />
        <Route path="/change-password" element={<ChangePassword />} />
        <Route element={<PrivateRoute />}>
          <Route element={<AppLayout />}>
            <Route path="/" element={<HomeRedirect />} />
            <Route element={<PermissionRoute perm="TELEMETRY_VIEW" />}>
              <Route path="/telemetry" element={<TelemetryPage />} />
              <Route path="/charts" element={<ChartsPage />} />
            </Route>
            <Route element={<PermissionRoute perm="DECODE_VIEW" />}>
              <Route path="/decode" element={<DecodePage />} />
            </Route>
            <Route element={<PermissionRoute perm="DISPLAY_VIEW" />}>
              <Route path="/display" element={<DisplayPage />} />
            </Route>
            {isVisible("/orbit") && (
              <Route element={<PermissionRoute perm="TELEMETRY_VIEW" />}>
                <Route path="/orbit" element={<OrbitPage />} />
              </Route>
            )}
            <Route element={<PermissionRoute perm="DOCS_VIEW" />}>
              <Route path="/docs/:section?" element={<DocsPage />} />
            </Route>
            <Route path="*" element={<HomeRedirect />} />
          </Route>
        </Route>
      </Routes>
    </Suspense>
  );
}
