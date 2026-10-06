const MESSAGES = [
  ["status", "Once, on connect", <><code>status</code>: the same object as <code>feed</code> in <code>GET /api/status</code></>],
  [
    "telemetry",
    "Each time a part's block completes",
    <>
      <code>tier</code>, <code>masterFrame</code>, <code>subframe</code>, <code>spacecraftTime</code>, <code>updates[]</code> with{" "}
      <code>parameterId</code>, <code>entry</code>, <code>tier</code>, <code>name</code>, <code>value</code> (decoded text),{" "}
      <code>quality</code>
    </>,
  ],
  [
    "blockIncomplete",
    "A block was abandoned because a piece never arrived",
    <>
      <code>tier</code>, <code>masterFrame</code>, <code>blockIndex</code>, <code>slicesReceived</code>, <code>slicesExpected</code>
    </>,
  ],
];

const QUALITIES = [
  ["OK", "Decoded normally."],
  ["NO_ANSWER", "Every byte of the field was 0xFF: the subsystem did not reply to the OBC's poll."],
  ["BLOCK_INCOMPLETE", "A piece of the H, M or L block never arrived, so the field cannot be read."],
  ["NOT_DOWNLINKED", "No LUT pick covers the field; the bytes are fill, not data."],
  ["WARMUP", "First master frame after the backend starts or the OBC reboots; values are decoded but not trusted."],
];

const EXAMPLE = `{
  "type": "telemetry",
  "tier": "VH",
  "masterFrame": 48,
  "subframe": 8,
  "spacecraftTime": "2026-09-25 13:08:20.737",
  "updates": [
    { "parameterId": 1980, "entry": "EP-016", "tier": "VH",
      "name": "Conv1 OP Voltage Reading", "value": "3.732 V", "quality": "OK" }
  ]
}`;

/** The WebSocket messages, which OpenAPI cannot describe. */
export default function StreamDoc() {
  return (
    <article className="doc">
      <h1>Live stream</h1>
      <p className="lead">
        The dashboard receives every decoded value over a WebSocket at <code>/ws/telemetry</code> on the same port as the
        API. It is one-way: the backend sends, the page listens.
      </p>

      <h2>Connecting</h2>
      <pre>{`const ws = new WebSocket('ws://localhost:8080/ws/telemetry')
ws.onmessage = (e) => console.log(JSON.parse(e.data))`}</pre>
      <p>
        On connect the backend sends one <code>status</code> message, then one <code>telemetry</code> message for each part
        (VH, H, M, L) that completes: 1 to 4 messages per second. A client that disconnects should reconnect and fetch{" "}
        <code>/api/telemetry/latest</code> again, as this dashboard does.
      </p>

      <h2>Messages</h2>
      <div className="doc-table">
        <table>
          <thead>
            <tr>
              <th>type</th>
              <th>When</th>
              <th>Fields</th>
            </tr>
          </thead>
          <tbody>
            {MESSAGES.map(([type, when, fields]) => (
              <tr key={type}>
                <td>
                  <code>{type}</code>
                </td>
                <td>{when}</td>
                <td>{fields}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      <h2>Example: telemetry</h2>
      <pre>{EXAMPLE}</pre>

      <h2>Quality values</h2>
      <div className="doc-table">
        <table>
          <thead>
            <tr>
              <th>quality</th>
              <th>Meaning</th>
            </tr>
          </thead>
          <tbody>
            {QUALITIES.map(([q, meaning]) => (
              <tr key={q}>
                <td>
                  <code>{q}</code>
                </td>
                <td>{meaning}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </article>
  );
}
