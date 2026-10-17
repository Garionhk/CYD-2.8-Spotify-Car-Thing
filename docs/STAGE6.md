# Stage 6 — Car hardening

> **Since then (as of 1.0.0):**
> - A **wrong Wi-Fi password** is now recognised from ESP-IDF's disconnect reason. The screen says "Wrong password for …", and for a network the device has never joined, the setup hotspot opens by itself.
> - The **screen-off** dropdown applies instantly.
> - The idle screen shows the IP next to `cyd-spotify.local`.
> - Release work (Noto fonts, merged image, web installer) is in `docs/README.md`.

Firmware `0.9.7-stage6` (firmware only; the fonts are unchanged).

## What was added

- **Fast boot.** The Spotify task saves the current track (title, artist, cover URL, length) to `/last.json` whenever a new one starts: it writes `last.tmp`, then renames it. At power-up the screen shows that track and its cached cover about 1.2 s after power-on, before Wi-Fi, with a "Waiting for Wi-Fi..." pill. Taps say "Not connected yet" instead of queueing commands.
- **Power-cut-safe writes.**
  - Settings, the login and calibration go to NVS, which is power-safe, and are written only when they change.
  - The Stage 0 test's boot counter is no longer written on every boot.
  - The cover swap writes the new URL file first, removes the old pair, then renames. A cut at any point leaves either the old cover and URL, or no cover at all; never a new picture under an old URL.
- **Watchdog, 30 s**, on the main loop (`enableLoopWDT`) and the Spotify task. The few legitimate long waits feed it as they go: the calibration wizard, the setup page's Scan button, and art downloads. The boot log prints the reset reason: power-on, restart, CRASH, WATCHDOG or BROWNOUT.
- **Idle screen.** A clock at 90% of the screen width: the 7-segment font at 2× with 2 px spacing gives 288 px for "HH:MM". The date sits under it, and the "Nothing playing" block is centred below. Time comes from NTP; the time zone is on the setup page (default Vancouver, from the owner's Mac).
- **Screen off when idle.** After the chosen minutes with nothing playing or playback paused, the backlight turns off. The first touch only wakes it. The setup page's dropdown now applies instantly (before, it needed Save, which caused "no effect").
- **Faster pickup** (owner's report: 20–30 s). Paused and nothing-playing polls run every 4 / 5 s for the first 2 minutes, then every 8 / 10 s (they were 10 / 30 s). A touch on the screen polls at once.

## Wi-Fi bugs found by the hotspot test (fixed)

1. **Network 2 "timed out" 6 ms after starting.** The join timer was reset only when the state changed, so joining → joining inherited network 1's expired 15 s.
2. **Every later attempt was "refused" within ~5 ms.** Right after `WiFi.begin()`, the status still held the previous attempt's failure. Failures now count only after 3 s.
3. These two made the device retry its router every ~9 s, and the home router then temporarily stopped accepting it. That cleared by itself.
4. **Slow car starts.** Home Wi-Fi went first and took 15 s per round, about 50 s in total. Now the **last network that worked goes first**, saved only when it changes, and each attempt gets **8 s**. Real joins took 0.1–0.3 s in every log.

## Acceptance (owner's board)

- [x] **Late hotspot:** with the hotspot turned on 60 s after power-up, the CYD joined by itself (owner observed it; that boot's log was wiped by a later power-up).
- [x] **Power cuts:** the owner pulled power repeatedly (the collector saw it unreachable 21 times). Both boots that stayed up long enough to be recorded came back clean: power-on reason, both networks kept, fonts, last track, login and calibration intact, no errors. The owner judged the full 20 unnecessary.
- [x] **Screen off:** off 1 minute after the idle screen appeared, back on at the tap.
- [x] **Idle clock:** spacing approved on the device.
- [x] **Fast boot:** last track on screen at 1.2 s; Spotify confirmed about 10 s later.
