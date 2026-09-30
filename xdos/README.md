# XDOS Ground Station — C++17

Level-1 ground station for the XDOS satellite, with a live multi-process
simulator stack and an OBC bridge.

## Layout

```
xdos_gs (CLI)  ──TCP 10000──►  xdos_obc (bridge + inspector CLI)
                                     │
                                     ├── TCP 10010 ──► xdos_eps_sim   (orbit/SOC/Vbatt engine)
                                     ├── TCP 10020 ──► xdos_adcs_sim  (quaternion + PD engine)
                                     └── TCP 10030 ──► xdos_gps_sim   (Keplerian orbit engine)
```

OBC routes incoming frames by sniffing their native framing prefix:

| Prefix              | Subsystem |
| ------------------- | --------- |
| `0x00 ... 0x01`     | EPS       |
| `0x1F 0x7F ...`     | ADCS      |
| `0xAA 0x44 0x12 ...`| GPS       |

CCSDS Space Packet wrapping on the GS↔OBC link is deferred to level 2.

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/xdos_tests        # 245 assertions in 34 test cases, all pass
```

Tools used: C++17, gcc 11+ / 13+, CMake 3.20+, nlohmann/json (vendored under
`third_party/`), Catch2 v3 (system package, optional).

## Run

Easiest:

```bash
scripts/run_all.sh        # spawns sims + OBC in background, exec GS in foreground
                          # Ctrl-C cleans up everything
scripts/run_all.sh --no-obc   # skip OBC, talk direct to subsystem sims
```

Or manually, one terminal each:

```bash
./build/xdos_eps_sim                 # add --quiet to suppress engine heartbeat
./build/xdos_adcs_sim
./build/xdos_gps_sim
./build/xdos_obc                     # bridge with its own inspector CLI
./build/xdos_gs                      # operator CLI (via OBC by default)
./build/xdos_gs --no-obc             # direct connect, bypass OBC
```

## CLI cheat-sheet

At the main menu:

| key | action                  |
| --- | ----------------------- |
| `1` | Send TC to ADCS         |
| `2` | Send TC to EPS          |
| `3` | Send TC to GPS          |
| `r` | Reconnect buses         |
| `q` | Quit                    |

At any TC-id prompt:

| input    | meaning                                  |
| -------- | ---------------------------------------- |
| `42`     | send TC 42 (you'll be prompted for params) |
| `?42`    | show TC 42 details without sending       |
| `<Enter>`| next page (if listing > 20)              |
| `b`      | back one level                           |
| `q`      | quit                                     |

ADCS gets a filter-by-kind sub-prompt because of its 152-entry listing:
`[s]et / [g]et / [a]ct / [Enter]=all / [b]ack`.

TM-requests (GET TCs) are auto-wrapped:
- GPS: `LOG(message_id=ID, trigger=ONCE)`
- ADCS: empty-payload TC frame with the TM's msg_id

Colors: yellow=SET, green=GET, cyan=ACT, red=error, bold-white=headers.
Pass `--no-color` to strip ANSI escapes (handy for piping to a file).

## Old CLI

`src/cli/main.cpp.old` is the previous 9-item-menu CLI, kept for reference.
Safe to delete once the new flow is comfortable.
