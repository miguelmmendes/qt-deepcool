# DeepCool MYSTIQUE Protocol Documentation

This document describes the USB protocol used by DeepCool MYSTIQUE 240/360 AIO coolers to display system information on their LCD screens.

## Device Information

| Property | Value |
|----------|-------|
| Vendor ID | `0x3633` |
| Product ID | `0x0009` |
| Interface Class | Vendor Specific (0xFF) |
| Endpoints | 0x01 OUT, 0x81 IN (init), 0x02 OUT, 0x82 IN (data) |
| Max Packet Size | 64 bytes |

## Packet Structure

All packets are 48 bytes with the following structure:

```
Offset  Size  Description
------  ----  -----------
0-1     2     Header: 0xAA 0x2E
2       1     Command byte
3-41    39    Payload (command-specific)
42-45   4     Footer: "HIDC" (0x48 0x49 0x44 0x43)
46-47   2     Checksum (little-endian sum of bytes 0-45)
```

### Response Packets

Device responses use header `0x55 0x2E` instead of `0xAA 0x2E`. The command byte is echoed back (or `0x00` for unknown commands).

## Endpoints

The device has two endpoint pairs:

- **Endpoint 0x01/0x81**: Used for initialization and configuration commands
- **Endpoint 0x02/0x82**: Used for display data updates

## Initialization Sequence

After a cold boot, the device shows a logo and ignores display data. The following sequence must be sent on **endpoint 0x01** to activate Machine Info mode:

### 1. Device Info Request (0x12)
```
AA 2E 12 00 00 ... 00 48 49 44 43 [checksum]
```
Response contains device serial number as ASCII string.

### 2. Configuration (0x02)
```
AA 2E 02 01 00 03 01 24 00 ... 00 48 49 44 43 [checksum]
```
Payload: `01 00 03 01 24`

### 3. Setup Commands (0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0B)
```
0x03: payload 01
0x04: payload 05 00 00 01
0x07: payload 00 02
0x08: payload 00 04
0x05: payload 01 01
0x0B: no payload
0x06: payload 01
```

### 4. Display Labels (0x15, 0x16, 0x17)
```
0x15: payload 2D 2D ("--")
0x16: payload 2D 2D ("--")
0x17: payload 2D 2D ("--")
```
Originally assumed to set display labels. The current firmware answers all three with
command byte `00` (unknown) and they have no visible effect.

### 5. Clock (0x0A), on the data endpoint 0x02
```
AA 2E 0A EA 07 09 19 00 30 32 00 ... 00 48 49 44 43 [checksum]
```
Payload: year (LE16), month, day, hour, minute, second in local time. It must go to **endpoint 0x02**:
on 0x01 the device rejects it (replies `00`). The original capture's `EA 07 02 02 02 27 21` was just
the capture's timestamp, not a "mode switch".

### 6. Status Request (0x10), data endpoint 0x02 only
On endpoint 0x01 it is rejected with `00`. See "Status Request Command" below.

The current DeepCreative startup (2026-09 capture) sends only `0x12`, `0x02 01 01 00 01 2D`, `0x03 01`,
`0x04 <layout>`, `0x07 00 02`, `0x08 00 FF` on 0x01, then `0x0A <time>` on 0x02.

## Display Data Command (0x01)

Sent on **endpoint 0x02** to update the display. This is the main command for showing system metrics.

### Packet Layout
13 fields of 3 bytes starting at offset 3 (`[uint16 LE integer][2-digit decimal]`), then the
`HIDC` footer and checksum. See **Machine Info Layouts and Data Fields** at the end of this
document for the field map. (An earlier byte-by-byte table here was a partial guess: its
"flags" and "MHz" bytes were really the voltage and fan-RPM fields.)

### Temperature-Based LED Color

The device has built-in temperature thresholds that change the LED ring color:
- Green: Low temperature
- Yellow/Orange: Medium temperature
- Red: High temperature (warning)

The exact thresholds are controlled by the device firmware.

## Status Request Command (0x10)

Query the current device state.

### Request
```
AA 2E 10 00 00 ... 00 48 49 44 43 00 02
```

### Response
```
55 2E 10 00 00 NN 00 ... 00 48 49 44 43 [checksum]
```
Only valid on the data endpoint 0x02. In the Windows captures byte 5 (`NN`) changes on every
poll (seen 01-0x25), so it is not a mode flag as first assumed; its meaning is unknown.

## Checksum Calculation

The checksum is a simple sum of all bytes from offset 0 to 45, stored as a 16-bit little-endian value at offsets 46-47.

```c
uint16_t checksum = 0;
for (int i = 0; i < 46; i++) {
    checksum += packet[i];
}
packet[46] = checksum & 0xFF;
packet[47] = (checksum >> 8) & 0xFF;
```

## Example: Complete Initialization + Display Update

```c
// 1. Send init sequence on endpoint 0x01
send_packet(EP_0x01, build_packet(0x12, NULL));
send_packet(EP_0x01, build_packet(0x02, "\x01\x00\x03\x01\x24"));
send_packet(EP_0x01, build_packet(0x03, "\x01"));
send_packet(EP_0x01, build_packet(0x04, "\x05\x00\x00\x01"));
send_packet(EP_0x01, build_packet(0x07, "\x00\x02"));
send_packet(EP_0x01, build_packet(0x08, "\x00\x04"));
send_packet(EP_0x01, build_packet(0x05, "\x01\x01"));
send_packet(EP_0x01, build_packet(0x0B, NULL));
send_packet(EP_0x01, build_packet(0x06, "\x01"));
send_packet(EP_0x01, build_packet(0x15, "\x2d\x2d"));
send_packet(EP_0x01, build_packet(0x16, "\x2d\x2d"));
send_packet(EP_0x01, build_packet(0x17, "\x2d\x2d"));

// 2. Set the clock on the DATA endpoint, then stream display data
send_packet(EP_0x02, build_packet(0x0A, local_time_payload));  // year LE16, mon, day, h, m, s
recv_packet(EP_0x82);
while (running) {
    send_packet(EP_0x02, build_packet(0x10, NULL));  // Status request
    recv_packet(EP_0x82);

    send_packet(EP_0x02, build_display_packet(fields));  // 13 x [u16 int][decimal]; screen blanks if this stops
    recv_packet(EP_0x82);

    sleep(1);
}
```

## Known Command Bytes

| Command | Endpoint | Description |
|---------|----------|-------------|
| 0x01 | 0x02 | Display data update |
| 0x02 | 0x01 | Configuration (rotation + settings) |
| 0x03 | 0x01 | Display mode: `01` stats, `02` image slideshow |
| 0x04 | 0x01 | Stats layout: `<main> 00 00 <aux>` |
| 0x05 | 0x01 | Setup (unknown) |
| 0x06 | 0x01 | Setup (unknown) |
| 0x07 | 0x01 | Slideshow interval / effect |
| 0x08 | 0x01 | Image list select/query (`00 FF` at startup returns a count) |
| 0x09 | 0x01 | Clear stored image list |
| 0x0A | 0x02 | Set clock (rejected on 0x01) |
| 0x0B | 0x01 | Setup (unknown) |
| 0x0F | 0x01 | Begin image upload |
| 0x10 | 0x02 | Status request |
| 0x12 | 0x01 | Device info request |
| 0x14 | 0x01 | Delete all images |
| 0x15-0x17 | 0x01 | Rejected (`00`) by current firmware |

## Screen Rotation Command (0x02)

Sent on **endpoint 0x01** to rotate the display. Uses the same command byte as the
initial configuration but can be sent at any time after initialization.

### Packet Layout
```
Offset  Description              Values
------  -----------              ------
0-1     Header                   AA 2E
2       Command                  02
3       Constant                 01
4       Constant                 00
5       Rotation                 00=0°, 01=90°, 02=180°, 03=270°
6       Constant                 01
7       Constant                 24
8-41    Reserved                 00 ...
42-45   Footer                   48 49 44 43
46-47   Checksum                 [calculated]
```

### Status Response Rotation Field

After setting rotation, the status response (0x10) byte 4 reflects the current
rotation value (0x00-0x03). Byte 5 remains 0xFF when in Machine Info mode.

## Notes

- The device must be initialized after every cold boot (full power cycle)
- Warm reboots may preserve the device state
- Images/GIFs uploaded via Windows software persist in device memory
- The protocol was reverse-engineered from USB captures of the Windows DeepCreative software

## References

- Implementation: `deepcooldevice.cpp`
- Protocol reverse-engineered from USB captures of Windows DeepCreative software

## Image Upload (reverse-engineered 2026-09-24)

DeepCreative uploads images and GIFs to the LCD as **baseline JPEGs, 480x640 (portrait), 4:2:0**.
Everything goes over **endpoint 0x01** (control replies on 0x81). Periodic 0x10/0x01 traffic on
0x02 keeps running during uploads.

### Sequence
1. `AA 2E 0F` (no payload): begin upload. Device echoes `55 2E 0F`.
2. For each frame, a 64-byte raw `DCLd` header, followed by the JPEG streamed raw in 64-byte chunks
   (the last chunk is short).
3. A 64-byte raw trailer: ASCII `dcldfinish`, zero-padded.
4. `AA 2E 08 <a> <b>`: select/show. Observed: first image `00 00`, second image `00 01`
   (device then alternated both every ~3 s), GIF `01 00`. Probably `<a>` = 0 still / 1 GIF, `<b>` = slot.

### DCLd header (64 bytes)
```
Offset  Size  Description
0-3     4     "DCLd"
4       1     Kind: 01 = still image, 02 = GIF frame
5-8     4     JPEG length (LE)
9-10    2     16-bit sum of all JPEG bytes (LE)
11-12   2     00 00
13      1     GIF: frame count (00 for stills)
14      1     00
15-16   2     GIF: 0x05DC = 1500 (3 frames x 500 ms; total or per-GIF duration, unconfirmed)
17      1     GIF: frame index (0-based)
18-19   2     00 00
20-51   32    ASCII hex ID (MD5-like; not MD5 of the JPEG or the source file; same for all frames of one GIF)
52-61   10    00
62-63   2     16-bit sum of bytes 0-61 (LE)
```
A GIF is sent as `DCLd`+JPEG repeated for each frame, then a single `dcldfinish`.

### Other commands
| Command | Payload | Meaning (confirmed on hardware unless noted) |
|---|---|---|
| `0x03` | `01` / `02` / `03` | Display mode: `01` = Machine Info (stats), `02` = image slideshow, `03` = history graphs (CPU frequency + temperature) |
| `0x02` | `<idle> 01 <rotation> <led> <brightness>` | Display settings, see below |
| `0x09` | none | Clear the stored image list. DeepCreative "deletes one" by sending 0x09 and re-uploading the rest |
| `0x14` | none | "Delete all" in DeepCreative (not yet tested from Linux) |
| `0x07` | `<interval> <effect>` | Slideshow: interval 00/01/02 = 3/5/7 s (from DeepCreative capture); effect 00 = split(?), 01 = scroll, 02 = fade |
| `0x08` | `<a> <b>` | Sent after uploads and on layout changes; exact meaning unknown |

### Display settings (0x02), mapped 2026-09-25 from DeepCreative
```
AA 2E 02 <idle> 01 <rotation> <led> <brightness> ...
```
| Byte | Meaning | Values |
|---|---|---|
| idle | Idle behaviour | `00` screen off, `01` preset animation |
| 01 | Constant in current DeepCreative (older captures had `00`) | |
| rotation | Orientation | `00`-`03` = 0/90/180/270° |
| led | LED ring colour source | `00` motherboard ARGB sync, `01` CPU temperature, `02` edge colour of the shown picture (confirmed on hardware) |
| brightness | Screen brightness | `00` = screen off, observed up to `0x44`; qt-deepcool clamps 0-100 |

All five settings are sent together, so the driver must remember them: re-sending the old fixed
`01 00 <rot> 01 24` (as the original rotation code did) resets brightness and LED mode.
The original init therefore always forced the LED to temperature mode.
There is no command for an arbitrary LED colour; qt-deepcool emulates one by painting a border
of that colour around the picture while the LED follows the picture edge.

Uploads **append** to a slideshow that the device keeps across power cycles (so they are stored in flash).
To replace what's shown with a single image: `0x09`, `0x0F`, DCLd+JPEG, `dcldfinish`, `0x08 00 00`, `0x03 02`.
A 15 KB JPEG takes ~0.6 s end to end. Tool: `capture/mystique_image.py`.

## Machine Info Layouts and Data Fields (mapped on hardware 2026-09-25)

### Display data packet (cmd 0x01, EP 0x02) is 13 fields of 3 bytes
Field `k` lives at offset `3 + 3k`: bytes `[0..1]` = 16-bit LE integer part, byte `[2]` = 2-digit decimal part.
Fields are rendered by the firmware; labels/icons are fixed. Updating values writes no flash.

| Field | Bytes | Meaning |
|---|---|---|
| 0 | 3-5 | CPU temperature (main CPU-temp layout; drives LED ring colour) |
| 1 | 6-8 | CPU usage % (System Monitor aux) |
| 2 | 9-11 | RAM usage % (System Monitor aux) |
| 3 | 12-14 | 3.3 V (voltage aux) |
| 4 | 15-17 | 5 V (voltage aux) |
| 5 | 18-20 | 12 V (voltage aux) |
| 6 | 21-23 | CPU frequency (GHz) — main layout 0 and System Monitor aux |
| 7 | 24-26 | CPU fan RPM (likely) |
| 8 | 27-29 | Pump / fan RPM (main layout 2) |
| 9-12 | 30-41 | Not shown by any layout found so far |

The older byte table above (GPU temp at 14, "flags") was a partial guess; this table supersedes it.

### Layout select: `AA 2E 04 <main> 00 00 <aux>` (EP 0x01)
| main | Big display | | aux | Bottom area |
|---|---|---|---|---|
| 0 | CPU frequency | | 0 | 3.3 V / 5 V / 12 V |
| 1 | Clock (set via 0x0A on EP 0x02) | | 1 | System Monitor: GHz, CPU %, RAM % |
| 2 | Fan/pump speed | | 2 | Core Data: CPU temp, GHz |
| 3 | CPU fan | | | |
| 4 | CPU fan + pump combo | | | |
| 5 | CPU temperature | | | |

`0x15`/`0x16`/`0x17` (label text) are answered with command byte `00` (unknown) and have no visible effect.
**Clock:** `AA 2E 0A <year LE16> <month> <day> <hour> <minute> <second>` sent on the **data endpoint
0x02** (reply on 0x82 echoes `0A`). Confirmed on hardware. On endpoint 0x01 the same packet is
rejected with `00`, which is why the original init sequence never set the time. The device keeps
the clock running itself; DeepCreative sends it once at startup.
The display blanks if no data packets arrive for a while, so keep streaming `0x01` packets.
`0x10` (status) is only valid on the data endpoint 0x02; on 0x01 it is answered with `00`.
The CPU-temp screen shows whole degrees (the decimal byte is ignored).

## Firmware and on-device storage (from DeepCreative's bundled MYSTIQUE_2.70.bin)

DeepCreative (Electron app, `C:\DeepCool`) ships `resources/app.asar.unpacked/resources/fw/MYSTIQUE_2.70.bin`
(451 096 bytes, SHA-256 `df9c19b5…17e6e`, not redistributed here). It is ARM Cortex-M code for a
**Synwit** MCU running **LVGL** with Synwit's "SynwitUI" layer. Strings show how the device is organised:

- **Stock screens are data, not code**: the firmware loads `SPI:ui.bin` (screen table, widgets, resources,
  fonts; format-versioned: "Incompatible UI data(fmt:%d.%d)") from the internal SPI flash. The app does not
  ship a `ui.bin`, so it is written at the factory. Changing the stock theme means replacing that file, which
  needs (1) a way to write it and (2) the SynwitUI format; neither is known.
- **Uploaded pictures live on a filesystem** (`SD:` volume on flash): `%s/index%03d.jpg`, `%s/angle_%d.jpg`,
  `%s/angle_0/index%03d.jpg`, `SD:GIF/%s/config.cfg`, `SD:DeepCool.cfg`. The 32-character ID in the DCLd
  upload header is most likely the `%s` folder name.
- **Firmware update** goes through a file: `SD:FW_update.bin`. How DeepCreative transfers it (possibly a
  DCLd-style upload with another kind byte) is not captured yet.

Tools: `capture/analysis/winimg.py` (read-only NTFS access to the VM disk image) and
`capture/analysis/asar_get.py` (extract files from app.asar).
