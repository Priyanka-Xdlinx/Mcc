# Binary Rate-Group LUT — Format & How-To

*XDOS ground station · `rate_group_lut.bin` · format version 1*

One binary file tells the OBC three things: **which telecommands to poll**,
**which bytes of each response to keep** (the picks), and **where those bytes
go** in the downlink block. The OBC never sees names or IDs — it replays
opaque TC bytes and copies byte ranges. All meaning stays on the ground, in
the Database and the JSON twin.

---

## 1. Making a binary

### 1a. From the CSV master list (the easy way for n commands)

Maintain ONE file, `config/rate_group_commands.csv`, listing every
rate-group command:

```csv
tier,tc_id,subsystem,tc_name,picks
VH,AD-150,ADCS,Control and estimation mode,
H,EP-016,EPS,Get_PDU_Voltage_Readings,"Batt Voltage Reading; bytes 0:4"
H,GPS-42,GPS,LOG,
L,XL-008,XLINK,HOUSE_KEEPING,
```

* `tier` VH/H/M/L — row order within a tier IS the entry order (the OBC↔GS
  link), so reordering rows changes the layout.
* `tc_id` — your label; for GPS the trailing number is the log id (GPS-42
  → BESTPOS).
* `tc_name` — EPS/XLINK: the TC name (response derived as `<name>_RSP`);
  ADCS: the TM name or numeric id (the poll IS the TM request); GPS: `LOG`.
* `picks` — semicolon-separated parameter names, 1-based parameter numbers,
  or `bytes OFF:SIZE`; empty = full payload.  Quote the cell if it contains
  commas.

Compile it (from the repo root):

```
$ ./build/xdos_lut_csv config/rate_group_commands.csv
xdos_lut_csv: XLINK key <prefix>... (SN <serial>) from <key file>
  VH   3 cmds     15 /  400 B
  H    3 cmds    144 /  800 B
  M    5 cmds    242 / 1200 B
  L    1 cmds     12 / 1400 B
xdos_lut_csv: wrote config/rate_group_lut.bin (508 B, CRC-16 0x5B22) and config/rate_group_lut_bin_gs.json
```

(The figures above are the table currently checked in — 12 entries, 508 B,
CRC 0x5B22. The key line reports the XLINK product key baked into any XLINK
entries; see §4.)

Errors carry CSV row numbers and the build rejects any tier over budget.
Parsing does not stop at the first bad row, so one run surfaces every
problem. Then uplink the `.bin` from `xdos_gs` (`g` → `s`).

### 1b. Interactively with the `xdos_gs` composer

Launch `xdos_gs` **from the repo root** (so `config/` is visible) and press
`g`. If a previous LUT exists you are offered `[r]esume` (edit it) or `[n]ew`
(start empty — the OBC also starts empty until the first uplink).

Composer actions:

```
[a]dd   [i]mport CSV   [p]icks   [t]ier   [d]elete   [l]ist   [b]uild   [s]end .bin to OBC   [q] back
```

`[i]mport CSV` loads `config/rate_group_commands.csv` (or any path) and
replaces the table — use the prompts only for touch-ups.

Worked example — poll ADCS TM 128, priority VH, keep only bytes 4–5:

```
> a                      add a command
> ADCS                   subsystem
> 128                    the TM to poll (ADCS polls ARE TM requests)
> VH                     priority tier
  Response Identification — 8 B raw payload
   #6  off=4 size=2  Runtime (seconds)
> 6                      the pick: parameter #6 = bytes 4–5
> b                      build → writes .bin + .json twin
> s                      uplink the .bin to the OBC
  OBC: OK bin 1 entries crc 0x5B22 applied
```

**Picks prompt syntax** (one line, comma-separated, mixable):

| You type            | Meaning                                                 |
|---------------------|---------------------------------------------------------|
| `6` or `3,9,12`     | pick by parameter number(s) from the displayed map      |
| `Runtime (seconds)` | pick by exact parameter name                            |
| `bytes 4:2`         | raw range — **OFFSET : SIZE** (bytes 4–5, *not* 4-to-5!)|
| *Enter* (empty)     | one pick covering the full raw payload                  |

Multiple picks per TM are normal. Selections are sorted and **adjacent or
overlapping ranges merge into one pick** automatically (e.g. picking eight
1-bit flags packed in one byte costs exactly 1 byte). Picks address the
**raw payload** — Database byte space, all native framing excluded.

`[b]uild` always writes both artifacts:

* `config/rate_group_lut.bin` — the OBC binary. **The only thing ever uplinked.**
* `config/rate_group_lut_bin_gs.json` — ground-only twin: names, IDs and pick
  labels for decoding, and the resume state for later edits. Never transmitted.

`[s]end` ships the `.bin` over the backdoor Ethernet to the OBC's LUT
listener: **TCP `obc_port + 3`** (10003 sim, **7003** bench), framed as
`"XLUT" + u32 big-endian length + file bytes`. The OBC independently checks
magic, length field, CRC-16 and every entry's structure, **hot-swaps the
running engine to the new table**, persists it as `obc_lut_store.bin`
(auto-reloaded on restart), and replies
`OK bin <N> entries crc 0x<CRC> applied` (or `ERR …`).

**The OBC executes ONLY the binary LUT.** It boots empty — no polls, no
subframes, no master frame — until a `.bin` is uplinked or a previously
stored one passes CRC at boot.  JSON tables are no longer accepted
(`ERR json LUT retired`).

---

## 2. File format — every field

All multi-byte values are **little-endian**. Layout: one 50-byte header,
then four segments (VH, H, M, L) of entries packed back-to-back.

### 2.1 Header (50 bytes)

| Off | Size | Field                 | Meaning |
|----:|-----:|-----------------------|---------|
| 0   | 4    | magic `"XLUT"`        | file identifier |
| 4   | 1    | version = 1           | format revision |
| 5   | 1    | flags = 0             | reserved |
| 6   | 2    | subframe_size = 1280  | downlink subframe, fixed (1362 ruled out) |
| 8   | 2    | seg VH = 400          | per-subframe segment widths — |
| 10  | 2    | seg H = 400           | the partition every subframe follows: |
| 12  | 2    | seg M = 300           | `VH[0:400] H[400:800] M[800:1100]` |
| 14  | 2    | seg L = 175           | `L[1100:1275]` |
| 16  | 1    | reserved = 5          | bytes 1275–1279, future use, sent as zero |
| 17  | 1    | pad = 0               | alignment |
| 18  | 2    | T_sf = 1000           | subframe period in ms (legacy files wrote 0 — read as 1000) |
| 20  | 8    | entry counts × 4      | u16 each: how many entries in VH, H, M, L |
| 28  | 16   | segment offsets × 4   | u32 each: absolute file offset of each segment — direct seek, no walking |
| 44  | 4    | file length           | total bytes; integrity check with the CRC |
| 48  | 2    | CRC-16-CCITT          | poly 0x1021, init 0xFFFF, computed over the **whole file with this field zeroed** |

The CRC is the shared **signature**: since entries carry no IDs, *position
within a segment* is the only link between what the OBC transmits and what
the ground decodes — the CRC guarantees both sides hold the identical table.

### 2.2 Entry (variable length, repeated per segment)

| Field       | Size | Meaning |
|-------------|-----:|---------|
| sync        | 2    | `0xA55A` — entry boundary marker |
| apid        | 1    | hardware APID: ADCS 1 · UHF 2 · EPS 3 · GPS 4 · Payload 5 · XLINK-S 6 |
| flags       | 1    | reserved = 0 |
| tc_len      | 2    | length of the TC bytes that follow |
| tc bytes    | var  | the **complete native frame**, replayed verbatim on the bus (ADCS: TM-request `1F 7F <id> 1F FF`; EPS: base64 telegram; GPS: LOG command; XLINK: key+event frame) |
| tm_raw_len  | 2    | raw response payload length per the Database |
| pick_count  | 1    | number of pick triplets |
| picks       | 6×n  | see below |

### 2.3 Pick triplet (6 bytes each)

| Field | Size | Meaning |
|-------|-----:|---------|
| src   | 2    | byte offset **in the raw response payload** |
| size  | 2    | bytes to copy |
| block | 2    | destination offset **inside the tier's block** (generator-assigned, packed back-to-back) |

The OBC's entire runtime job per pick is
`memcpy(block + block_off, payload + src, size)` — zero interpretation.

### 2.4 Block budgets (closed at build; re-checked at uplink)

| Tier | Block span   | Budget (unique bytes/block) |
|------|--------------|-----------------------------|
| VH   | 1 subframe   | 400  |
| H    | 2 subframes  | 800  (400 + 400) |
| M    | 4 subframes  | 1200 (300 × 4) |
| L    | 8 subframes  | 1400 (175 × 8) |

A tier whose picked bytes exceed its budget is **rejected at build time**
with the tier and last entry named — overflow is never handled at runtime.
The OBC re-checks every pick against the same budgets when it parses an
uplink (§5) and refuses the whole table if one is out of range. Unfilled
block bytes are zero-padded. Master frame = 16 subframes = 20,400 B of
unique telemetry.

---

## 3. Annotated example entries

Both are taken byte for byte out of the `config/rate_group_lut.bin` in the
tree (12 entries, 508 B, CRC 0x5B22) — decode your own with
`python3 -c` or the twin.

**VH entry 1** — ADCS TM 150 *Control and estimation mode*, no `picks` cell,
so one pick covers the whole payload (20 B entry at file offset 0x0032):

```
5A A5             sync 0xA55A (0x5A,0xA5 on disk — little-endian)
01                apid 1 = ADCS
00                flags
05 00             tc_len = 5
1F 7F 96 1F FF    TC bytes: TM-request for id 0x96 (150)
05 00             tm_raw_len = 5
01                pick_count = 1
00 00 05 00 00 00   pick: src 0, size 5, block 0   (<full payload>)
```

**H entry 2** — EPS `Get_PDU_Voltage_Readings`, CSV picks
`"Batt Voltage Reading; bytes 0:4"` (31 B entry at file offset 0x008C):

```
5A A5                            sync
03 00                            apid 3 = EPS · flags 0
0A 00                            tc_len = 10
00 41 63 6B 51 53 77 3D 3D 01    TC bytes: the EPS telegram, replayed verbatim
14 00                            tm_raw_len = 20
02                               pick_count = 2   ← multiple picks per TM
00 00 04 00 40 00                  pick 1: src  0, size 4, block 64  (<bytes>)
10 00 04 00 44 00                  pick 2: src 16, size 4, block 68
                                           (Batt Voltage Reading)
```

Only 8 of the 20 response bytes are downlinked. The two picks stay separate
because bytes 4–15 sit between them; had they been adjacent they would have
merged into one 8-byte pick. Note that `EP-016` and `Batt Voltage Reading`
appear nowhere in the binary — they live only in the twin.

---

## 4. Rules the build enforces

Unknown TC/TM or parameter names fail loudly (picks are stored by *name* in
the twin, so a Database revision recomputes offsets instead of silently
shifting bytes). Picks must lie inside `tm_raw_len`. Budgets must close.

**XLINK entries carry the product key.** `xdos_lut_csv` loads it from the
`mib/xlink_keys/` catalog and bakes it into the XLINK TC bytes, which the OBC
replays unchanged. The builder's default key is all-zero — accepted by the
simulator only — so a bench or flight build must report a real key on the
first output line.

**The build is deterministic**: identical source + Database → byte-identical
file → same CRC, so the signature is quotable in reviews. Verify it any time:

```
$ cp config/rate_group_lut.bin /tmp/before.bin
$ ./build/xdos_lut_csv config/rate_group_commands.csv
$ cmp /tmp/before.bin config/rate_group_lut.bin && echo identical
```

---

## 5. What the OBC rejects

`obc/binary_rate_lut.cpp` re-derives every structural property before the
engine will touch the table; the reason string is what comes back on the
uplink socket, prefixed `ERR `. Nothing is applied partially — the parser
fills a fresh object and the engine swaps only on complete success, so a
rejected uplink leaves the running table untouched.

| Check | Reply |
|-------|-------|
| first 4 bytes are `XLUT` | `bin bad magic` |
| version == 1 | `bin unsupported version <n>` |
| subframe_size == 1280 | `bin subframe size mismatch` |
| each segment width matches the compiled partition | `bin segment width mismatch (<tier>)` |
| reserved_bytes == 5 | `bin reserved-bytes mismatch` |
| file_length field == bytes received | `bin length field mismatch` |
| CRC over the image with bytes 48–49 zeroed | `bin CRC mismatch` |
| first segment offset == 50, offsets non-decreasing, last inside the file | `bin first segment offset mismatch` · `bin segment offsets not increasing` · `bin segment offset beyond file` |
| sync 0xA55A where the walk expects it | `bin bad sync at <tier> entry <i>` |
| APID is a known subsystem | `bin unknown APID <n> at …` |
| tc_len non-zero and inside the segment | `bin bad TC length at …` |
| pick_count ≥ 1 | `bin entry with no picks at …` |
| pick size non-zero | `bin zero-size pick at …` |
| src + size ≤ tm_raw_len | `bin pick outside raw payload at …` |
| block + size ≤ the tier budget | `bin pick outside block budget at …` |
| the walk ends exactly on the segment boundary | `bin trailing bytes in segment <tier>` |

Note the redundancy: subframe size, segment widths and the reserved count are
already compiled into both sides. They are in the file so that a table built
against a *different* frame definition is refused at uplink rather than
quietly filling the wrong bytes.

---

## 6. Runtime behaviour worth knowing

* **Boot is silent.** No table → no polls and no frames at all. The OBC
  reloads `obc_lut_store.bin` (searched in `.`, `..`, `../..`) if it passes
  CRC, otherwise it waits for an uplink.
* **0xFF means "down".** On `swap_lut` every pick range in the block buffers
  is pre-filled with 0xFF, and a failed poll rewrites that entry's ranges
  with 0xFF. An all-0xFF field is *no answer*, not data. Bytes no pick claims
  stay 0x00.
* **Schedule.** Computed at load: tier-*t* entry *i* is polled in slots where
  `slot % span == i % span` (VH every subframe, H every 2, M every 4, L every
  8). Polls are spread across the 1 s window with a 50 ms link-pacing floor;
  an overrunning window finishes late but never skips a slot, so block phase
  is preserved.
* **Slot alignment on the wire.** The TM header's per-APID subframe count
  gives the position: `SFC % 16` is the slot, `SFC / 16` the master-frame id.
  `xdos_tm_view` positions subframes by these, so a lost frame leaves a
  visible gap instead of shifting everything after it.

*See also `docs/binary_lut_design.md` (frame-level design),
`src/lut/lut_format.hpp` (the constants both sides compile against) and
`src/lut/binary_lut_builder.hpp` (the format's reference comment).*
