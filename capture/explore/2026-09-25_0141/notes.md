# DeepCreative exploration 2026-09-25 01:41

## Stage 1: I will now disable and enable screen brightness. I will also use a slider to adjust brightness
- Capture: `01_i_will_now_disable_and_enable_screen_bri.pcapng` (508K)
- Result: Screen turned off, then on again. Then brightness changed to dimmer, then brighter, then brightest
```
   5.620 EP01 OUT cmd=0x02 payload=0101
   5.620 EP81 IN  cmd=0x02 payload=
  11.933 EP01 OUT cmd=0x02 payload=010100002e
  11.934 EP81 IN  cmd=0x02 payload=
  15.147 EP01 OUT cmd=0x02 payload=010100001a
  15.148 EP81 IN  cmd=0x02 payload=
  17.373 EP01 OUT cmd=0x02 payload=010100002f
  17.374 EP81 IN  cmd=0x02 payload=
  19.320 EP01 OUT cmd=0x02 payload=0101000044
  19.320 EP81 IN  cmd=0x02 payload=
```

## Stage 2: RGB light efect. Two settings: 1 - Sync with motherboar RGB; 2 - Enable temperature warning RGB
- Capture: `02_rgb_light_efect_two_settings_1_sync_with.pcapng` (332K)
- Result: When temperature warning was selected color was blue. When motherbooard option was selected color was orange like the fans and ram DIMMS
```
   2.363 EP01 OUT cmd=0x02 payload=0101000144
   2.363 EP81 IN  cmd=0x02 payload=
   4.257 EP01 OUT cmd=0x02 payload=0101000044
   4.257 EP81 IN  cmd=0x02 payload=
   6.625 EP01 OUT cmd=0x02 payload=0101000144
   6.626 EP81 IN  cmd=0x02 payload=
  10.971 EP01 OUT cmd=0x02 payload=0101000044
  10.972 EP81 IN  cmd=0x02 payload=
  13.476 EP01 OUT cmd=0x02 payload=0101000144
  13.476 EP81 IN  cmd=0x02 payload=
```

## Stage 3: Idle settings: 1 - change to disable screen; 2 - use preset animation
- Capture: `03_idle_settings_1_change_to_disable_screen.pcapng` (344K)
- Result: Nothing as it did not go idle
```
   2.625 EP01 OUT cmd=0x02 payload=0001000144
   2.625 EP81 IN  cmd=0x02 payload=
   6.346 EP01 OUT cmd=0x02 payload=0101000144
   6.347 EP81 IN  cmd=0x02 payload=
```

## Stage 4: Media mode image switch time
- Capture: `04_media_mode_image_switch_time.pcapng` (452K)
- Result: image switch time altered to 3 then 5 then 7 seconds
```
   2.530 EP01 OUT cmd=0x07 payload=0002
   2.531 EP81 IN  cmd=0x07 payload=
   4.412 EP01 OUT cmd=0x07 payload=0102
   4.412 EP81 IN  cmd=0x07 payload=
   6.571 EP01 OUT cmd=0x07 payload=0202
   6.571 EP81 IN  cmd=0x07 payload=
```

## Stage 5: sync RHB with Image/GIF eedge picking
- Capture: `05_sync_rhb_with_image_gif_eedge_picking.pcapng` (284K)
- Result: The RGB change to the color of the edge of the image
```
   3.500 EP01 OUT cmd=0x02 payload=0101000244
   3.500 EP81 IN  cmd=0x02 payload=
  13.489 EP01 OUT cmd=0x02 payload=0101000144
  13.489 EP81 IN  cmd=0x02 payload=
```

## Stage 6: MOde switch
- Capture: `06_mode_switch.pcapng` (476K)
- Result: First we went in media mode, then in monitor mode and last into recording mode that shows graphs of CPI frequency and CPu temperature
```
   3.820 EP01 OUT cmd=0x03 payload=02
   3.820 EP81 IN  cmd=0x03 payload=
   7.411 EP01 OUT cmd=0x03 payload=01
   7.411 EP81 IN  cmd=0x03 payload=
  10.818 EP01 OUT cmd=0x03 payload=03
  10.818 EP81 IN  cmd=0x03 payload=
```
