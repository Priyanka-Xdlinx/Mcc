import { useEffect, useMemo, useRef, useState } from 'react'
import type { ThemeChoice } from '../theme'
import { plural } from '../format'

type Section = 'build' | 'database' | 'api' | 'stream'

const SECTIONS: { id: Section; label: string; hint: string }[] = [
  { id: 'build', label: 'How it is built', hint: 'Architecture, build and run, decode, tests' },
  { id: 'database', label: 'Database schema', hint: 'Live tables, keys and row counts' },
  { id: 'api', label: 'REST API (Swagger)', hint: 'Every endpoint, try it out' },
  { id: 'stream', label: 'Live stream', hint: 'The WebSocket messages' },
]

export function Docs({ theme }: { theme: ThemeChoice }) {
  const [section, setSectionState] = useState<Section>(() => {
    const s = location.hash.split('/')[1]
    return (SECTIONS.some((x) => x.id === s) ? s : 'build') as Section
  })
  const setSection = (s: Section) => {
    setSectionState(s)
    history.replaceState(null, '', `#docs/${s}`)
  }
  return (
    <div className="docs">
      <nav className="docs-nav" aria-label="Documentation sections">
        <div className="docs-nav-title">Documentation</div>
        {SECTIONS.map((s) => (
          <button
            key={s.id}
            className={section === s.id ? 'on' : ''}
            aria-current={section === s.id ? 'page' : undefined}
            onClick={() => setSection(s.id)}
          >
            <span className="dn-label">{s.label}</span>
            <span className="dn-hint">{s.hint}</span>
          </button>
        ))}
      </nav>
      <div className={`docs-body ${section === 'api' ? 'wide' : ''}`}>
        {section === 'build' && <BuildDoc />}
        {section === 'database' && <DatabaseDoc />}
        {section === 'api' && <ApiDoc theme={theme} />}
        {section === 'stream' && <StreamDoc />}
      </div>
    </div>
  )
}

/* ─────────────────────────────── How it is built ─────────────────────────── */

function BuildDoc() {
  return (
    <article className="doc">
      <h1>How OrbLinx MCC is built</h1>
      <p className="lead">
        The MCC receives the XMCC v1 subframe feed from the XDLINX core team's logic, decodes every
        downlinked parameter to an engineering value, stores it, and shows it live in this dashboard.
      </p>

      <h2>Architecture</h2>
      <figure className="doc-fig">
        <svg viewBox="0 0 980 250" role="img"
             aria-label="The XDLINX core logic sends XMCC subframes on TCP 9100 to the Spring Boot backend, which decodes them, stores them in SQLite and pushes them to this React dashboard over REST and WebSocket.">
          <defs>
            <marker id="docarrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto">
              <path d="M0,0 L10,5 L0,10 z" className="d-arrowhead" />
            </marker>
          </defs>
          <rect className="d-box" x="10" y="70" width="170" height="92" rx="8" />
          <text className="d-title" x="95" y="100" textAnchor="middle">XDLINX core logic</text>
          <text className="d-sub" x="95" y="120" textAnchor="middle">XDOS OBC + subsystems</text>
          <text className="d-sub" x="95" y="138" textAnchor="middle">xdos_tm_view</text>

          <rect className="d-box d-main" x="250" y="20" width="440" height="200" rx="10" />
          <text className="d-label" x="266" y="42">SPRING BOOT BACKEND · JAVA 17 · :8080</text>
          <rect className="d-step" x="266" y="56" width="126" height="52" rx="6" />
          <text className="d-title" x="329" y="78" textAnchor="middle">XmccClient</text>
          <text className="d-sub" x="329" y="96" textAnchor="middle">framing, resync</text>
          <rect className="d-step" x="408" y="56" width="126" height="52" rx="6" />
          <text className="d-title" x="471" y="78" textAnchor="middle">TierAssembler</text>
          <text className="d-sub" x="471" y="96" textAnchor="middle">VH H M L blocks</text>
          <rect className="d-step" x="550" y="56" width="126" height="52" rx="6" />
          <text className="d-title" x="613" y="78" textAnchor="middle">ParameterDecoder</text>
          <text className="d-sub" x="613" y="96" textAnchor="middle">LUT + MIB rules</text>
          <rect className="d-step" x="266" y="148" width="190" height="56" rx="6" />
          <text className="d-title" x="361" y="171" textAnchor="middle">TelemetryRepo</text>
          <text className="d-sub" x="361" y="190" textAnchor="middle">SQLite mcc.db</text>
          <rect className="d-step" x="486" y="148" width="190" height="56" rx="6" />
          <text className="d-title" x="581" y="171" textAnchor="middle">REST + TelemetrySocket</text>
          <text className="d-sub" x="581" y="190" textAnchor="middle">/api/** · /ws/telemetry</text>
          <line className="d-line" x1="392" y1="82" x2="404" y2="82" markerEnd="url(#docarrow)" />
          <line className="d-line" x1="534" y1="82" x2="546" y2="82" markerEnd="url(#docarrow)" />
          <path className="d-line" d="M613,108 L613,128 L361,128 L361,144" markerEnd="url(#docarrow)" />
          <path className="d-line" d="M613,108 L613,144" markerEnd="url(#docarrow)" />

          <rect className="d-box" x="770" y="70" width="200" height="92" rx="8" />
          <text className="d-title" x="870" y="100" textAnchor="middle">React dashboard</text>
          <text className="d-sub" x="870" y="120" textAnchor="middle">Vite build, served by</text>
          <text className="d-sub" x="870" y="138" textAnchor="middle">the backend</text>

          <line className="d-line strong" x1="180" y1="116" x2="246" y2="116" markerEnd="url(#docarrow)" />
          <text className="d-edge" x="213" y="106" textAnchor="middle">TCP 9100</text>
          <text className="d-edge" x="213" y="134" textAnchor="middle">1,306 B/s</text>
          <line className="d-line strong" x1="690" y1="176" x2="766" y2="140" markerEnd="url(#docarrow)" />
          <text className="d-edge" x="730" y="182" textAnchor="middle">JSON</text>
        </svg>
        <figcaption>
          One process does the work: it reads the feed, decodes, stores and serves. The dashboard is a
          static build served by the same process, so it needs no separate web server.
        </figcaption>
      </figure>

      <h2>Components</h2>
      <div className="doc-table">
        <table>
          <thead><tr><th>Part</th><th>Where</th><th>What it does</th></tr></thead>
          <tbody>
            <tr><td>Feed client</td><td><code>backend/…/wire/XmccClient.java</code></td><td>Connects to the XMCC feed on TCP 9100, reads the 26-byte header and the payload length it announces, resynchronises on a bad magic, reconnects every 1 s when the feed is down.</td></tr>
            <tr><td>Tier assembly</td><td><code>wire/TierAssembler.java</code>, <code>Tier.java</code></td><td>Cuts each 1,280-byte payload into VH (0–399), H (400–799), M (800–1099) and L (1100–1274), and joins H, M and L pieces over 2, 4 and 8 subframes into whole blocks.</td></tr>
            <tr><td>Decoder</td><td><code>decode/ParameterDecoder.java</code></td><td>Uses the LUT picks to rebuild each subsystem reply, then the MIB field definition (bit offset, length, type, endianness, scale, enum) to produce the engineering value and its quality.</td></tr>
            <tr><td>Ingest</td><td><code>IngestService.java</code></td><td>Runs the pipeline per subframe; tracks sessions, warm-up, lost subframes and incomplete blocks; stores values and pushes them to the socket.</td></tr>
            <tr><td>Storage</td><td><code>store/TelemetryRepo.java</code>, <code>SchemaRepo.java</code></td><td>Latest value per parameter plus history in SQLite; loads the MIB and LUT tables at start and checks the LUT CRC-16.</td></tr>
            <tr><td>API</td><td><code>api/*Controller.java</code></td><td>The REST endpoints in the Swagger section, plus the WebSocket in Live stream.</td></tr>
            <tr><td>Feed control</td><td><code>sim/SimulatorService.java</code></td><td>Starts and stops the XDLINX stack with one fixed command; nothing from a request reaches a shell.</td></tr>
            <tr><td>Schema builder</td><td><code>schema/build_schema.py</code></td><td>Builds <code>mcc.db</code> from the mission database JSON (<code>updated xdos/mib/*.json</code>) and the LUT twin (<code>config/rate_group_lut_bin_gs.json</code>). Never from the flattened CSV, which lacks endianness.</td></tr>
            <tr><td>Dashboard</td><td><code>frontend/src</code></td><td>React 18 + TypeScript, built by Vite into <code>backend/src/main/resources/static</code>.</td></tr>
          </tbody>
        </table>
      </div>

      <h2>Build and run</h2>
      <ol className="doc-steps">
        <li>
          <b>Decode schema.</b> Reads the MIB and the LUT twin, writes <code>mcc.db</code>.
          <pre>cd mcc-app/schema && python3 build_schema.py</pre>
        </li>
        <li>
          <b>Dashboard.</b> Type-checks and bundles the React app, then stages it inside the backend.
          <pre>{`cd mcc-app/frontend && npm install && npm run build
rm -rf ../backend/src/main/resources/static
cp -r dist ../backend/src/main/resources/static`}</pre>
        </li>
        <li>
          <b>Backend.</b> Compiles, runs the 17 tests and packages one executable jar.
          <pre>cd mcc-app/backend && mvn clean package</pre>
        </li>
        <li>
          <b>Run.</b> <code>start.sh</code> builds anything missing, starts the backend, waits for it to answer,
          and with <code>--sim</code> also starts the XDLINX stack. <code>--build</code> forces steps 1–3.
          <pre>{`cd mcc-app
./start.sh --sim      # backend + telemetry stack
./start.sh --build    # rebuild everything first
./stop.sh             # stop both`}</pre>
        </li>
      </ol>

      <h2>How a value is decoded</h2>
      <ol className="doc-steps compact">
        <li><b>Header.</b> 26 bytes, big-endian: magic <code>XMCC</code>, version 1, master frame (u32), subframe 1–16 (u16), spacecraft time to the millisecond, payload length (u32).</li>
        <li><b>Tier split.</b> The payload is always cut at the same byte positions: VH, H, M, L and 5 reserved bytes.</li>
        <li><b>Block assembly.</b> VH is whole in every subframe. H, M and L arrive one piece per subframe and are joined over 2, 4 and 8 subframes. A missing piece gives quality <code>BLOCK_INCOMPLETE</code>.</li>
        <li><b>LUT picks.</b> The LUT (CRC-16 checked at start) says which bytes of the block belong to which subsystem reply; they are copied back to their place in that reply.</li>
        <li><b>MIB decode.</b> ADCS and XLINK-S fields are bit-packed, least significant bit first; the others are byte-addressed with their own endianness. <code>scale</code> is a multiplier. All <code>0xFF</code> across a field means the subsystem did not answer (<code>NO_ANSWER</code>), not the value 255.</li>
      </ol>

      <h2>Tests and verification</h2>
      <div className="doc-table">
        <table>
          <thead><tr><th>Test</th><th className="r">Cases</th><th>What it proves</th></tr></thead>
          <tbody>
            <tr><td>ParameterDecoderTest</td><td className="r">10</td><td>Golden vectors from captured bytes: LSB-first bit fields, scale direction, IEEE-754 byte order, sign extension, fields crossing a byte boundary.</td></tr>
            <tr><td>PipelineTest</td><td className="r">5</td><td>Tier spans, absolute pick offsets, gap reporting, header rejection.</td></tr>
            <tr><td>GoldenParityTest</td><td className="r">1</td><td>16,908 readings from a recorded capture are identical to the reference decoder (<code>tools/mcc_decode.py</code>), zero tolerance.</td></tr>
            <tr><td>GroundStationCrossCheckTest</td><td className="r">1</td><td>16,800 readings compared with the XDOS display log: 15,899 identical; every difference is a registered one (536 XLINK offset, 5 no-answer sentinel, 360 display label only).</td></tr>
          </tbody>
        </table>
      </div>

      <h2>Stack and ports</h2>
      <div className="doc-grid">
        <dl>
          <dt>Backend</dt><dd>Java 17 · Spring Boot 3.3.5 · SQLite (xerial JDBC 3.46) · springdoc-openapi 2.6</dd>
          <dt>Dashboard</dt><dd>React 18.3 · TypeScript 5.9 · Vite 5.4 · no UI framework, no chart library</dd>
          <dt>Build tools</dt><dd>Maven 3.9 · Node 22 · Python 3 (schema builder)</dd>
        </dl>
        <dl>
          <dt>8080</dt><dd>Backend: dashboard, <code>/api/**</code>, <code>/ws/telemetry</code>, Swagger</dd>
          <dt>9100</dt><dd>XMCC feed from the XDLINX core logic</dd>
          <dt>10000–10070</dt><dd>XDLINX stack internals (OBC and subsystems)</dd>
        </dl>
      </div>
    </article>
  )
}

/* ──────────────────────────────── Database schema ────────────────────────── */

interface Column { name: string; type: string; notNull: boolean; pk: number; default: string | null }
interface ForeignKey { column: string; references: string; referencesColumn: string }
interface Index { name: string; unique: boolean; origin: string; columns: string[] }
interface Table { name: string; rows: number; columns: Column[]; foreignKeys: ForeignKey[]; indexes: Index[] }
interface Schema { engine: string; journalMode: string; pageSize: number; pageCount: number; tables: Table[] }

const GROUPS: { title: string; note: string; tables: string[] }[] = [
  { title: 'Mission database (MIB)', note: 'What each message and field means', tables: ['subsystem', 'enum_type', 'enum_value', 'message', 'parameter'] },
  { title: 'Downlink map (LUT)', note: 'Which bytes belong to which message', tables: ['tier', 'lut', 'lut_entry', 'pick', 'pick_param'] },
  { title: 'Telemetry', note: 'The decoded values', tables: ['telemetry_latest', 'telemetry_history'] },
]

const TABLE_NOTES: Record<string, string> = {
  subsystem: 'One row per subsystem in the mission database, with its exact MIB spelling (ADCS, EPS, GPS, Payload, SENSOR, UHF, XLINK-S).',
  enum_type: 'Named enumerations, scoped per subsystem (two subsystems may reuse a name).',
  enum_value: 'The labels of each enumeration, e.g. 3 → "Aborted".',
  message: 'Telemetry messages from the MIB. Direction is part of the key: 41 ADCS names exist as both TC and TM.',
  parameter: 'Every field of every message: offset and length normalised to bits, data type, endianness, scale (always a multiplier), unit, enum.',
  tier: 'The four fixed parts of the payload: byte offset, size and how many subframes make one block.',
  lut: 'The lookup table in flight: its CRC-16 must match the one the OBC uses, or the backend refuses to decode.',
  lut_entry: 'The 64 LUT entries: which subsystem reply goes into which tier, and at which position.',
  pick: 'Byte runs copied from a reply into the tier block, at absolute block offsets.',
  pick_param: 'The parameters inside each pick: the 755 downlinked parameter slots.',
  telemetry_latest: 'The newest reading of each parameter, overwritten in place.',
  telemetry_history: 'Readings over time, tagged with the run (session). Capped at 500,000 rows and pruned every 30 s.',
}

const POS: Record<string, [number, number]> = {
  subsystem: [20, 34], enum_type: [20, 104], enum_value: [20, 174], message: [20, 244], parameter: [20, 314],
  tier: [330, 34], lut: [330, 104], lut_entry: [330, 174], pick: [330, 244], pick_param: [330, 314],
  telemetry_latest: [640, 174], telemetry_history: [640, 314],
}
const BW = 230
const BH = 44

function DatabaseDoc() {
  const [schema, setSchema] = useState<Schema | null>(null)
  const [error, setError] = useState<string | null>(null)
  const [open, setOpen] = useState<string | null>('parameter')

  useEffect(() => {
    fetch('/api/schema')
      .then((r) => (r.ok ? r.json() : Promise.reject(new Error(`HTTP ${r.status}`))))
      .then(setSchema)
      .catch((e) => setError(String(e.message ?? e)))
  }, [])

  const byName = useMemo(() => new Map((schema?.tables ?? []).map((t) => [t.name, t])), [schema])
  const links = useMemo(() => {
    const out: { from: string; to: string; col: string }[] = []
    for (const t of schema?.tables ?? []) {
      for (const fk of t.foreignKeys) if (POS[t.name] && POS[fk.references]) out.push({ from: t.name, to: fk.references, col: fk.column })
    }
    return out
  }, [schema])

  return (
    <article className="doc">
      <h1>Database schema</h1>
      <p className="lead">
        Read live from the running database, so it always matches what the backend uses.
        {schema && (
          <> {schema.engine} · {schema.tables.length} tables · {(schema.pageSize * schema.pageCount / 1048576).toFixed(0)} MB on disk · journal mode {schema.journalMode}.</>
        )}
      </p>
      {error && <div className="doc-error">Could not read the schema: {error}. Is the backend running?</div>}
      {!schema && !error && <div className="doc-loading">Reading the schema…</div>}

      {schema && (
        <>
          <h2>Relationships</h2>
          <figure className="doc-fig">
            <svg viewBox="0 0 900 380" role="img" aria-label="Entity relationship diagram of the MCC database: mission database tables, downlink map tables and telemetry tables, with foreign keys drawn as lines.">
              <defs>
                <marker id="erarrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto">
                  <path d="M0,0 L10,5 L0,10 z" className="d-arrowhead" />
                </marker>
              </defs>
              {GROUPS.map((g, i) => (
                <text key={g.title} className="d-label" x={[20, 330, 640][i]} y="20">{g.title.toUpperCase()}</text>
              ))}
              {links.map((l, i) => {
                const [fx, fy] = POS[l.from]
                const [tx, ty] = POS[l.to]
                const sameCol = fx === tx
                const d = sameCol
                  ? (() => {
                      const x = fx + BW
                      const off = 16 + (i % 3) * 10
                      return `M${x},${fy + BH / 2} C${x + off},${fy + BH / 2} ${x + off},${ty + BH / 2} ${x + 2},${ty + BH / 2}`
                    })()
                  : (() => {
                      const leftToRight = fx > tx
                      const x1 = leftToRight ? fx : fx + BW
                      const x2 = leftToRight ? tx + BW : tx
                      const mid = (x1 + x2) / 2
                      return `M${x1},${fy + BH / 2} C${mid},${fy + BH / 2} ${mid},${ty + BH / 2} ${x2 + (leftToRight ? 2 : -2)},${ty + BH / 2}`
                    })()
                return <path key={i} className="d-fk" d={d} markerEnd="url(#erarrow)"><title>{`${l.from}.${l.col} → ${l.to}`}</title></path>
              })}
              {Object.entries(POS).map(([name, [x, y]]) => {
                const t = byName.get(name)
                return (
                  <g key={name} className={`d-table ${open === name ? 'on' : ''}`} onClick={() => setOpen(name)} role="button"
                     aria-label={`${name}: ${t?.rows ?? 0} rows`}>
                    <rect x={x} y={y} width={BW} height={BH} rx="7" />
                    <text className="d-title" x={x + 12} y={y + 19}>{name}</text>
                    <text className="d-sub" x={x + 12} y={y + 35}>{t ? `${plural(t.rows, 'row')} · ${plural(t.columns.length, 'column')}` : 'missing'}</text>
                  </g>
                )
              })}
            </svg>
            <figcaption>Each line is a foreign key, drawn from the live schema; the arrow points to the referenced table. Click a table to see its columns below.</figcaption>
          </figure>

          {GROUPS.map((g) => (
            <section key={g.title}>
              <h2>{g.title} <span className="h-note">{g.note}</span></h2>
              <div className="tcards">
                {g.tables.map((name) => {
                  const t = byName.get(name)
                  if (!t) return null
                  const fkCol = new Map(t.foreignKeys.map((f) => [f.column, f]))
                  const isOpen = open === name
                  return (
                    <div className={`tcard ${isOpen ? 'open' : ''}`} key={name} id={`t-${name}`}>
                      <button className="tcard-head" onClick={() => setOpen(isOpen ? null : name)} aria-expanded={isOpen}>
                        <span className="tc-name">{name}</span>
                        <span className="tc-rows">{plural(t.rows, 'row')}</span>
                        <span className="tc-caret">{isOpen ? '−' : '+'}</span>
                      </button>
                      <p className="tc-note">{TABLE_NOTES[name]}</p>
                      {isOpen && (
                        <>
                          <div className="doc-table">
                            <table>
                              <thead><tr><th>Column</th><th>Type</th><th>Key</th><th>Null</th><th>Default</th></tr></thead>
                              <tbody>
                                {t.columns.map((c) => {
                                  const fk = fkCol.get(c.name)
                                  return (
                                    <tr key={c.name}>
                                      <td><code>{c.name}</code></td>
                                      <td className="dimcol">{c.type || '—'}</td>
                                      <td>
                                        {c.pk > 0 && <span className="keychip pk">PK</span>}
                                        {fk && <span className="keychip fk" title={`references ${fk.references}(${fk.referencesColumn})`}>FK → {fk.references}</span>}
                                      </td>
                                      <td className="dimcol">{c.notNull || c.pk > 0 ? 'no' : 'yes'}</td>
                                      <td className="dimcol">{c.default ?? ''}</td>
                                    </tr>
                                  )
                                })}
                              </tbody>
                            </table>
                          </div>
                          {t.indexes.length > 0 && (
                            <div className="tc-idx">
                              <b>Indexes</b>
                              {t.indexes.map((ix) => (
                                <span key={ix.name} className="idxchip">
                                  {ix.origin === 'pk' ? 'primary key' : ix.unique ? 'unique' : 'index'} ({ix.columns.join(', ')})
                                </span>
                              ))}
                            </div>
                          )}
                        </>
                      )}
                    </div>
                  )
                })}
              </div>
            </section>
          ))}

          <h2>Planned next</h2>
          <p>
            The store is being redesigned for the full mission database, several spacecraft and every value
            kept: PostgreSQL with raw frames, a 48-hour per-value table, packed 10-minute segments and hourly
            summaries, written by a Rust ingest and read by a Java API. That design is reviewed separately and
            is not what runs here yet.
          </p>
        </>
      )}
    </article>
  )
}

/* ─────────────────────────────────── Swagger ─────────────────────────────── */

// Swagger UI ships light only. In the dark theme it is inverted in place, with
// hue rotated back so the method colours (GET blue, POST green) keep their meaning.
const SWAGGER_DARK = `
  html { filter: invert(0.92) hue-rotate(180deg); background: #fff; }
  .swagger-ui .topbar { display: none; }
`
const SWAGGER_LIGHT = `.swagger-ui .topbar { display: none; }`

function useDark(choice: ThemeChoice) {
  const [sysDark, setSysDark] = useState(() => window.matchMedia('(prefers-color-scheme: dark)').matches)
  useEffect(() => {
    const m = window.matchMedia('(prefers-color-scheme: dark)')
    const on = () => setSysDark(m.matches)
    m.addEventListener('change', on)
    return () => m.removeEventListener('change', on)
  }, [])
  return choice === 'dark' || (choice === 'system' && sysDark)
}

function ApiDoc({ theme }: { theme: ThemeChoice }) {
  const frame = useRef<HTMLIFrameElement>(null)
  const [loaded, setLoaded] = useState(false)
  const dark = useDark(theme)

  useEffect(() => {
    const doc = frame.current?.contentDocument
    if (!loaded || !doc) return
    let style = doc.getElementById('mcc-theme') as HTMLStyleElement | null
    if (!style) {
      style = doc.createElement('style')
      style.id = 'mcc-theme'
      doc.head.appendChild(style)
    }
    style.textContent = dark ? SWAGGER_DARK : SWAGGER_LIGHT
  }, [loaded, dark])

  return (
    <article className="doc doc-api">
      <div className="api-head">
        <div>
          <h1>REST API</h1>
          <p className="lead">
            Generated by the backend from its own code (OpenAPI 3). Open an endpoint and press
            <b> Try it out</b> to call it against the running system.
          </p>
        </div>
        <div className="api-links">
          <a href="/swagger-ui.html" target="_blank" rel="noreferrer">Open full screen ↗</a>
          <a href="/v3/api-docs" target="_blank" rel="noreferrer">OpenAPI JSON ↗</a>
        </div>
      </div>
      <div className="swagger-wrap">
        {!loaded && <div className="doc-loading">Loading Swagger UI…</div>}
        <iframe
          ref={frame}
          title="Swagger UI for the OrbLinx MCC API"
          src="/swagger-ui/index.html"
          onLoad={() => setLoaded(true)}
          className={loaded ? '' : 'hidden-frame'}
        />
      </div>
    </article>
  )
}

/* ───────────────────────────────── Live stream ───────────────────────────── */

function StreamDoc() {
  return (
    <article className="doc">
      <h1>Live stream</h1>
      <p className="lead">
        The dashboard receives every decoded value over a WebSocket at <code>/ws/telemetry</code> on the same
        port as the API. It is one-way: the backend sends, the page listens. OpenAPI cannot describe
        WebSockets, so the messages are documented here.
      </p>

      <h2>Connecting</h2>
      <pre>{`const ws = new WebSocket('ws://localhost:8080/ws/telemetry')
ws.onmessage = (e) => console.log(JSON.parse(e.data))`}</pre>
      <p>
        On connect the backend sends one <code>status</code> message. After that it sends one{' '}
        <code>telemetry</code> message for each part (VH, H, M, L) that completes, which is 1 to 4 messages per
        second. A client that disconnects should reconnect and fetch <code>/api/telemetry/latest</code> again,
        as this dashboard does.
      </p>

      <h2>Messages</h2>
      <div className="doc-table">
        <table>
          <thead><tr><th>type</th><th>When</th><th>Fields</th></tr></thead>
          <tbody>
            <tr><td><code>status</code></td><td>Once, on connect</td><td><code>status</code>: the same object as <code>feed</code> in <code>GET /api/status</code></td></tr>
            <tr><td><code>telemetry</code></td><td>Each time a part's block completes</td><td><code>tier</code>, <code>masterFrame</code>, <code>subframe</code>, <code>spacecraftTime</code>, <code>updates[]</code> with <code>parameterId</code>, <code>entry</code>, <code>tier</code>, <code>name</code>, <code>value</code> (decoded text), <code>quality</code></td></tr>
            <tr><td><code>blockIncomplete</code></td><td>A block was abandoned because a piece never arrived</td><td><code>tier</code>, <code>masterFrame</code>, <code>blockIndex</code>, <code>slicesReceived</code>, <code>slicesExpected</code></td></tr>
          </tbody>
        </table>
      </div>

      <h2>Example: telemetry</h2>
      <pre>{`{
  "type": "telemetry",
  "tier": "VH",
  "masterFrame": 48,
  "subframe": 8,
  "spacecraftTime": "2026-09-25 13:08:20.737",
  "updates": [
    { "parameterId": 1980, "entry": "EP-016", "tier": "VH",
      "name": "Conv1 OP Voltage Reading", "value": "3.732 V", "quality": "OK" },
    { "parameterId": 1981, "entry": "EP-016", "tier": "VH",
      "name": "Conv2 OP Voltage Reading", "value": "3.749 V", "quality": "OK" }
  ]
}`}</pre>

      <h2>Quality values</h2>
      <div className="doc-table">
        <table>
          <thead><tr><th>quality</th><th>Meaning</th></tr></thead>
          <tbody>
            <tr><td><code>OK</code></td><td>Decoded normally.</td></tr>
            <tr><td><code>NO_ANSWER</code></td><td>Every byte of the field was 0xFF: the subsystem did not reply to the OBC's poll.</td></tr>
            <tr><td><code>BLOCK_INCOMPLETE</code></td><td>A piece of the H, M or L block never arrived, so the field cannot be read.</td></tr>
            <tr><td><code>NOT_DOWNLINKED</code></td><td>No LUT pick covers the field; the bytes are fill, not data.</td></tr>
            <tr><td><code>WARMUP</code></td><td>First master frame after the backend starts or the OBC reboots; values are decoded but not trusted.</td></tr>
          </tbody>
        </table>
      </div>
    </article>
  )
}
