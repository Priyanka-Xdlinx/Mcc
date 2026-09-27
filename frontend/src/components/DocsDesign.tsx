/**
 * Design documentation: requirements, high-level design, low-level design,
 * timing, traceability, risks and glossary. Figures marked "measured" were taken
 * on the running system on 2026-09-27 (Apple M4, feed at 1 subframe/s, 755
 * downlinked parameters); the rest are the values in the code and config.
 */

function Arrow({ id }: { id: string }) {
  return (
    <marker id={id} viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto">
      <path d="M0,0 L10,5 L0,10 z" className="d-arrowhead" />
    </marker>
  )
}

const Status = ({ ok, text }: { ok: boolean | null; text: string }) => (
  <span className={`keychip ${ok === null ? '' : ok ? 'ok' : 'fk'}`} style={ok === null ? { background: 'var(--panel-2)', color: 'var(--dim)' } : undefined}>
    {text}
  </span>
)

/* ───────────────────────────────── Requirements ──────────────────────────── */

export function RequirementsDoc() {
  return (
    <article className="doc">
      <h1>Requirements</h1>
      <p className="lead">
        What the MCC must do, how well, and within which limits. Every requirement has an id used in
        Traceability, a way it is verified, and its status today.
      </p>

      <h2>Functional</h2>
      <div className="doc-table">
        <table>
          <thead><tr><th>Id</th><th>Requirement</th><th>Verified by</th><th>Status</th></tr></thead>
          <tbody>
            <tr><td>FR-01</td><td>Connect to the XMCC v1 feed on TCP 9100 as a client and reconnect by itself when the feed drops.</td><td>Stop/start the stack; watch the link state</td><td><Status ok text="met" /></td></tr>
            <tr><td>FR-02</td><td>Check every header: magic <code>XMCC</code>, version 1, payload length 1 B–1 MiB; resynchronise on a bad magic; refuse any other version.</td><td>PipelineTest</td><td><Status ok text="met" /></td></tr>
            <tr><td>FR-03</td><td>Rebuild the VH, H, M and L blocks from 1, 2, 4 and 8 subframes; report a block with a missing piece.</td><td>PipelineTest</td><td><Status ok text="met" /></td></tr>
            <tr><td>FR-04</td><td>Decode every parameter the flown LUT downlinks (755 today) into an engineering value using the MIB: bit or byte addressing, endianness, scale (multiplier), enumerations.</td><td>ParameterDecoderTest, GoldenParityTest</td><td><Status ok text="met" /></td></tr>
            <tr><td>FR-05</td><td>Give every value a quality: OK, NO_ANSWER (all 0xFF), BLOCK_INCOMPLETE, NOT_DOWNLINKED, WARMUP.</td><td>GroundStationCrossCheckTest, UI</td><td><Status ok text="met" /></td></tr>
            <tr><td>FR-06</td><td>Refuse to decode when the LUT the schema was built from does not match the LUT twin on disk (CRC-16).</td><td>Start with a changed twin</td><td><Status ok text="met" /></td></tr>
            <tr><td>FR-07</td><td>Keep the latest value of every parameter and a history per run (session).</td><td>GET /api/telemetry/latest, /history</td><td><Status ok text="met" /></td></tr>
            <tr><td>FR-08</td><td>Push decoded values to the dashboard as they are decoded.</td><td>WebSocket /ws/telemetry</td><td><Status ok text="met" /></td></tr>
            <tr><td>FR-09</td><td>Show, for any parameter on any of the last 320 subframes, every step from payload bytes to engineering value.</td><td>Decode trace page, GET /api/trace</td><td><Status ok text="met" /></td></tr>
            <tr><td>FR-10</td><td>Chart any parameter over time on a UTC axis, with zoom.</td><td>Charts page</td><td><Status ok text="met" /></td></tr>
            <tr><td>FR-11</td><td>Start and stop the XDLINX telemetry stack from the dashboard, with a confirmation before stopping.</td><td>Header feed control</td><td><Status ok text="met" /></td></tr>
            <tr><td>FR-12</td><td>Show the spacecraft position from GPS BESTPOS on a globe and a map, with orbit figures measured from the track.</td><td>Ground track page</td><td><Status ok text="met" /></td></tr>
            <tr><td>FR-13</td><td>Describe the REST API in OpenAPI and offer it in Swagger UI.</td><td>/v3/api-docs, Docs → REST API</td><td><Status ok text="met" /></td></tr>
            <tr><td>FR-14</td><td>Show the database schema as it is, not as documented.</td><td>GET /api/schema</td><td><Status ok text="met" /></td></tr>
            <tr><td>FR-15</td><td>Keep every value for at least 30 days, for several spacecraft.</td><td>Postgres redesign</td><td><Status ok={false} text="planned" /></td></tr>
            <tr><td>FR-16</td><td>Send telecommands and record their replies.</td><td>—</td><td><Status ok={null} text="future" /></td></tr>
          </tbody>
        </table>
      </div>

      <h2>Non-functional</h2>
      <div className="doc-table">
        <table>
          <thead><tr><th>Id</th><th>Quality</th><th>Target</th><th>Measured / checked</th><th>Status</th></tr></thead>
          <tbody>
            <tr><td>NFR-01</td><td>Decode correctness</td><td>100 % identical to the reference decoder</td><td>16,908 of 16,908 readings identical</td><td><Status ok text="met" /></td></tr>
            <tr><td>NFR-02</td><td>Freshness: spacecraft time to stored value</td><td>&lt; 1 s on average</td><td>502 ms average, 126 ms minimum (103 VH readings); first seconds after a start up to 3.8 s</td><td><Status ok text="met" /></td></tr>
            <tr><td>NFR-03</td><td>Throughput</td><td>Keep up with 1 subframe/s (352 values/s)</td><td>351.3 values/s stored, no backlog</td><td><Status ok text="met" /></td></tr>
            <tr><td>NFR-04</td><td>Decode time per subframe</td><td>&lt; 100 ms (10 % of the 1 s period)</td><td>48 subframes in 77 ms in GoldenParityTest, under 2 ms each</td><td><Status ok text="met" /></td></tr>
            <tr><td>NFR-05</td><td>API response for dashboard calls</td><td>&lt; 100 ms median</td><td>1.5–26 ms median; 5,000-row history 71 ms median, 160 ms max</td><td><Status ok text="met" /></td></tr>
            <tr><td>NFR-06</td><td>Recovery</td><td>Feed reconnect ≤ 1 s after it returns; page recovers without reload</td><td>Reconnect every 1,000 ms; page reloads its data on socket reconnect</td><td><Status ok text="met" /></td></tr>
            <tr><td>NFR-07</td><td>Retention</td><td>≥ 30 days of every value</td><td>24.1 minutes (500,000-row cap)</td><td><Status ok={false} text="not met" /></td></tr>
            <tr><td>NFR-08</td><td>Accessibility</td><td>WCAG 2.1 AA text contrast in both themes; keyboard focus visible</td><td>Every text colour ≥ 4.5 : 1, computed; focus ring on every control</td><td><Status ok text="met" /></td></tr>
            <tr><td>NFR-09</td><td>Layout</td><td>No horizontal scroll from 360 to 1,440 px</td><td>0 px overflow on every page at 12 widths</td><td><Status ok text="met" /></td></tr>
            <tr><td>NFR-10</td><td>Works offline</td><td>No runtime dependency on the internet</td><td>Fonts, logos and images bundled; only outbound links need a network</td><td><Status ok text="met" /></td></tr>
            <tr><td>NFR-11</td><td>Safety of feed control</td><td>Nothing from a request reaches a shell</td><td>One fixed command, no parameters</td><td><Status ok text="met" /></td></tr>
            <tr><td>NFR-12</td><td>Access control</td><td>Operators sign in; actions are audited</td><td>Not implemented; runs on localhost only</td><td><Status ok={false} text="not met" /></td></tr>
          </tbody>
        </table>
      </div>

      <h2>Constraints</h2>
      <ul className="doc-list">
        <li><b>Input is fixed by the XDLINX core team:</b> XMCC v1 over TCP, 26-byte header + 1,280-byte payload, one message per second. The MCC cannot ask for anything.</li>
        <li><b>No names or ids on the wire.</b> Meaning comes only from the LUT twin and the MIB JSON; both must match what the spacecraft flies.</li>
        <li><b>No spacecraft id and no frame CRC flag in XMCC v1.</b> One feed = one spacecraft; a corrupted frame cannot be detected here.</li>
        <li><b>The LUT CRC is not downlinked.</b> The check is against the twin file, not the table in flight.</li>
        <li><b>Single host.</b> Java 17, SQLite in one file, ports 8080 (MCC) and 9100 (feed).</li>
      </ul>

      <h2>Assumptions</h2>
      <ul className="doc-list">
        <li>Spacecraft time in the header is UTC calendar time with 1 ms resolution, no leap seconds.</li>
        <li>The master frame counter only goes back when the OBC reboots (its 16-bit wrap every 18 h 12 min is a known exception, see Risks).</li>
        <li>Values in the current feed come from the XDLINX core team's logic; some fields carry test patterns (e.g. −9.3e+61) and are decoded as sent.</li>
      </ul>
    </article>
  )
}

/* ─────────────────────────────── High-level design ───────────────────────── */

export function HldDoc() {
  return (
    <article className="doc">
      <h1>High-level design</h1>
      <p className="lead">The system in its surroundings, the parts it is made of, and how data moves through them.</p>

      <h2>Context</h2>
      <figure className="doc-fig">
        <svg viewBox="0 0 900 230" role="img" aria-label="Context: the XDLINX core logic sends XMCC to the MCC; operators use the MCC in a browser; the MCC reads the MIB and LUT twin files.">
          <defs><Arrow id="hldA" /></defs>
          <rect className="d-box" x="20" y="80" width="200" height="74" rx="8" />
          <text className="d-title" x="120" y="110" textAnchor="middle">XDLINX core logic</text>
          <text className="d-sub" x="120" y="130" textAnchor="middle">OBC, subsystems, forwarder</text>
          <rect className="d-box d-main" x="340" y="70" width="220" height="94" rx="10" />
          <text className="d-title" x="450" y="108" textAnchor="middle">OrbLinx MCC</text>
          <text className="d-sub" x="450" y="128" textAnchor="middle">decode · store · display</text>
          <rect className="d-box" x="680" y="80" width="200" height="74" rx="8" />
          <text className="d-title" x="780" y="110" textAnchor="middle">Operators</text>
          <text className="d-sub" x="780" y="130" textAnchor="middle">browser</text>
          <rect className="d-box" x="340" y="186" width="220" height="36" rx="6" />
          <text className="d-sub" x="450" y="209" textAnchor="middle">MIB JSON + LUT twin (files)</text>
          <line className="d-line strong" x1="220" y1="117" x2="336" y2="117" markerEnd="url(#hldA)" />
          <text className="d-edge" x="278" y="108" textAnchor="middle">XMCC v1 · TCP 9100</text>
          <line className="d-line" x1="676" y1="108" x2="564" y2="108" markerEnd="url(#hldA)" />
          <line className="d-line" x1="564" y1="130" x2="676" y2="130" markerEnd="url(#hldA)" />
          <text className="d-edge" x="620" y="98" textAnchor="middle">HTTP</text>
          <text className="d-edge" x="620" y="148" textAnchor="middle">JSON · WebSocket</text>
          <line className="d-line" x1="450" y1="186" x2="450" y2="168" markerEnd="url(#hldA)" />
          <text className="d-edge" x="120" y="60" textAnchor="middle">one-way, no reply</text>
        </svg>
        <figcaption>The feed is one-way: the MCC never writes back to the XDLINX side. Start and stop of the stack is a local process command, not a message on the feed.</figcaption>
      </figure>

      <h2>Building blocks</h2>
      <div className="doc-table">
        <table>
          <thead><tr><th>Block</th><th>Responsibility</th><th>Talks to</th></tr></thead>
          <tbody>
            <tr><td>Wire layer</td><td>Framing, header checks, resync, tier split, block assembly, frame buffer (320 subframes).</td><td>Feed socket → Ingest</td></tr>
            <tr><td>Decode layer</td><td>LUT picks → subsystem reply → MIB fields → engineering value + quality; decode trace on the same code path.</td><td>Ingest, Decode API</td></tr>
            <tr><td>Ingest</td><td>Runs the pipeline per subframe; sessions, warm-up, gaps, incomplete blocks; store and publish.</td><td>Wire, Decode, Storage, Live push</td></tr>
            <tr><td>Storage</td><td>MIB + LUT tables (read at start), latest value per parameter, history per run, pruning.</td><td>SQLite mcc.db</td></tr>
            <tr><td>API</td><td>REST for telemetry, frames, trace, status, schema, feed control; OpenAPI.</td><td>Browser</td></tr>
            <tr><td>Live push</td><td>One WebSocket broadcast per completed tier block.</td><td>Browser</td></tr>
            <tr><td>Dashboard</td><td>React pages: Dashboard, Telemetry, Charts, Ground track, Decode trace, Docs, XDLINX.</td><td>API, Live push</td></tr>
          </tbody>
        </table>
      </div>

      <h2>Data flow</h2>
      <figure className="doc-fig">
        <svg viewBox="0 0 980 150" role="img" aria-label="Data flow: subframe, header check, tier split, block assembly, LUT picks, MIB decode, store and push, dashboard.">
          <defs><Arrow id="hldB" /></defs>
          {[
            ['Subframe', '1,306 B'],
            ['Header check', 'magic, version, length'],
            ['Tier split', 'VH H M L'],
            ['Block assembly', '1 / 2 / 4 / 8'],
            ['LUT picks', 'bytes → reply'],
            ['MIB decode', 'value + quality'],
            ['Store + push', 'SQLite · WebSocket'],
          ].map(([a, b], i) => (
            <g key={a}>
              <rect className={`d-step${i === 6 ? '' : ''}`} x={10 + i * 139} y="40" width="122" height="62" rx="7" />
              <text className="d-title" x={71 + i * 139} y="66" textAnchor="middle">{a}</text>
              <text className="d-sub" x={71 + i * 139} y="85" textAnchor="middle">{b}</text>
              {i < 6 && <line className="d-line" x1={132 + i * 139} y1="71" x2={147 + i * 139} y2="71" markerEnd="url(#hldB)" />}
            </g>
          ))}
          <text className="d-label" x="10" y="26">ONE SUBFRAME, EVERY SECOND, ON ONE THREAD</text>
          <text className="d-sub" x="10" y="130">Values of H, M and L wait in memory until their last piece arrives (up to 8 s for L).</text>
        </svg>
      </figure>

      <h2>Runtime and deployment</h2>
      <div className="doc-table">
        <table>
          <thead><tr><th>Process</th><th>Port</th><th>Started by</th><th>Notes</th></tr></thead>
          <tbody>
            <tr><td>MCC backend (Java, one jar)</td><td>8080</td><td><code>./start.sh</code></td><td>Serves the dashboard, <code>/api/**</code>, <code>/ws/telemetry</code>, Swagger. Writes <code>backend.log</code>, pid in <code>.backend.pid</code>.</td></tr>
            <tr><td>XDLINX stack: OBC, subsystems, xdos_tm_view</td><td>9100 (feed), 10000–10070 (internal)</td><td><code>./start.sh --sim</code> or Start feed</td><td>OBC boots ~30 s after start; the first master frame is warm-up.</td></tr>
            <tr><td>SQLite database</td><td>—</td><td>built by <code>schema/build_schema.py</code></td><td><code>mcc.db</code>, journal mode delete, 122 MB with a full history.</td></tr>
          </tbody>
        </table>
      </div>

      <h2>Key decisions</h2>
      <div className="doc-table">
        <table>
          <thead><tr><th>Decision</th><th>Why</th><th>Trade-off</th></tr></thead>
          <tbody>
            <tr><td>Decode from the MIB JSON, never the CSV</td><td>The CSV has no endianness and its scale column means different things per subsystem.</td><td>The schema builder must parse seven JSON files.</td></tr>
            <tr><td>One thread per feed, synchronous pipeline</td><td>Order is guaranteed and the work per subframe is under 2 ms, far inside the 1 s period.</td><td>A slow disk write would delay the next subframe.</td></tr>
            <tr><td>Refuse to start on LUT CRC mismatch</td><td>A wrong LUT reads every value from the wrong offset and still looks plausible.</td><td>An operator must rebuild the schema after a LUT change.</td></tr>
            <tr><td>Trace produced by the decoder itself</td><td>A separate "explain" copy could disagree with the value on screen.</td><td>The decoder carries optional trace hooks.</td></tr>
            <tr><td>Store the formatted value text</td><td>Fast to build the first version.</td><td>~247 B per value and a 24-minute history; replaced in the Postgres redesign.</td></tr>
            <tr><td>Dashboard served by the backend</td><td>One process, one port, same origin for REST and WebSocket.</td><td>A frontend change needs a backend rebuild to ship.</td></tr>
          </tbody>
        </table>
      </div>
    </article>
  )
}

/* ─────────────────────────────── Low-level design ────────────────────────── */

export function LldDoc() {
  const lanes = ['XmccClient', 'IngestService', 'TierAssembler', 'ParameterDecoder', 'TelemetryRepo', 'TelemetrySocket', 'Browser']
  const lx = (i: number) => 70 + i * 140
  return (
    <article className="doc">
      <h1>Low-level design</h1>
      <p className="lead">Packages, the sequence for one subframe, the state machines, the decode algorithm, threads and error handling.</p>

      <h2>Packages</h2>
      <div className="doc-table">
        <table>
          <thead><tr><th>Package</th><th>Classes</th><th>Role</th></tr></thead>
          <tbody>
            <tr><td><code>wire</code></td><td>XmccClient, XmccHeader, Subframe, Tier, TierAssembler, FrameBuffer</td><td>Socket, framing, header parsing, tier layout, block assembly, 320-subframe ring buffer.</td></tr>
            <tr><td><code>decode</code></td><td>ParameterDecoder, EntryDef, PickDef, MessageDef, ParamDef, DecodedValue, Quality, TraceService, FrameDecodeService, Trace</td><td>Value decoding, quality, trace and whole-frame decode.</td></tr>
            <tr><td><code>store</code></td><td>SchemaRepo, TelemetryRepo</td><td>Loads MIB/LUT at start; writes latest and history; prunes.</td></tr>
            <tr><td><code>api</code></td><td>TelemetryController, DecodeController, SimController, DocsController, TelemetrySocket, WebConfig, OpenApiConfig</td><td>REST, WebSocket, CORS, OpenAPI.</td></tr>
            <tr><td><code>sim</code></td><td>SimulatorService, EgseClient</td><td>Starts and stops the XDLINX stack (EgseClient is not used).</td></tr>
            <tr><td>root</td><td>MccApplication, IngestService</td><td>Boot, LUT check, per-subframe pipeline, status.</td></tr>
          </tbody>
        </table>
      </div>

      <h2>Sequence: one subframe</h2>
      <figure className="doc-fig">
        <svg viewBox="0 0 1000 420" role="img" aria-label="Sequence diagram for one subframe from the socket to the browser.">
          <defs><Arrow id="lldA" /></defs>
          {lanes.map((l, i) => (
            <g key={l}>
              <rect className="d-step" x={lx(i) - 62} y="10" width="124" height="30" rx="6" />
              <text className="d-title" x={lx(i)} y="30" textAnchor="middle" style={{ fontSize: 11.5 }}>{l}</text>
              <line className="d-line" x1={lx(i)} y1="40" x2={lx(i)} y2="410" strokeDasharray="3 4" />
            </g>
          ))}
          {[
            [0, 1, 70, 'onSubframe(sf)'],
            [1, 1, 100, 'session / warm-up / gap check'],
            [1, 2, 130, 'feed(tier, sf) × 4'],
            [2, 1, 160, 'block when all pieces present'],
            [1, 3, 190, 'decodeEntry(entry, block)'],
            [3, 1, 220, 'values + quality'],
            [1, 4, 250, 'store(session, values)'],
            [4, 4, 280, 'upsert latest · insert history'],
            [1, 5, 310, 'broadcast({type: telemetry})'],
            [5, 6, 340, 'WebSocket message'],
            [6, 6, 370, 'patch table, charts, dashboard'],
          ].map(([a, b, y, label], k) => {
            const x1 = lx(a as number)
            const x2 = lx(b as number)
            const self = a === b
            return (
              <g key={k}>
                {self ? (
                  <path className="d-line" d={`M${x1},${(y as number) - 8} h26 v16 h-22`} markerEnd="url(#lldA)" />
                ) : (
                  <line className={`d-line ${k === 9 ? 'strong' : ''}`} x1={x1} y1={y as number} x2={x2 + (x2 > x1 ? -4 : 4)} y2={y as number} markerEnd="url(#lldA)" />
                )}
                <text className="d-sub" x={self ? x1 + 32 : (x1 + x2) / 2} y={(y as number) - 5} textAnchor={self ? 'start' : 'middle'}>{label}</text>
              </g>
            )
          })}
        </svg>
        <figcaption>Everything up to the broadcast runs on the <code>xmcc-client</code> thread, in order. REST requests run on Tomcat's request threads and read the buffer and database concurrently.</figcaption>
      </figure>

      <h2>State machines</h2>
      <div className="doc-grid">
        <div className="sm">
          <b>Feed connection</b>
          <ol>
            <li><code>Disconnected</code> → connect (timeout 3 s) → <code>Connected</code></li>
            <li><code>Connected</code>: read 26-byte header, then exactly <i>length</i> bytes</li>
            <li>bad magic → slide one byte at a time until <code>XMCC</code> (stay connected)</li>
            <li>wrong version, read error, or 30 s silence → close → wait 1 s → <code>Disconnected</code></li>
          </ol>
        </div>
        <div className="sm">
          <b>Session and warm-up</b>
          <ol>
            <li>first subframe after start → new session, its master frame is <code>WARMUP</code></li>
            <li>master frame increases → same session, values <code>OK</code> after warm-up</li>
            <li>master frame decreases → OBC reboot → new session, warm-up again</li>
            <li>master frame changes → pending blocks of older frames are abandoned (<code>blockIncomplete</code>)</li>
          </ol>
        </div>
        <div className="sm">
          <b>Tier block (per tier, master frame, block index)</b>
          <ol>
            <li><code>Empty</code> → piece <i>k</i> of <i>n</i> arrives → <code>Partial</code></li>
            <li><code>Partial</code> → all <i>n</i> pieces present → <code>Complete</code> → decoded, removed</li>
            <li><code>Partial</code> → master frame moves on → <code>Abandoned</code> → reported</li>
            <li>piece index = (subframe − 1) mod n; n = 1, 2, 4, 8 for VH, H, M, L</li>
          </ol>
        </div>
        <div className="sm">
          <b>Dashboard link indicator</b>
          <ol>
            <li><code>Receiving</code>: socket open, feed connected, message in the last 3 s</li>
            <li><code>No data</code>: connected, nothing for 3 s (booting or stalled)</li>
            <li><code>Feed stopped</code>: backend up, feed not connected</li>
            <li><code>Offline</code>: backend not reachable; banner shown, page recovers by itself</li>
          </ol>
        </div>
      </div>

      <h2>Decode algorithm (one parameter)</h2>
      <pre>{`raw[]  ← copy each LUT pick: block[pick.block .. +size] → reply[pick.src .. +size]
lo, hi ← bit_offset, bit_offset + bit_length          // MIB, normalised to bits
if bytes [lo/8, hi/8) not all received      → BLOCK_INCOMPLETE (or NOT_DOWNLINKED)
if every byte in that range is 0xFF         → NO_ANSWER      // not the value 255
if message is bit-addressed (ADCS, XLINK-S):
    v ← bits lo..hi, least significant bit first, across byte boundaries
else:
    v ← bytes at offset, in the field's own endianness
by data_type:
    FLOAT (32 bit), DOUBLE (64 bit) → IEEE-754
    INT  → sign-extend at the field width
    ENUM → label from enum_value;  BOOL → any bit set
value ← v × scale     (scale is a multiplier, never a divisor)
quality ← WARMUP in the first master frame of a session, else OK`}</pre>

      <h2>Threads</h2>
      <div className="doc-table">
        <table>
          <thead><tr><th>Thread</th><th>Does</th><th>Shared state</th></tr></thead>
          <tbody>
            <tr><td><code>xmcc-client</code> (daemon)</td><td>Socket read, assembly, decode, store, broadcast, in order.</td><td>Writes the frame buffer, assembler, counters, SQLite.</td></tr>
            <tr><td>Tomcat <code>http-nio-8080-exec-*</code></td><td>REST requests, WebSocket handshake.</td><td>Reads the frame buffer and SQLite.</td></tr>
            <tr><td>Spring scheduler</td><td>History prune every 30 s.</td><td>Deletes the oldest history rows.</td></tr>
            <tr><td>Simulator process</td><td>Separate OS processes, started and stopped by SimulatorService.</td><td>None inside the JVM.</td></tr>
          </tbody>
        </table>
      </div>

      <h2>Error handling</h2>
      <div className="doc-table">
        <table>
          <thead><tr><th>Situation</th><th>Behaviour</th></tr></thead>
          <tbody>
            <tr><td>Feed not listening</td><td>Retry every 1 s; dashboard shows Feed stopped.</td></tr>
            <tr><td>Bad magic in the stream</td><td>Resynchronise byte by byte; no reconnect.</td></tr>
            <tr><td>Unknown XMCC version</td><td>Close and reconnect; nothing is decoded from that message.</td></tr>
            <tr><td>Piece of a block lost</td><td>Block abandoned, counted, <code>blockIncomplete</code> pushed; values keep their last reading.</td></tr>
            <tr><td>Subsystem did not answer</td><td>All-0xFF field → <code>NO_ANSWER</code>, listed under Needs attention.</td></tr>
            <tr><td>LUT CRC mismatch at start</td><td>Backend refuses to start and names both CRCs.</td></tr>
            <tr><td>Backend down while a page is open</td><td>Red banner, feed control disabled; data reloads when the socket reconnects (every 1.5 s).</td></tr>
            <tr><td>Stack start or stop fails</td><td>Error shown next to the feed button.</td></tr>
          </tbody>
        </table>
      </div>
    </article>
  )
}

/* ─────────────────────────────── Timing and performance ──────────────────── */

export function TimingDoc() {
  return (
    <article className="doc">
      <h1>Timing and performance</h1>
      <p className="lead">The clocks the system runs on, the deadlines that follow from them, and what was measured.</p>

      <h2>The feed's clock</h2>
      <figure className="doc-fig">
        <svg viewBox="0 0 980 190" role="img" aria-label="Timeline of one master frame: 16 subframes one second apart, VH values every second, H every 2 s, M every 4 s, L every 8 s.">
          <text className="d-label" x="20" y="22">ONE MASTER FRAME = 16 SUBFRAMES = 16 s</text>
          {Array.from({ length: 16 }, (_, i) => (
            <g key={i}>
              <line className="d-line" x1={70 + i * 56} y1="36" x2={70 + i * 56} y2="168" strokeDasharray="2 4" />
              <text className="d-sub" x={70 + i * 56} y="184" textAnchor="middle">{i}s</text>
            </g>
          ))}
          {[['VH', 1, 'var(--t-vh)'], ['H', 2, 'var(--t-h)'], ['M', 4, 'var(--t-m)'], ['L', 8, 'var(--t-l)']].map(([n, span, c], r) => (
            <g key={n as string}>
              <text className="d-title" x="20" y={60 + r * 30}>{n}</text>
              {Array.from({ length: 16 }, (_, i) =>
                (i + 1) % (span as number) === 0 ? (
                  <circle key={i} cx={70 + i * 56} cy={56 + r * 30} r="7" style={{ fill: c as string }} />
                ) : null,
              )}
            </g>
          ))}
        </svg>
        <figcaption>A dot is a moment the part's block is complete and its values are decoded and pushed. VH values are at most 1 s old, H 2 s, M 4 s, L 8 s.</figcaption>
      </figure>

      <h2>Deadlines</h2>
      <div className="doc-table">
        <table>
          <thead><tr><th>Constraint</th><th>Value</th><th>Where it comes from</th></tr></thead>
          <tbody>
            <tr><td>Subframe period</td><td>1,000 ms</td><td>LUT header (T_sf); the OBC pushes on a fixed clock</td></tr>
            <tr><td>Processing budget per subframe</td><td>&lt; 1,000 ms, target &lt; 100 ms</td><td>must finish before the next subframe; measured under 2 ms</td></tr>
            <tr><td>Master frame</td><td>16 s</td><td>16 subframes</td></tr>
            <tr><td>Oldest possible value</td><td>VH 1 s · H 2 s · M 4 s · L 8 s</td><td>block span</td></tr>
            <tr><td>Warm-up after start or reboot</td><td>one master frame, up to 16 s</td><td>IngestService</td></tr>
            <tr><td>OBC boot after Start feed</td><td>about 30 s</td><td>XDLINX stack</td></tr>
            <tr><td>Feed connect timeout</td><td>3 s</td><td>XmccClient</td></tr>
            <tr><td>Feed silence before reconnect</td><td>30 s</td><td>socket read timeout</td></tr>
            <tr><td>Feed reconnect delay</td><td>1 s</td><td><code>mcc.feed.reconnect-delay-ms</code></td></tr>
            <tr><td>Frame buffer</td><td>320 subframes = 5 min 20 s</td><td>FrameBuffer(320)</td></tr>
            <tr><td>History kept</td><td>500,000 rows ≈ 24 min (measured 24.1)</td><td><code>mcc.history.max-rows</code>, pruned every 30 s</td></tr>
            <tr><td>Master frame counter wrap</td><td>every 65,536 s = 18 h 12 min</td><td>16-bit subframe counter ÷ 16</td></tr>
          </tbody>
        </table>
      </div>

      <h2>Dashboard refresh intervals</h2>
      <div className="doc-table">
        <table>
          <thead><tr><th>What</th><th>Interval</th></tr></thead>
          <tbody>
            <tr><td>Live values (WebSocket)</td><td>as decoded: 1–4 messages per second</td></tr>
            <tr><td>Status poll</td><td>2 s</td></tr>
            <tr><td>Master-frame strip</td><td>1 s</td></tr>
            <tr><td>Charts and live trends</td><td>3 s</td></tr>
            <tr><td>Ground track history</td><td>4 s (the position itself is live)</td></tr>
            <tr><td>"Receiving" threshold</td><td>last message within 3 s</td></tr>
            <tr><td>WebSocket reconnect</td><td>1.5 s</td></tr>
            <tr><td>Stop confirmation window</td><td>6 s</td></tr>
          </tbody>
        </table>
      </div>

      <h2>Measured</h2>
      <p className="lead">Taken on 2026-09-27 on the running system (Apple M4, 755 parameters, one subframe per second). Median of 7 requests unless noted.</p>
      <div className="doc-table">
        <table>
          <thead><tr><th>Measurement</th><th className="r">Median</th><th className="r">Max</th><th>Notes</th></tr></thead>
          <tbody>
            <tr><td>GET /api/frames?limit=320</td><td className="r">1.5 ms</td><td className="r">5.2 ms</td><td>from memory</td></tr>
            <tr><td>GET /api/trace</td><td className="r">2.5 ms</td><td className="r">16.6 ms</td><td>decoded on request</td></tr>
            <tr><td>GET /api/telemetry/{'{id}'}/history (600 rows)</td><td className="r">5.5 ms</td><td className="r">56.5 ms</td><td>one chart, 10 minutes of VH</td></tr>
            <tr><td>GET /api/frame-decode</td><td className="r">10.1 ms</td><td className="r">20.4 ms</td><td>1,280 bytes, 755 parameters</td></tr>
            <tr><td>GET /api/telemetry/latest</td><td className="r">13.6 ms</td><td className="r">16.2 ms</td><td>755 rows, 273 KB JSON</td></tr>
            <tr><td>GET /api/schema</td><td className="r">18.3 ms</td><td className="r">20.6 ms</td><td>12 tables, counts included</td></tr>
            <tr><td>GET /api/status</td><td className="r">25.8 ms</td><td className="r">26.9 ms</td><td>includes a process listing</td></tr>
            <tr><td>GET /api/telemetry/{'{id}'}/history (5,000 rows)</td><td className="r">71.2 ms</td><td className="r">159.5 ms</td><td>largest request the UI makes</td></tr>
            <tr><td>Spacecraft time → stored</td><td className="r">502 ms avg</td><td className="r">3,845 ms</td><td>min 126 ms; max during the first seconds after a start</td></tr>
            <tr><td>Values stored</td><td className="r">351.3 /s</td><td className="r">—</td><td>expected 352.3 /s from the LUT</td></tr>
            <tr><td>Decode, 48 subframes</td><td className="r">77 ms</td><td className="r">—</td><td>GoldenParityTest, &lt; 2 ms per subframe</td></tr>
          </tbody>
        </table>
      </div>

      <h2>Capacity</h2>
      <pre>{`values per second  = 148 (VH) + 235/2 (H) + 322/4 (M) + 50/8 (L)   = 352.3
values per day     = 352.3 × 86,400                               = 30.4 million
feed bytes per day = 1,306 × 86,400                               = 113 MB
history kept today = 500,000 rows ÷ 352.3 /s                      = 23.7 min  (measured 24.1)`}</pre>
    </article>
  )
}

/* ─────────────────────────── Traceability, risks, glossary ───────────────── */

export function TraceabilityDoc() {
  const rows: [string, string, string, string][] = [
    ['FR-01', 'wire/XmccClient', 'reconnect loop', 'live: stop and start the stack'],
    ['FR-02', 'wire/XmccHeader', 'OutOfSync, version check', 'PipelineTest'],
    ['FR-03', 'wire/TierAssembler, Tier', 'feed, drainBefore', 'PipelineTest'],
    ['FR-04', 'decode/ParameterDecoder', 'decodeEntry, decodeOne', 'ParameterDecoderTest, GoldenParityTest'],
    ['FR-05', 'decode/Quality', 'quality rules', 'GroundStationCrossCheckTest'],
    ['FR-06', 'IngestService', 'verifyLut', 'start with a changed twin'],
    ['FR-07', 'store/TelemetryRepo', 'store, history', 'API: /latest, /history'],
    ['FR-08', 'api/TelemetrySocket', 'broadcast', 'dashboard live updates'],
    ['FR-09', 'decode/TraceService', 'trace', 'Decode trace page'],
    ['FR-10', 'frontend Charts, Chart', 'uPlot, UTC axis', 'Charts page'],
    ['FR-11', 'sim/SimulatorService, SimControl', 'start, stop, confirm', 'header feed control'],
    ['FR-12', 'frontend orbit/*', 'Globe, GroundMap, orbitMetrics', 'Ground track page'],
    ['FR-13', 'api/OpenApiConfig, springdoc', '@Operation annotations', '/v3/api-docs'],
    ['FR-14', 'api/DocsController', 'schema', 'Docs → Database schema'],
    ['NFR-01', 'decode', '—', 'GoldenParityTest: 16,908 identical'],
    ['NFR-08', 'frontend index.css tokens', 'contrast', 'computed ≥ 4.5:1 per theme'],
  ]
  return (
    <article className="doc">
      <h1>Traceability</h1>
      <p className="lead">From each requirement to the code that implements it and the test or check that proves it.</p>
      <div className="doc-table">
        <table>
          <thead><tr><th>Requirement</th><th>Component</th><th>Key code</th><th>Verified by</th></tr></thead>
          <tbody>{rows.map((r) => <tr key={r[0]}><td>{r[0]}</td><td><code>{r[1]}</code></td><td>{r[2]}</td><td>{r[3]}</td></tr>)}</tbody>
        </table>
      </div>
    </article>
  )
}

export function RisksDoc() {
  const risks: [string, string, string, string][] = [
    ['R-01', 'Master frame counter wraps every 18 h 12 min and is taken as an OBC reboot', 'A new session starts; charts restart', 'Treat 4095 → 0 as a wrap (Postgres redesign)'],
    ['R-02', 'History holds only about 24 minutes', 'Older data cannot be charted or compared', 'Postgres redesign: 48 h per value, packed segments, hourly summaries'],
    ['R-03', 'LUT CRC is checked against the twin file, not the table in flight', 'A different flown LUT would decode wrongly', 'Ask XDLINX to add the LUT CRC to XMCC v2'],
    ['R-04', 'XMCC has no frame-CRC flag; corrupted frames are forwarded', 'A bad frame decodes as valid', 'Ask XDLINX for a CRC-OK flag in XMCC v2'],
    ['R-05', 'XLINK-S display offset differs by 20 bytes from the ground station display (KD-1)', '127 parameters differ from their display', 'MCC follows the MIB; divergence registered and tested'],
    ['R-06', 'No sign-in or audit', 'Anyone on the machine can stop the feed', 'Local only today; roles in the redesign'],
    ['R-07', 'Test-pattern values in the current feed (e.g. −9.3e+61)', 'Looks wrong to a viewer', 'Shown as decoded, shortened for display'],
    ['R-08', 'SQLite single writer in the ingest thread', 'A slow write delays the next subframe', 'Measured headroom: < 2 ms decode in a 1,000 ms period'],
  ]
  return (
    <article className="doc">
      <h1>Risks and open issues</h1>
      <div className="doc-table">
        <table>
          <thead><tr><th>Id</th><th>Risk</th><th>Effect</th><th>Mitigation</th></tr></thead>
          <tbody>{risks.map((r) => <tr key={r[0]}><td>{r[0]}</td><td>{r[1]}</td><td>{r[2]}</td><td>{r[3]}</td></tr>)}</tbody>
        </table>
      </div>
    </article>
  )
}

export function GlossaryDoc() {
  const terms: [string, string][] = [
    ['XMCC v1', "The XDLINX core team's message format to the MCC: 26-byte header + payload, one per subframe, TCP 9100."],
    ['Subframe', 'One message: 1,280 payload bytes, sent every second.'],
    ['Master frame', '16 subframes (16 s). Its number restarts at 0 when the OBC reboots.'],
    ['Tier / part', 'A fixed slice of the payload: VH (Very High, every 1 s), H (High, 2 s), M (Medium, 4 s), L (Low, 8 s).'],
    ['Block', 'A tier\'s data once all its pieces (1, 2, 4 or 8 subframes) have arrived.'],
    ['LUT', 'Lookup table flown on the OBC: which subsystem reply bytes go where in each tier. The ground keeps a "twin" copy.'],
    ['Pick', 'One run of bytes copied from a subsystem reply into a tier block.'],
    ['MIB', 'Mission information base: every message and field, with offset, length, type, endianness, scale, unit, enum.'],
    ['Engineering value', 'The decoded, scaled value in real units, e.g. 3.732 V.'],
    ['Quality', 'How far a value can be trusted: OK, NO_ANSWER, BLOCK_INCOMPLETE, NOT_DOWNLINKED, WARMUP.'],
    ['Warm-up', 'The first master frame after a start or reboot; decoded but not trusted.'],
    ['Session', 'One run of the OBC, from boot to the next reboot.'],
    ['Spacecraft time', 'The time stamped by the spacecraft, copied into the XMCC header; UTC, 1 ms.'],
    ['BESTPOS', 'GPS receiver message with latitude, longitude and height.'],
    ['Ground track', 'The path on the Earth directly below the spacecraft.'],
    ['Footprint', 'The area of the Earth the spacecraft can see down to 0° elevation.'],
    ['Inclination', 'The angle of the orbit to the equator; equal to the highest latitude reached.'],
    ['PDOP', 'Position dilution of precision: how much satellite geometry weakens a GPS fix (lower is better).'],
  ]
  return (
    <article className="doc">
      <h1>Glossary</h1>
      <div className="doc-table">
        <table>
          <thead><tr><th>Term</th><th>Meaning</th></tr></thead>
          <tbody>{terms.map((t) => <tr key={t[0]}><td style={{ whiteSpace: 'nowrap' }}><b>{t[0]}</b></td><td>{t[1]}</td></tr>)}</tbody>
        </table>
      </div>
    </article>
  )
}
