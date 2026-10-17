# Stage 0 — Hardware verification

> **Since then (as of 1.0.0):** Stage 0 is now its own env: `pio run -e hwtest -t upload`. The three display envs (`cyd`, `cyd_inv`, `cyd_st7789`) were replaced by one ST7789 build that switches to ILI9341 at runtime: the setup page's **Panel controller**, or an 8 s finger hold at power-on. Touch is bit-banged (the XPT2046 library never registered a tap on this board). Uploads use 115200 baud, and brightness is a percentage.

Firmware: `src/main.cpp` (version `0.0.1-stage0`). Flash it, open the serial monitor, and go through the checks below. Don't start Stage 1 until every box is ticked.

## Flash

```bash
pio run -e cyd -t upload && pio device monitor
```

Uploads run at 115200 baud. This board's CH340 chip fails at 921600.

## Display and touch: the MP3 player project's approach

- TFT_eSPI is built for **ST7789** (HSPI, BGR, 40 MHz). These are the same settings as `../ESP32 2.8 LCD MP3 Player - Internal Speaker`.
- **Panel**, **inversion** and **rotation** are settings in NVS. If the panel is set to ILI9341, the firmware resets the controller and sends the ILI9341 init table itself. One binary drives both panel types.
- Boot defaults: ST7789, **invert off**, rotation 1.
- **Blind recovery:** if the screen shows nothing, hold a finger on it from power-up for 8 s. The blue LED blinks from 4 s, then the device switches controller and restarts.
- Touch is **bit-banged** on 25/32/39/33. Calibration is stored in native portrait coordinates, so it works for both rotation 1 and rotation 3.

## First boot

1. On an uncalibrated board, the blue LED lights for 3 s. This is the boot-gesture window; don't touch the screen.
2. The **calibration wizard** runs in portrait. Press and hold each of the 4 targets, then tap the centre to confirm (it must land within 25 px).
3. The test screen appears:
   - Top: colour bars **RED GREEN BLUE WHITE BLACK YELLOW**, then R/G/B/grey gradients
   - Middle: 5 crosshair targets, a status line, and a heap line
   - Bottom: **INVERT · FLIP · BRIGHT · PANEL · RECAL**

## Checks

- [x] **Colours:** each bar matches its label, and the gradients are smooth. (Owner's board: ST7789, invert off.)
- [x] **Touch calibration:** accepted, with the confirm tap 6 px off (x 3875→186, y 184→3748, z ≥ 300, noise 85)
- [ ] **Touch accuracy:** tap each target several times. A green dot means within 10 px. The status line shows `N/M ok, max E`.
- [ ] **FLIP:** the screen rotates 180° (rotation 1 ↔ 3), and taps still land on the targets.
- [ ] **PANEL:** switches to ILI9341 and restarts. On an ST7789 board the screen will look wrong; hold a finger for 8 s at boot, or tap PANEL again, to switch back.
- [ ] **Backlight PWM:** **BRIGHT** steps through 255 → 170 → 90 → 30.
- [ ] **RGB LED:** flashes R, G, B at boot. A short **BOOT** press cycles off/red/green/blue/white.
- [ ] **BOOT long press (5 s):** erases settings and calibration, then reboots.
- [ ] **Persistence:** after a power cycle, settings and calibration are kept.
- [x] **LittleFS:** `fs: write/read OK`, total 2,031,616 bytes
- [x] **Heap:** about 324 KB free at idle, largest block about 110 KB, no PSRAM

## Known board quirk

The board sometimes drops off USB (`[Errno 6] Device not configured`), and the MP3 project saw the same. The serial logger in this session reconnects automatically.
