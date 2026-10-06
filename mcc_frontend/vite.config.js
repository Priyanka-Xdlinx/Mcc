import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";

// The MCC backend owns the REST API, the auth endpoints and the telemetry
// socket. Proxying them keeps the dev server same-origin, so cookies, CSRF and
// the WebSocket behave exactly as in the production build the backend serves.
const BACKEND = process.env.MCC_BACKEND ?? "http://localhost:8080";

export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      "/api": { target: BACKEND, changeOrigin: true },
      "/auth": { target: BACKEND },
      "/ws": { target: BACKEND.replace(/^http/, "ws"), ws: true },
      // Swagger UI and the OpenAPI description, shown on the Docs page
      "/swagger-ui": { target: BACKEND, changeOrigin: true },
      "/v3": { target: BACKEND, changeOrigin: true },
    },
  },
  build: {
    sourcemap: false,
  },
});
