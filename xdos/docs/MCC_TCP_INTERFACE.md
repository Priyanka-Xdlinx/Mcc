# MCC TCP Interface — Rate-Group Subframe Feed

*Ground Station → MCC Software · protocol `XMCC` version 1*

The ground station forwards **every rate-group subframe it receives from the
OBC** to the MCC software over a plain TCP connection. One TCP message = one
subframe: a fixed 26-byte header saying *where and when* the subframe belongs,
followed by the subframe's raw payload bytes.

This document is everything needed to decode the feed.

---

## 1. Connecting

The link is **one-way** — the GS only sends, and never expects MCC to reply.
Either side may own the socket; the GS is configured for whichever suits you:

| Mode | GS is | MCC is | GS launched with |
|------|-------|--------|------------------|
| A | TCP **server** (listens) | TCP **client** (connects in) | `--mcc-listen [HOST:]PORT` |
| B | TCP **client** (dials out) | TCP **server** (listens) | `--mcc-connect HOST:PORT` |

Behaviour worth relying on:

* **Mode A** accepts multiple MCC clients at once — every connected client gets
  every subframe. A client that stops reading is dropped when the send fails.
* **Mode B** retries on the next subframe after a connection failure, so the
  feed re-establishes itself without restarting the GS.
* There is **no handshake, no login and no keep-alive**. Data starts flowing on
  the next subframe after the connection is up.
* Anything MCC sends on this socket is ignored.

---

## 2. Message format

**Every field is big-endian (network byte order).**

| Offset | Size | Field | Notes |
|-------:|-----:|-------|-------|
| 0 | 4 | magic `"XMCC"` | `0x58 0x4D 0x43 0x43` |
| 4 | 1 | version | `1` for this document |
| 5 | 1 | reserved | `0` |
| 6 | 4 | **master frame number** | u32, which master frame |
| 10 | 2 | **subframe number** | u16, **1..16** within that master frame |
| 12 | 2 | timestamp — year | u16, e.g. `2026` |
| 14 | 1 | timestamp — month | 1..12 |
| 15 | 1 | timestamp — day | 1..31 |
| 16 | 1 | timestamp — hour | 0..23 |
| 17 | 1 | timestamp — minute | 0..59 |
| 18 | 1 | timestamp — second | 0..59 |
| 19 | 2 | timestamp — millisecond | u16, 0..999 |
| 21 | 1 | reserved | `0` |
| 22 | 4 | **payload length** | u32, byte count that follows — normally `1280` |
| 26 | N | **payload** | the subframe data field |

**Total = 26 + 1280 = 1306 bytes** per subframe at the standard subframe size.

### Field semantics

**Master frame number** — a free-running counter derived from the frame's
per-APID subframe count (`SFC / 16`). It is *not* reset per pass. Use it
together with the subframe number as the unique position key.

**Subframe number** — `1..16`, i.e. `(SFC % 16) + 1`. Sent 1-based to match
the GS operator displays. A gap in this sequence means a subframe was lost on
the downlink — the numbering never shifts to hide it, so a missing subframe
stays visible as a gap rather than silently displacing later data.

**Timestamp** — the subframe's own **CCS Time Code, copied field for field out
of the TM header**. It is *not* a ground receive time and *not* converted to a
Unix epoch, deliberately: passing it through verbatim keeps it free of timezone
and leap-second handling, so MCC sees exactly what the spacecraft stamped.

**Payload length** — always read this rather than assuming 1280. It is present
so the feed stays parseable if the subframe size is ever changed.

**Payload** — the subframe **data field only**: no CCSDS TM header, no CRC,
no framing. The GS has already validated and stripped all of that.

---

## 3. Framing rules (important)

TCP is a byte **stream**, not a message queue. A single `recv()` may return
half a message, or several messages joined together. Decode like this:

1. Read exactly 26 bytes → header.
2. Verify magic is `XMCC`. If not, you are out of sync — scan forward for the
   next `XMCC` and resume.
3. Verify version is `1`. A higher version means the GS was upgraded; the
   header may have grown, so stop rather than misparse.
4. Read exactly `payload length` further bytes → payload.
5. Repeat.

Always loop on `recv()` until you have the full count you asked for. Never
assume one `recv()` == one subframe.

---

## 4. Payload layout

The 1280-byte payload is partitioned into four **priority tier segments**,
always at the same fixed offsets:

```
byte 0                400              800           1100      1275  1280
     |       VH       |        H        |      M      |    L    | rsv |
            400 B            400 B          300 B        175 B    5 B
```

| Tier | Segment in this subframe | Block spans | Full block size |
|------|--------------------------|-------------|-----------------|
| VH (Very High) | `[0:400]` | 1 subframe | 400 B |
| H (High) | `[400:800]` | 2 subframes | 800 B |
| M (Medium) | `[800:1100]` | 4 subframes | 1200 B |
| L (Low) | `[1100:1275]` | 8 subframes | 1400 B |
| reserved | `[1275:1280]` | — | sent as zero |

**This is the part that matters most for decoding.** Only the VH segment is
complete in a single subframe. The other tiers are *slices* of a larger block
that must be concatenated across consecutive subframes before their contents
mean anything:

* **VH** — complete every subframe.
* **H** — concatenate the H segments of 2 consecutive subframes → 800 B block.
  Slice index = `(subframe_number - 1) % 2`.
* **M** — concatenate 4 consecutive → 1200 B block. Slice = `(sf - 1) % 4`.
* **L** — concatenate 8 consecutive → 1400 B block. Slice = `(sf - 1) % 8`.

A block is only valid once **all** its slices have arrived. If a subframe was
lost, the block containing it is incomplete — treat those values as stale, not
as data.

### Two byte values with special meaning

* **All `0xFF` across a parameter's bytes = that subsystem did not answer.**
  The OBC pre-fills every picked byte range with `0xFF`, and rewrites it with
  `0xFF` when a poll fails. It means *no answer*, **not** a reading of 255.
* **`0x00` = bytes no parameter claims.** Unfilled block space is zero-padded.

---

## 5. Turning payload bytes into named parameters

The payload carries **no names and no identifiers** — only bytes at known
offsets. The OBC deliberately holds no database; all meaning lives on the
ground in the LUT and its ground-side twin.

To decode parameters, MCC needs the twin file from the GS:

```
config/rate_group_lut_bin_gs.json
```

It lists, per tier and in **entry order**, each command's id, response name and
picks, where each pick carries `block` (its byte offset inside that tier's
assembled block) and `size`. Decoding is then:

```
value_bytes = tier_block[pick.block : pick.block + pick.size]
```

### The CRC must match — check it

The twin and the binary LUT the OBC is executing both carry a **CRC-16**
(`crc16` in the twin). Position within a tier is the *only* link between what
the OBC transmits and what the ground decodes — there are no ids on the wire.
If MCC decodes with a twin whose CRC differs from the table the OBC is running,
**every value will be read from the wrong offsets and will still look
plausible**. Always confirm the twin's CRC matches the LUT in flight before
trusting decoded values.

---

## 6. What to expect operationally

* **Cadence** — one subframe per second (`T_sf` = 1000 ms), so one complete
  master frame every 16 seconds.
* **The first master frame after OBC boot is a warm-up — discard it.** The
  OBC starts with all pick ranges set to `0xFF` and each entry is only polled
  once its turn comes round in its tier's block span. Measured from a cold
  start: VH is complete from subframe 1, H from subframe 4, M from subframe 11,
  and L does not fully populate within the first master frame at all. From the
  second master frame onward every entry reads valid.
* **Gaps are real.** A missing subframe number means a lost downlink frame,
  not a renumbering.

---

## 7. Reference decoder

Minimal, complete, and matches the format above:

```python
import socket, struct

HDR = 26

def recv_exact(sock, n):
    buf = b''
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError('GS closed the connection')
        buf += chunk
    return buf

def read_subframe(sock):
    h = recv_exact(sock, HDR)
    if h[:4] != b'XMCC':
        raise ValueError('out of sync: bad magic')
    version = h[4]
    if version != 1:
        raise ValueError(f'unsupported XMCC version {version}')
    mf, sf, year = struct.unpack('>IHH', h[6:14])
    month, day, hour, minute, second = h[14], h[15], h[16], h[17], h[18]
    ms, _reserved, length = struct.unpack('>HBI', h[19:26])
    payload = recv_exact(sock, length)
    return {
        'master_frame': mf,
        'subframe': sf,                      # 1..16
        'time': f'{year:04d}-{month:02d}-{day:02d} '
                f'{hour:02d}:{minute:02d}:{second:02d}.{ms:03d}',
        'payload': payload,                  # 1280 bytes
        'VH': payload[0:400],
        'H':  payload[400:800],
        'M':  payload[800:1100],
        'L':  payload[1100:1275],
    }

# Mode A — MCC connects to the GS:
sock = socket.create_connection(('<gs-host>', 9100))
while True:
    f = read_subframe(sock)
    print(f"MF#{f['master_frame']} SF {f['subframe']}/16  {f['time']}  "
          f"{len(f['payload'])} B")
```

### Assembling a multi-subframe tier block

```python
from collections import defaultdict

SPAN = {'VH': 1, 'H': 2, 'M': 4, 'L': 8}
slices = defaultdict(dict)          # (tier, block_id) -> {slice_index: bytes}

def feed(tier, f):
    span = SPAN[tier]
    idx = (f['subframe'] - 1) % span
    block_id = (f['master_frame'], (f['subframe'] - 1) // span)
    slices[(tier, block_id)][idx] = f[tier]
    have = slices[(tier, block_id)]
    if len(have) == span:                       # every slice present
        return b''.join(have[i] for i in range(span))
    return None                                 # still filling
```

---

## 8. Sample of a live feed

Captured from the simulator, one line per received subframe:

```
v1 MF#0 SF 15/16  2026-09-20 12:11:45.787  len 1280
v1 MF#0 SF 16/16  2026-09-20 12:11:46.787  len 1280
v1 MF#1 SF  1/16  2026-09-20 12:11:47.787  len 1280
v1 MF#1 SF  2/16  2026-09-20 12:11:48.787  len 1280
```

Note the master frame number incrementing as the subframe number wraps 16 → 1,
and the one-second cadence.

---

## 9. Versioning

Byte 4 is the format version. Version `1` is described here. If the header ever
gains fields, the version increments and the payload offset moves — which is
why **MCC must reject an unknown version rather than parse it**, and why the
payload length is an explicit field rather than a constant.

Both reserved bytes (offsets 5 and 21) are sent as `0`. Ignore them on receive;
do not assume they stay zero in a future version.
