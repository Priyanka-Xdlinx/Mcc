import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// The backend owns both the REST API and the telemetry socket. Proxying them
// keeps the dev server same-origin, so there is no CORS difference between
// development and the production build that Spring serves itself.
export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      '/api': { target: 'http://localhost:8080', changeOrigin: true },
      '/ws': { target: 'ws://localhost:8080', ws: true },
    },
  },
  build: { outDir: 'dist' },
})
