import { PLATFORMS, XDLINX_LINKS } from './platforms'

/** XDLINX Space Labs: the satellite platforms and public links, from xdlinx.space. */
export function XdlinxPage() {
  return (
    <div className="dash">
      <div className="dash-inner">
        <div className="dash-head">
          <div>
            <h1>XDLINX Space Labs</h1>
            <p>
              Small-satellite platforms from Hyderabad. The XDSAT family below runs XD OS, the flight
              software whose telemetry this MCC decodes. Details from xdlinx.space.
            </p>
          </div>
        </div>

        <section className="card">
          <div className="card-head">
            <h2>XDSAT platforms</h2>
            <span className="sub">5 platforms · from 6U to 100 kg payloads</span>
            <a className="linkbtn act" href="https://xdlinx.space/platform/" target="_blank" rel="noreferrer">
              xdlinx.space/platform ↗
            </a>
          </div>
          <div className="platforms">
            {PLATFORMS.map((p) => (
              <a className="pf" key={p.id} href={p.url} target="_blank" rel="noreferrer">
                <div className="pf-img"><img src={p.image} alt={p.name} loading="lazy" /></div>
                <div className="pf-body">
                  <div className="pf-name">{p.name}<span className="ext">↗</span></div>
                  <dl>
                    <dt>Bus</dt><dd>{p.bus}</dd>
                    <dt>Payload</dt><dd>{p.payload}</dd>
                    <dt>Power storage</dt><dd>{p.power}</dd>
                    <dt>Solar</dt><dd>{p.solar}</dd>
                    <dt>Heritage</dt><dd>{p.heritage}</dd>
                  </dl>
                </div>
              </a>
            ))}
          </div>
        </section>

        <section className="card">
          <div className="card-head">
            <h2>Common to every platform</h2>
            <span className="sub">as published on xdlinx.space</span>
          </div>
          <div className="doc-grid">
            <dl>
              <dt>Bus OS</dt><dd>XD OS: distributed compute for bus, payload edge and SDR</dd>
              <dt>Payloads</dt><dd>EO optical, SAR, SIGINT, IoT, laser</dd>
              <dt>Pointing</dt><dd>±0.002° (1σ), 3 axes, 2 star trackers</dd>
            </dl>
            <dl>
              <dt>TT&amp;C</dt><dd>S-band SDR and UHF, secure and flexible, with software update</dd>
              <dt>Downlink</dt><dd>X-band up to 500 Mbps</dd>
              <dt>Ground</dt><dd>Public or private ground station</dd>
            </dl>
          </div>
        </section>

        <section className="card">
          <div className="card-head"><h2>Links</h2></div>
          <div className="xd-links" style={{ marginTop: 0 }}>
            {XDLINX_LINKS.map((l) => (
              <a key={l.url} href={l.url} target="_blank" rel="noreferrer">{l.label}</a>
            ))}
          </div>
        </section>
      </div>
    </div>
  )
}
