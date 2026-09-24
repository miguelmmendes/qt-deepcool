# Reverse-engineering toolkit

Scripts used to map the MYSTIQUE protocol by capturing DeepCreative's USB traffic from a Windows VM
and then replaying commands from Linux. The findings live in [../PROTOCOL.md](../PROTOCOL.md).

## Progress

| Area | Status |
|---|---|
| Stats data packet (13 fields × `[u16][decimal]`) | Mapped fields 0-8 on hardware; fields 9-12 not shown by any screen |
| Stats layouts (`0x04 <main> 00 00 <aux>`) | 6 main screens, 3 bottom areas, all confirmed |
| Clock (`0x0A` on the data endpoint) | Confirmed; qt-deepcool syncs it at start and hourly |
| Image upload (`0x0F` + `DCLd` header + JPEG + `dcldfinish`) | Confirmed from Linux; 480x640 JPEG, ~0.6 s |
| Replace image (`0x09` clear, then upload) | Confirmed |
| Slideshow settings (`0x07`), delete all (`0x14`) | Seen in captures, not tested from Linux |
| Label text (`0x15`-`0x17`) | Rejected by firmware; labels are fixed |
| Display blanks without data | Observed; keep streaming `0x01` packets |

Open questions:
- Uploaded images persist across power cycles, so each upload probably writes flash. A frequently
  refreshed custom dashboard could wear it out; no RAM-only image command is known.
- `0x05`, `0x06`, `0x0B`, `0x08` meanings; the `DCLd` header's 32-char ID field.
- 5 V / 12 V divider defaults (`in4:3`, `in1:6.5`) were checked for plausibility only, not against BIOS.

## Tools

All Python tools run without root once `../70-deepcool.rules` is installed:
`uv run --with pyusb --with pillow <script>`. Stop `deepcool-cli` (and the VM) first — only one
program can hold the device.

| Script | Purpose |
|---|---|
| `mystique_image.py` | Upload an image (`upload pic.png`), test card (`test`), back to stats (`info`), `clear`, or `raw <cmd> <hex>` |
| `experiment.py` | Send commands, then stream fixed field values (`--cmd 04:05000001 --set 3=11 ...`) |
| `mode_sweep.py` | Cycle `0x04` main screens or aux areas with byte-numbered values (byte *i* = *i*) |
| `layout_cycle.py`, `aux_label_test.py` | One-off experiments (0x08 is not a layout selector; labels rejected) |
| `live_stats.py` | Prototype of the real-sensor feed that became `sensors.cpp` + `deepcool-cli` |
| `fan_identify.sh` | `sudo`: stops one motherboard fan output at a time to identify fans (restores settings) |
| `analysis/parse.py` | Decode a capture: control commands, `DCLd` uploads (JPEGs saved to `analysis/extracted/`) |
| `record.sh` | Guided capture: one `.pcapng` per step via `usbmon`, with notes in `notes.txt` |
| `docker-compose.yml` | Boots the existing Windows install in `~/.windows` with only the cooler passed through |

## Capturing DeepCreative traffic

```bash
sudo modprobe usbmon                        # cooler is on usbmon<bus>, see lsusb (bus 6 here)
./record.sh                                 # start recording first
sudo docker-compose up                      # second terminal; UI at http://localhost:8006
python3 analysis/parse.py <capture>.pcapng  # decode (cooler is USB address 5 here)
```

Notes: `dumpcap` drops privileges and can't write into `$HOME`, so `record.sh` writes via a shell
redirect. Captures (`*.pcapng`) are git-ignored; `notes.txt` records what was done in each one.
