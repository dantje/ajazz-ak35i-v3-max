# Ajazz AK35I V3 MAX — HID Protocol Reference

## 1. Device Identification

| Field | Value |
|-------|-------|
| USB VID | `0x0C45` (SONiX / Microdia) |
| USB PID | `0x8009` |
| USB Manufacturer string | `SONiX` |
| USB Product string | `USB KEYBOARD` |
| Label on device | `AK35I V3 MAX` |
| MCU | SONiX SN32F290 (ARM Cortex-M0+) |
| Display | 1.14-inch TFT, 240×135 pixels, RGB565, autonomous RTC |

---

## 2. Physical Setup

The back of the keyboard has two hardware switches:

| Switch | Positions | Notes |
|--------|-----------|-------|
| Connection mode | `BT` / `USB` / `RF` | Must be set to **USB** for HID commands to reach the display MCU |
| OS mode | `Win` / `MAC` | Affects key mapping only; does not affect the command protocol |

When in BT or RF mode the display is driven by the wireless MCU. USB HID
feature reports are acknowledged by the USB MCU but not forwarded to the
display — time sync, lighting, and image-upload commands have no effect.

---

## 3. HID Interfaces

The keyboard enumerates as four HID interfaces:

| hidraw (Linux) | Interface | Usage Page | Purpose |
|----------------|-----------|------------|---------|
| hidraw0 | 0 | `0x0001` | Boot keyboard (keystrokes) |
| hidraw1 | 1 | `0x000C` | Consumer / media keys |
| hidraw2 | 2 | `0xFF68` | Display image data — 4096-byte OUT reports |
| **hidraw3** | **3** | **`0xFF13`** | **Command channel** — 64-byte Feature + IN + OUT reports |

The HID report descriptor for interface 3 declares **no report IDs**;
every report on that interface is exactly **64 bytes**.

---

## 4. Transport Layer

### 4.1 Feature Reports (interface 3 — command channel)

All configuration commands use **HID Feature Reports** on interface 3.

**Sending a command (`HIDIOCSFEATURE`):**

Prepend one byte of report-ID `0x00` to the 64-byte payload, then call
`HIDIOCSFEATURE(65)`. The kernel strips the report-ID byte and delivers
the 64-byte payload to the device.

```
Buffer passed to HIDIOCSFEATURE(65):
  [0x00]  [byte 0 … byte 63]
   ↑ report-ID (stripped by kernel)   ↑ 64-byte payload delivered to device
```

**Reading an ACK (`HIDIOCGFEATURE`):**

Call `HIDIOCGFEATURE(65)` with a 65-byte buffer initialised to `0x00`.
The device response occupies bytes `[1..64]` (byte `[0]` is the report-ID).

### 4.2 Timing — 35 ms inter-report gap

**The firmware silently drops any command received less than ~35 ms after
the previous one.** Insert a 35 ms sleep after every `HIDIOCSFEATURE` call
(and again after `HIDIOCGFEATURE`).

---

## 5. Packet Formats

All command packets are exactly **64 bytes**.

### 5.1 Command Packet

Byte `[0]` is always `0x04` (Ajazz command prefix).

```
Byte:  0       1       2       3–7     8       9      10–63
     +-------+-------+-------+-------+-------+-------+-------+
     | 0x04  |  CMD  | arg1  | 0x00… | arg2  |arg2hi | 0x00… |
     +-------+-------+-------+-------+-------+-------+-------+
```

| Field | Byte | Description |
|-------|------|-------------|
| Prefix | 0 | Always `0x04` |
| CMD | 1 | Command byte (see §6) |
| arg1 | 2 | First argument (command-specific; often `0x00`) |
| Reserved | 3–7 | Always `0x00` |
| arg2 | 8 | Second argument (often `0x01` to begin, `0x00` for SAVE) |
| arg2-hi | 9 | High byte of arg2 when 16-bit (CMD_IMAGE chunk count only) |
| Reserved | 10–63 | Always `0x00` |

### 5.2 Data Payload

Used to carry data for an operation (time values, colour values, etc.).
The buffer is zeroed before populating specific fields. Some payloads end
with the delimiter `0xAA 0x55` at a fixed offset.

### 5.3 ACK / Response

The firmware responds to `HIDIOCGFEATURE` by echoing back a modified form
of the most recently received command packet. The ACK must be read and
discarded after each command that requires it (see operation sequences in §7).

---

## 6. Command Byte Reference

### 6.1 Control Commands

| Byte | Name | arg2 | ACK | Function |
|------|------|------|-----|----------|
| `0x02` | `CMD_SAVE` | `0x00` | yes | Commit / persist the current transaction |
| `0x18` | `CMD_START` | `0x01` | yes | Begin a session |
| `0xF0` | `CMD_FINISH` | `0x01` | no | End transaction (post-save cleanup; can be omitted) |

### 6.2 Write Commands

| Byte | Name | arg2 | ACK | Function |
|------|------|------|-----|----------|
| `0x13` | `CMD_LIGHTING` | `0x01` | yes | Write lighting mode |
| `0x28` | `CMD_TIME` | `0x01` | yes | Begin a time-sync transaction |
| `0x23` | `CMD_CUSTOM_LIGHT` | `0x09` (9 packets) | yes | Upload per-key custom RGB lighting |
| `0x72` | `CMD_IMAGE` | chunk count (16-bit LE) | yes | Begin image/GIF upload |

### 6.3 Read Commands

Read commands retrieve data from the device. The read protocol works as
follows:

1. Send `[04 CMD 00 00 00 00 00 00 01 00 … 00]` via `HIDIOCSFEATURE`.
2. Wait 35 ms, then call `HIDIOCGFEATURE` — the first response is an
   **echo/ACK** (the command packet echoed back with byte[3] set to
   `0x01`). **Discard this packet.**
3. Call `HIDIOCGFEATURE` repeatedly (with 35 ms gaps) to read the actual
   data packets (64 bytes each).

To retrieve K data packets, call `HIDIOCGFEATURE` a total of **K + 1**
times and discard the first result.

| Byte | Name | Data packets | Function |
|------|------|-------------|----------|
| `0x05` | `CMD_READ_ID` | 1 (64 B) | Device ID: capabilities word, VID, PID, firmware version |
| `0x12` | `CMD_READ_LIGHTING` | 1 (64 B) | Current lighting config (same layout as write payload §7.2.1) |
| `0xF5` | `CMD_READ_PERKEY` | 9 (576 B) | Live per-key RGB state (see §7.4) |

#### 6.3.1 CMD 0x05 — Device ID Payload

```
Byte:  0    1    2    3    4    5    6    7    8    9   10   11
     +----+----+----+----+----+----+----+----+----+----+----+----+
     | 40 | 30 | 00 | 00 | 45 | 0C | 09 | 80 | xx | xx | FF | FF |
     +----+----+----+----+----+----+----+----+----+----+----+----+
```

| Field | Bytes | Description |
|-------|-------|-------------|
| Capabilities | 0–3 | `0x3040` (constant) |
| VID | 4–5 | USB VID, little-endian (`0x0C45`) |
| PID | 6–7 | USB PID, little-endian (`0x8009`) |
| Firmware version | 8–9 | LE uint16 (e.g. `0x0127` = V1.27) |
| End marker | 10–11 | `0xFFFF` |

---

## 7. Operation Sequences

All sequences use Feature Reports on **interface 3** (hidraw3).
Insert a **35 ms delay after every send**.
`→ ACK` means call `HIDIOCGFEATURE` after the send (and delay again).

### 7.1 Time Sync

Sets the on-board RTC. The display clock runs autonomously after this;
no repeated updates are needed. Send local time, not UTC.

```
Step  Packet                                                    ACK
────  ──────────────────────────────────────────────────────    ───
 1.   CMD_START  [04 18 00 00 00 00 00 00 01 00 … 00]          no
 2.   CMD_TIME   [04 28 00 00 00 00 00 00 01 00 … 00]          yes
 3.   Time data  (see §7.1.1)                                  no
 4.   CMD_SAVE   [04 02 00 00 00 00 00 00 00 00 … 00]          yes
```

#### 7.1.1 Time Data Payload (64 bytes)

```
Byte:  0       1       2       3       4       5       6       7
     +-------+-------+-------+-------+-------+-------+-------+-------+
     | 0x00  | slot  | 0x5A  | year  | month |  day  | hour  |  min  |
     +-------+-------+-------+-------+-------+-------+-------+-------+

Byte:  8       9      10      11–61   62      63
     +-------+-------+-------+-------+-------+-------+
     |  sec  | 0x00  | 0x04  | 0x00… | 0xAA  | 0x55  |
     +-------+-------+-------+-------+-------+-------+
```

| Field | Byte | Description |
|-------|------|-------------|
| Marker | 0 | `0x00` |
| Slot | 1 | LCD slot index (1-based) |
| Magic | 2 | `0x5A` |
| Year | 3 | Year minus 2000 (e.g. 2026 → `0x1A`) |
| Month | 4 | 1–12 |
| Day | 5 | 1–31 |
| Hour | 6 | 0–23 (local time) |
| Minute | 7 | 0–59 |
| Second | 8 | 0–59 |
| Reserved | 9 | `0x00` |
| Unknown | 10 | `0x04` (constant in all observed implementations) |
| Padding | 11–61 | `0x00` |
| Delimiter | 62–63 | `0xAA 0x55` |

### 7.2 Lighting Mode

```
Step  Packet                                                    ACK
────  ──────────────────────────────────────────────────────    ───
 1.   CMD_START     [04 18 00 00 00 00 00 00 01 00 … 00]       yes
 2.   CMD_LIGHTING  [04 13 00 00 00 00 00 00 01 00 … 00]       yes
 3.   Lighting data (see §7.2.1)                               no
 4.   CMD_SAVE      [04 02 00 00 00 00 00 00 00 00 … 00]       yes
```

#### 7.2.1 Lighting Data Payload (64 bytes)

```
Byte:  0       1       2       3       4–7     8       9      10      11
     +-------+-------+-------+-------+-------+-------+-------+-------+-------+
     | mode  |   R   |   G   |   B   | 0x00… |rainbw |bright | speed |  dir  |
     +-------+-------+-------+-------+-------+-------+-------+-------+-------+

Byte: 12–61   62      63
     +-------+-------+-------+
     | 0x00… | 0x55  | 0xAA  |
     +-------+-------+-------+
```

| Field | Byte | Description |
|-------|------|-------------|
| Mode | 0 | Lighting mode byte (see §7.2.2) |
| R | 1 | Red 0–255 |
| G | 2 | Green 0–255 |
| B | 3 | Blue 0–255 |
| Reserved | 4–7 | `0x00` |
| Rainbow | 8 | `0x00` = off, `0x01` = multicolour cycling |
| Brightness | 9 | 0–5 |
| Speed | 10 | 0–5 |
| Direction | 11 | 0=left, 1=down, 2=up, 3=right |
| Padding | 12–61 | `0x00` |
| Delimiter | 62–63 | `0x55 0xAA` |

#### 7.2.2 Lighting Mode Values

| Value | Name | Description |
|-------|------|-------------|
| `0x00` | off | Backlight off |
| `0x01` | static | Solid colour |
| `0x02` | singleon | Single key illuminate |
| `0x03` | singleoff | Single key extinguish |
| `0x04` | glitter | Glitter / sparkle |
| `0x05` | falling | Falling keys |
| `0x06` | colourful | Multicolour static |
| `0x07` | breath | Breathing |
| `0x08` | spectrum | Colour spectrum cycle |
| `0x09` | outward | Outward wave |
| `0x0A` | scrolling | Scrolling wave |
| `0x0B` | rolling | Rolling |
| `0x0C` | rotating | Rotating |
| `0x0D` | explode | Explosion |
| `0x0E` | launch | Launch |
| `0x0F` | ripples | Ripples |
| `0x10` | flowing | Flowing |
| `0x11` | pulsating | Pulsating |
| `0x12` | tilt | Tilt |
| `0x13` | shuttle | Shuttle |
| `0x80` | custom | Per-key custom RGB (activate after uploading per-key data via CMD 0x23) |

### 7.3 Image / GIF Upload

Image data is sent as **output reports** on **interface 2** (hidraw2,
usage page `0xFF68`), 4096 bytes per chunk. Command packets use **feature
reports** on interface 3.

#### 7.3.1 Display Parameters

| Parameter | Value |
|-----------|-------|
| Width | 240 pixels |
| Height | 135 pixels |
| Pixel format | RGB565, little-endian |
| Bytes per frame | 240 × 135 × 2 = 64,800 |
| Header size | 256 bytes |
| Maximum frames | **141** |

#### 7.3.2 Protocol Sequence

```
Step  Packet                                                    ACK
────  ──────────────────────────────────────────────────────    ───
 1.   CMD_START   [04 18 00 00 00 00 00 00 01 00 … 00]         yes
 2.   CMD_IMAGE   (see §7.3.3)                                 yes
 3.   Data chunks (4096 bytes each on interface 2)             yes*
 4.   CMD_SAVE    [04 02 00 00 00 00 00 00 00 00 … 00]         yes

 * Data chunk ACKs use input reports: poll() + read() on the display
   interface fd, with a 300 ms timeout.
   WARNING: do NOT use HIDIOCGFEATURE on the display interface — it
   crashes the firmware and requires a power cycle to recover.
```

**Note on persistence:** the firmware writes pixel data directly to
non-volatile flash as each chunk is received. Skipping CMD_SAVE does not
prevent pixel data from being written — animations persist regardless.
CMD_SAVE commits the header metadata (frame count, delays, slot validity).

#### 7.3.3 CMD_IMAGE Command Packet

```
Byte:  0       1       2       3–7     8       9      10–63
     +-------+-------+-------+-------+-------+-------+-------+
     | 0x04  | 0x72  |  slot | 0x00… |chk_lo |chk_hi | 0x00… |
     +-------+-------+-------+-------+-------+-------+-------+
```

| Field | Byte | Description |
|-------|------|-------------|
| Prefix | 0 | `0x04` |
| CMD | 1 | `0x72` |
| Slot | 2 | LCD slot index (1-based) |
| Reserved | 3–7 | `0x00` |
| Chunk count lo | 8 | `total_chunks & 0xFF` |
| Chunk count hi | 9 | `total_chunks >> 8` |
| Reserved | 10–63 | `0x00` |

#### 7.3.4 Image Data Buffer Layout

```
     +==============================+
     |     Header (256 bytes)       |
     +==============================+
     |   Frame 0 pixels             |   240 × 135 × 2 = 64,800 bytes
     +------------------------------+
     |   Frame 1 pixels             |   64,800 bytes
     +------------------------------+
     |          ...                 |
     +------------------------------+
     |   Frame N-1 pixels           |
     +==============================+
```

#### 7.3.5 Image Header (256 bytes)

```
Byte:  0       1       2       ...     N       N+1     ...   255
     +-------+-------+-------+     +-------+-------+     +-------+
     |  N    |dly[0] |dly[1] | ... |dly[N-1]| 0xFF  | ... | 0xFF  |
     +-------+-------+-------+     +-------+-------+     +-------+
```

| Field | Byte | Description |
|-------|------|-------------|
| Frame count | 0 | Number of frames N (1–141) |
| Delay[0..N-1] | 1..N | Per-frame delay in 2 ms units (1–255; range 2–510 ms) |
| Padding | N+1–255 | `0xFF` |

#### 7.3.6 Upload Size Calculations

```
total_bytes = 256 + 64800 × N
chunks      = ceil(total_bytes / 4096)
```

| N frames | Total bytes | Chunks | Est. time |
|----------|-------------|--------|-----------|
| 1 | 65,056 | 16 | ~1 s |
| 10 | 648,256 | 159 | ~6 s |
| 50 | 3,240,256 | 792 | ~28 s |
| 100 | 6,480,256 | 1,583 | ~55 s |
| 141 (max) | 9,137,056 | 2,231 | ~78 s |

Upload rate is ~35 ms/chunk (one ACK round-trip per chunk).

### 7.4 Per-Key Custom Lighting

#### 7.4.1 Writing Per-Key Data (CMD 0x23)

Uploads 144 per-key RGB entries to flash. After writing, set lighting
mode to `0x80` via the lighting sequence (§7.2) to activate the per-key
renderer.

```
Step  Packet                                                    ACK
────  ──────────────────────────────────────────────────────    ───
 1.   CMD_START        [04 18 00 00 00 00 00 00 01 00 … 00]    yes
 2.   CMD_CUSTOM_LIGHT [04 23 00 00 00 00 00 00 09 00 … 00]    yes
 3.   Per-key data (9 × 64-byte packets = 576 bytes)           no
 4.   CMD_SAVE         [04 02 00 00 00 00 00 00 00 00 … 00]    yes
```

#### 7.4.2 Reading Live Per-Key State (CMD 0xF5)

Returns the live per-key RGB state from the firmware, reflecting both the
base animation colours and any Fn+~ per-key overrides.

No CMD_START is needed.

```
Step  Packet                                                    ACK
────  ──────────────────────────────────────────────────────    ───
 1.   CMD_READ_PERKEY  [04 F5 00 00 00 00 00 00 09 00 … 00]    yes*
 2.   9 × 64-byte data packets (576 bytes)                     n/a
 3.   CMD_SAVE         [04 02 00 00 00 00 00 00 00 00 … 00]    yes
 4.   CMD_FINISH       [04 F0 00 00 00 00 00 00 00 00 … 00]    no

 * The first HIDIOCGFEATURE response is an echo/ACK — discard it,
   then read 9 data packets (see §6.3 read protocol).
```

**State machine cleanup:** after reading all 9 data packets the firmware
enters a wait-for-save state. Steps 3–4 (CMD_SAVE + CMD_FINISH) reset
the state machine to idle. Without this cleanup, subsequent reads return
stale data.

#### 7.4.3 Per-Key Data Format (576 bytes)

Both CMD 0x23 (write) and CMD 0xF5 (read) use the same format:
144 entries of 4 bytes each.

```
Byte:  0       1       2       3       4       5       6       7
     +-------+-------+-------+-------+-------+-------+-------+-------+
     | pos_0 |  R_0  |  G_0  |  B_0  | pos_1 |  R_1  |  G_1  |  B_1  |
     +-------+-------+-------+-------+-------+-------+-------+-------+
     ...
```

| Field | Size | Description |
|-------|------|-------------|
| pos | 1 | Key position index (`0x00`–`0x8F`) |
| R | 1 | Red 0–255 |
| G | 1 | Green 0–255 |
| B | 1 | Blue 0–255 |

Entries with R=G=B=0 are either unused positions (no physical key) or
keys with no per-key colour set.

---

## 8. References

- [KyleBoyer/TFTTimeSync-node](https://github.com/KyleBoyer/TFTTimeSync-node) — Node.js implementation for AK35I V3 MAX
- [TaxMachine/ajazz-keyboard-software-linux](https://github.com/TaxMachine/ajazz-keyboard-software-linux) — Linux software for related models
- [fpb/ajazz-ak820-pro](https://github.com/fpb/ajazz-ak820-pro) — hardware teardown of related AK820 Pro
