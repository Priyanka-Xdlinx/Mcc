# MCC app — XMCC telemetry decoder

Ingests the `XMCC` telemetry feed of the XDLINX core team's test stack (`xdos/`) on **port 9100**, decodes every
parameter into engineering units, stores it in SQLite and serves a live
dashboard. The simulator can be started and stopped from the browser.

```
React + Vite ──HTTP/WS──> Spring Boot ──TCP 9100──> xdos_tm_view ──> OBC ──> 6 bus sims
                              │
                           SQLite (schema + telemetry)
```

## Requirements

Everything the app needs is in this repository. Install these tools once:

| Tool | Version | Used for |
|---|---|---|
| JDK | 17 or later | backend |
| Maven | 3.9 | backend build |
| Node.js | 22 | web UI build |
| Python 3.10+ | standard library | `mcc.py` launcher, builds `mcc.db`, runs the recorded telemetry feed |

Only for the live test stack (start.sh, macOS / Linux): a C++ toolchain with
CMake (Xcode Command Line Tools on macOS, GCC + CMake on Linux).

## First run after cloning

There are two ways to run it; they differ only in where the telemetry comes from.

**Developers (Windows, macOS or Linux) - recorded feed, no C++ build.**
`mcc.py` needs Python 3.10+. Its feed is always the recording: real XMCC from
the test stack (`testdata/feed_capture.xmcc`) served by `feed/xmcc_replay.py`
on port 9100, decoded exactly like the live feed.

```bash
python mcc.py start --build --sim     # first time: build everything, start the app and the feed
python mcc.py start --sim             # afterwards
python mcc.py status
python mcc.py stop
```

**Live XDLINX test stack (macOS / Linux) - `start.sh` / `stop.sh`.**
Build the C++ stack once, then Start feed runs it:

```bash
xdos/scripts/build_mac.sh                                    # macOS
cmake -S xdos -B xdos/build && cmake --build xdos/build -j   # Linux
./start.sh --build --sim
```

`mcc.db`, `frontend/dist` and the jar are generated, never committed.

## Run it

```bash
./start.sh --sim     # build anything missing, start the backend and the simulator
./stop.sh            # stop both
```

Then open <http://localhost:8080/>. Telemetry appears once the OBC boots
(~10 s after the EPS kill switch is released); the first master frame is warm-up
and is marked as such.

| Command | What it does |
|---|---|
| `./start.sh` | Backend only — press **Start feed** in the UI |
| `./start.sh --sim` | Also starts the XDOS simulator stack |
| `./start.sh --build` | Force a rebuild of schema, frontend and backend |
| `./start.sh --fg` | Backend in the foreground, Ctrl-C to stop |
| `./stop.sh` | Stop the simulator and the backend |
| `./stop.sh --sim` | Stop only the simulator, leave the backend up |

`start.sh` builds whatever is missing, so a clean checkout needs nothing else.
It waits on the health endpoint rather than sleeping a fixed amount, and prints
the last 20 log lines if the backend dies during startup. `stop.sh` is safe to
run when nothing is up, and verifies afterwards that nothing survived.

Backend log: `backend.log`. Backend pid: `.backend.pid`.

For frontend development, `npm run dev` in `frontend/` serves on :5173 and
proxies `/api` and `/ws` to the backend.

<details>
<summary>Manual equivalent, if you would rather not use the scripts</summary>

```bash
cd schema   && python3 build_schema.py
cd ../frontend && npm install && npm run build
rm -rf ../backend/src/main/resources/static
mkdir -p ../backend/src/main/resources/static
cp -r dist/* ../backend/src/main/resources/static/
cd ../backend && mvn -DskipTests package
MCC_DB=../mcc.db MCC_XDOS_HOME=../xdos \
  java -jar target/mcc-backend-1.0.0.jar
```
</details>

## The views

Pages: **Dashboard**, **Telemetry**, **Charts**, **Decode trace**, **Docs** and **XDLINX**.
Each has its own address (`/#charts`, `/#docs/database`, …); `?theme=light|dark|auto`
sets the theme from the address. The look (palette, IBM Plex type, header, uPlot
charts) is shared with the Orblinx platform.

**Docs** — how the system is built, the database schema read live from `mcc.db`
(`GET /api/schema`), the REST API in Swagger UI, and the WebSocket messages.
Swagger UI is also at <http://localhost:8080/swagger-ui.html>, and the OpenAPI
description at <http://localhost:8080/v3/api-docs>.

**XDLINX** — the XDSAT platforms and links from xdlinx.space (images stored locally).

**Charts** — uPlot charts on a UTC time axis; drag to zoom, +/− buttons, reset.
Windows: last 5 min, last 15 min, or all stored history.

The app opens on the **Dashboard**. A theme switch in the header offers
**Auto** (follows the OS), **Light** and **Dark**; the choice is remembered in the
browser.

**Dashboard** — link state, spacecraft time, master frame, how many parameters
are OK, lost subframes; the current master frame as a 16-subframe strip showing
where each part (VH/H/M/L) completes; health per subsystem (click one to open
its telemetry); parameters that are changing, with sparklines; parameters that
need attention; and live trend charts of the changing parameters.
When the feed stops, figures are labelled *last known* instead of looking live.


**Telemetry** — every parameter the LUT downlinks, updating over the websocket:
value, unit, tier, quality, and the frame it came from. Filter by subsystem, tier
or text; `∿` marks a parameter that is not byte aligned.

**Charts** — pick up to six parameters and plot them. Series of different units
are drawn as separate charts rather than sharing a second y-axis, so two
unrelated shapes are never made to look comparable. Colours are assigned in a
fixed order from a palette validated for colour-vision deficiency and for ≥3:1
contrast against this surface, and a series keeps its colour when another is
removed.

**Decode trace** — the whole chain for one parameter on one subframe, in three
lanes: Layer 1 (framing, tier split, block assembly, the LUT), Layer 2 (the MIB:
offsets, type, endianness, scale, enums) and the result. Every step shows the
actual bytes, with the field highlighted in a hex dump.

- **Live** follows the newest frame; **Prev / Next** step one subframe at a time.
- **Replay** plays the buffered frames forward at 0.5×–4× so the decode can be
  watched rather than inferred.
- The timeline strip selects any of the last 320 subframes (20 master frames).

The trace is produced by the decoder on its ordinary code path, not by a separate
"explain" implementation — a trace generated by a parallel copy of the arithmetic
could disagree with the value on the dashboard and nobody would notice.

## Layout

| Path | What it is |
|---|---|
| `schema/build_schema.py` | Builds `mcc.db` from `mib/*.json` **and never from the CSV** — see below |
| `schema/schema.sql` | The DDL |
| `backend/…/wire/` | Layer 1: XMCC framing, tier reassembly, the LUT twin |
| `backend/…/decode/` | Layer 2: the parameter decoder |
| `backend/…/sim/` | Starts and stops the simulator |
| `frontend/` | The dashboard |
| `testdata/` | A 48-subframe capture and the reference decode it is checked against |

## Two decode layers

**Layer 1 — resolution.** The wire carries no names and no identifiers, only
bytes at positions. `config/rate_group_lut_bin_gs.json` says which byte runs
belong to which message. Its **CRC-16 must match the table the OBC is flying**;
the backend refuses to start otherwise, because a mismatched twin reads every
value from the wrong offset and still looks entirely plausible.

**Layer 2 — parameter decode.** The MIB says what those bytes mean. Three rules
matter more than the rest:

- ADCS and XLINK-S are bit-addressed, packed **LSB-first across the whole
  payload**. 183 of the 755 downlinked parameters are not byte-aligned and some
  span a byte boundary.
- `scale` is a **multiplier** on decode.
- All-`0xFF` across a field means *the subsystem did not answer* — not 255 — and
  it is decided per parameter.

## Why the schema is generated from the JSON

The previously published `mib/telemetry_decode_contract.html` was generated from
`mission_parameter_dictionary.csv`. That CSV has **no `endian` column**, and its
`scale` column is a divisor for ADCS but a multiplier for EPS/XLINK-S. Both
limitations are baked into anything built from it. The JSON MIB has neither
problem, so the generator reads that instead.

## Tests

```bash
cd backend && mvn test
```

| Level | What it proves |
|---|---|
| Unit | Golden vectors from real captured bytes — LSB-first packing, scale direction, IEEE-754 order, sign extension, cross-byte bit fields |
| Component | Tier spans, absolute pick offsets, gap reporting, header rejection |
| **L3a** | Every reading identical to `tools/mcc_decode.py --compat mib` over the same capture. **Zero tolerance.** |
| **L3b** | Compared against the ground station's own log of the same run; every difference must be a registered divergence |

The registered divergences are places where the **ground station's display is
wrong** and copying it would mean importing a defect:

| | Divergence |
|---|---|
| KD-1 | XLINK 20-byte prefix double-shift (`tm_view_main.cpp:136`) — 127 parameters |
| KD-2 | `0xFF` judged per entry rather than per parameter — renders as `-1` |
| KD-3 | Cosmetic operator flag labels (`9` → `9 (set)`); the number is identical |

## Notes

- Stopping uses `run_stack.sh`'s own kill pattern, **not `scripts/kill_all.sh`**,
  which misses `xdos_tm_view` and `xdos_sensor_sim` while reporting success.
- The dashboard will look mostly frozen. That is the simulator, not the decoder —
  only 8 of 755 parameters ever change value. See
  `mib/TELEMETRY_OBSERVATIONS.md`.
