# XDOS Binary Rate-Group LUT — Design (Master Frame rev. 2026-08-03)

Status: **frame layout FINAL** (per Jayaram, 2026-08-03) and **IMPLEMENTED**
end-to-end (per Jayaram, 2026-08-27): CSV → binary build, OBC execution,
tiered tm_view decode.  §4 records the decisions that closed the previously
pending items.

## 1. Subframe layout (FINAL)

Subframe size is fixed at **1280 bytes** (the TM DATA field of the FCA1
downlink frame).  The 1362-byte variant is **ruled out**.

| Field      | Offset      | Size  |
|------------|-------------|-------|
| Very High  | 0    – 399  | 400 B |
| High       | 400  – 799  | 400 B |
| Medium     | 800  – 1099 | 300 B |
| Low        | 1100 – 1274 | 175 B |
| Reserved   | 1275 – 1279 | **5 B** (future use, transmitted as zero) |

Every subframe carries all four segments at these fixed offsets.

## 2. Master frame structure (FINAL)

16 subframes per master frame.  Each tier refreshes in **blocks**; a block
holds *unique* data spread evenly across the subframes it spans:

| Tier | Block span | Unique data per block | Blocks per MF | Unique per MF |
|------|-----------:|----------------------:|--------------:|--------------:|
| VH   | 1 subframe | 400 B                 | 16            | 6,400 B |
| H    | 2          | 800 B (400+400)       | 8             | 6,400 B |
| M    | 4          | 1,200 B (300×4)       | 4             | 4,800 B |
| L    | 8          | 1,400 B (175×8)       | 2             | 2,800 B |

Total unique telemetry per master frame: **20,400 B** (+ 16×5 B reserved).
Block boundaries are subframe-aligned: H blocks start at subframes 1,3,5,…;
M at 1,5,9,13; L at 1 and 9.

Fill policy (FINAL): a tier's unfilled block bytes are zero-padded; a
classification whose picked data exceeds its block budget is **rejected at
LUT-generation time** (never handled at runtime).

Per-command budget with the current 78-command classification
(VH 27 / H 18 / M 23 / L 10 commands): VH ≈ 14.8 B, H ≈ 44.4 B,
M ≈ 52.2 B, L = 140 B of picked data per command — VH is the tight tier;
the generator's overflow check will bite there first.

## 3. Binary LUT file (structure carried forward, updated to this frame)

One binary file, four segments (VH/H/M/L), 50-byte header:
magic `"XLUT"`, version, flags, **subframe_size = 1280 (fixed)**, the
partition table `400/400/300/175/5`, segment table at offset 16,
**CRC-16-CCITT signature** at offset 48 (shared GS↔OBC; position in file is
the sole OBC↔GS entry link — entries carry no command IDs).

Entry: `SYNC + APID + TC payload + TM length + picks`.  APIDs are the
hardware values (ADCS 1, EPS 3, GPS 4, Payload 5, XLINK-S 6).
Pick triplet: `(SourceOffset, Size, BlockOffset)` — 2 B each, byte units,
**block-relative** (tier implicit from the segment; the old global
SubframeOffset is gone).  The GS JSON twin retains IDs and response names
for decoding; the OBC file stays Database-free.

## 4. DECIDED (Jayaram, 2026-08-27) — implemented

* **Subframe period (T_sf) = 1000 ms** — one subframe per second, master
  frame every 16 s.  Written into the LUT header (offset 18); a legacy 0
  in an old file is read as 1000.
* **Cadence: tier-scheduled, 16-position schedule** — computed by the OBC
  at LUT load: tier-t entry i is polled in the slots where
  `slot % span == i % span` (VH every subframe, H per 2, M per 4, L per 8).
  Polls are spread evenly across each 1 s window (50 ms link-pacing floor);
  a window that overruns finishes late but never skips a slot, so block
  phase is kept.  A failed poll writes 0xFF over the entry's pick ranges.
* **Execution source: the binary LUT only** — the OBC boots with NO table
  and transmits NOTHING until a binary LUT is uplinked (routing port + 3);
  an accepted uplink is persisted to `obc_lut_store.bin` and auto-reloaded
  on restart.  The legacy JSON LUT path is retired (uplink replies ERR).
* **Authoring: one CSV master list** (`config/rate_group_commands.csv`,
  columns `tier,tc_id,subsystem,tc_name,picks`) compiled by `xdos_lut_csv`
  or imported in the xdos_gs `[g]` composer (`[i]`).  See
  `src/lut/rate_group_csv.hpp` for the grammar.
* **Slot alignment on the wire** — the TM header's per-APID sub-frame count
  increments once per push; `SFC % 16` is the subframe's slot and `SFC / 16`
  its master-frame id.  xdos_tm_view positions subframes by these (a lost
  frame leaves a visible gap instead of shifting the alignment).

## 5. Display path

Master-frame telemetry is received and displayed by the standalone
**xdos_tm_view** terminal (OBC rate-group port = routing port + 2); the
telecommanding CLI (xdos_gs) carries no rate-group views.  The [u] LUT
uplink flow remains in xdos_gs (it is telecommanding).
