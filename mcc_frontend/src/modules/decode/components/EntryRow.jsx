/**
 * One LUT entry of a tier: its picks, the reconstructed subsystem reply and
 * its parameters. Clicking a parameter opens its trace.
 */
export default function EntryRow({ entry: e, open, onToggle, onlyDecoded, onTrace }) {
  const ok = e.parameters.filter((p) => p.quality === "OK").length;
  const params = onlyDecoded ? e.parameters.filter((p) => p.quality === "OK") : e.parameters;

  return (
    <div className={`entry ${open ? "open" : ""}`}>
      <button className="entry-head" onClick={onToggle} aria-expanded={open}>
        <span className="caret">{open ? "▾" : "▸"}</span>
        <span className="e-id">{e.entry}</span>
        <span className="e-sub">{e.subsystem}</span>
        <span className="e-msg" title={e.message}>
          {e.message}
        </span>
        <span className="e-bytes">
          {e.picks.reduce((n, p) => n + p.size, 0)}/{e.tmRawLen} B
        </span>
        <span className="e-count">
          {ok}/{e.parameters.length}
        </span>
      </button>

      {open && (
        <div className="entry-body">
          <div className="picks">
            {e.picks.map((p, i) => (
              <span className="pick-chip" key={i}>
                block[{p.block}:{p.block + p.size}] → reply[{p.src}:{p.src + p.size}]
                {p.payloadFrom !== null && (
                  <em>
                    {" "}
                    · in this subframe at {p.payloadFrom}–{p.payloadTo}
                  </em>
                )}
              </span>
            ))}
            {e.bitAddressed && <span className="pick-chip bit">bit-addressed</span>}
          </div>

          {e.replyHex && (
            <div className="replydump">
              <div className="hex-title">
                reconstructed {e.message} reply — {e.tmRawLen} bytes, {e.replyHave.reduce((n, x) => n + x, 0)}{" "}
                downlinked <span className="dimcol">(faint = never sent)</span>
              </div>
              <div className="hex-body">
                {Array.from({ length: Math.ceil(e.replyHex.length / 16) }, (_, ri) => (
                  <div className="hex-row" key={ri}>
                    <span className="hex-off">{String(ri * 16).padStart(5, " ")}</span>
                    {e.replyHex.slice(ri * 16, ri * 16 + 16).map((b, ci) => (
                      <span key={ci} className={e.replyHave[ri * 16 + ci] ? "hex-b" : "hex-b absent"}>
                        {b}
                      </span>
                    ))}
                  </div>
                ))}
              </div>
            </div>
          )}

          <table className="ptable">
            <thead>
              <tr>
                <th>#</th>
                <th>Parameter</th>
                <th>Bytes</th>
                <th>Bits</th>
                <th>Type</th>
                <th>Binary</th>
                <th style={{ textAlign: "right" }}>Value</th>
                <th>Quality</th>
                <th />
              </tr>
            </thead>
            <tbody>
              {params.map((p, i) => {
                const traceable = p.parameterId != null;
                return (
                  <tr
                    key={i}
                    className={traceable ? "traceable" : ""}
                    title={traceable ? "Trace this parameter byte by byte" : undefined}
                    onClick={() => traceable && onTrace(p.parameterId)}
                  >
                    <td className="num dimcol">{p.seq ?? "—"}</td>
                    <td className="p-name">
                      {p.name}
                      {p.byteAligned === false && (
                        <span className="nba" title="not byte aligned">
                          {" "}
                          ∿
                        </span>
                      )}
                      {p.unit ? <span className="p-unit"> {p.unit}</span> : null}
                      {p.uid && <div className="p-uid">{p.uid}</div>}
                    </td>
                    <td className="num dimcol">{p.byteFrom !== undefined ? `${p.byteFrom}–${p.byteTo}` : "—"}</td>
                    <td className="num dimcol">{p.bitOffset !== undefined ? `${p.bitOffset}+${p.bitLength}` : "—"}</td>
                    <td className="dimcol">{p.dataType ?? "—"}</td>
                    <td className="raw bits">{p.rawBits ?? "—"}</td>
                    <td className="num">{p.value ?? "—"}</td>
                    <td className={`q ${p.quality ?? ""}`}>{p.quality ?? "—"}</td>
                    <td className="tracecell">
                      <span className="trace-btn">trace</span>
                    </td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      )}
    </div>
  );
}
