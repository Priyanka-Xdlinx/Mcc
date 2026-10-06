# OrbLinx MCC — frontend

The mission-control console for the MCC backend: live telemetry, charts, the
decode trace, display pages, the ground track and the system docs.

React 19 · Vite · MUI · Redux Toolkit · react-router · axios · Jest.
Laid out the same way as `spacelinx_frontend`.

## Run

```sh
npm install
npm run dev        # http://localhost:5173, proxies /api, /auth, /ws and Swagger to :8080
npm test
npm run lint
npm run build      # dist/
```

The backend must be running (`python mcc.py start` in the repo root). Point the
dev proxy elsewhere with `MCC_BACKEND=http://host:8080 npm run dev`. Every
setting has a default; `.env.example` lists what can be overridden.

## Layout

```
src/
  api/              axios client, auth/CSRF interceptors, one file per backend area
  assets/           logos and the global stylesheet (design tokens, light + dark)
  auth/             password policy
  components/       app shell: header, user menu, feed control, status bar, chart
    common/         the UI kit - the only place @mui/material is imported
  config/           env.js (runtime settings), navigation.js (the pages)
  hooks/            telemetry socket, clock, resolved theme
  layouts/          AppLayout (signed in), AuthLayout (sign-in pages)
  modules/          one folder per feature: pages/ components/ hooks/ utils/ + its stylesheet
    telemetry/      every downlinked parameter, filterable
    charts/         sections of plot / stat / value widgets
    decode/         whole-frame byte map and one-parameter trace
    display/        floating 20-slot display pages, saved per operator
    orbit/          3D globe, ground map and position charts from GPS
    docs/           live DB schema, Swagger, WebSocket reference
  pages/            Login, ChangePassword
  redux/            auth, app (ui + notifications), telemetry - each behind an index.js
  routes/           AppRoutes (lazy pages), PrivateRoute
  theme/            MUI theme, both palettes
  utils/            formatting, storage, logger, chart maths
test/               Jest: setup, mocks, render helper, unit tests mirroring src/
```

Conventions: a component imports `redux/<feature>`, never a slice file; pages
compose `components/common` and never import MUI directly. The instrument views
(byte map, display windows, globe HUD) use their own feature stylesheets on the
shared CSS tokens, scoped under `.feature`.

## Differences from `frontend/`

Ported: sign-in, users admin, telemetry table, decode trace, display pages,
orbit, and docs (schema, Swagger, stream). Not ported:

- **Docs**: the static design write-ups (requirements, HLD/LLD, timing,
  traceability, risks, glossary). They are prose, better kept as Markdown in the repo.
- **Charts**: the derived-parameter calculator, thresholds, drag-to-reorder and
  the gauge/bits/table/bar widget types. Plot, stat card and value remain.
- Dead code in the old app: `Dashboard`, `Trends`, `XdlinxPage`, `platforms`.
- The collapsed-header hamburger on Display is a single "show header" button.
