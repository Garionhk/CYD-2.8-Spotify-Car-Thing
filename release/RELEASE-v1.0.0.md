# CYD Spotify Remote v1.0.0

The first release: a "Car Thing"-style Spotify now-playing screen and remote for the 2.8" ESP32 **Cheap Yellow Display** (ESP32-2432S028R). The CYD plays no audio; it controls Spotify on your phone, computer or speaker over Wi-Fi. In the car it connects through your phone's hotspot.

**Needs Spotify Premium** and a free Spotify developer app (3 minutes; steps in the README).

## Install

Download **`cyd-spotify-v1.0.0.bin`** below. It's the whole 4 MB flash (app, fonts, partition table), written at address **0x0**.

**In the browser** with [esptool.spacehuhn.com](https://esptool.spacehuhn.com/), in Chrome or Edge on a computer:

1. Set **115200** baud, then **Connect** and choose the CYD's port.
2. Set the offset to **0x0**, choose the `.bin`, and click **Program**.
3. Replug the CYD when it's done.

**Command line:**

```bash
esptool.py --chip esp32 --baud 115200 write_flash 0x0 cyd-spotify-v1.0.0.bin
```

Installing erases the board. Afterwards, follow **[README → First-time setup](../README.md#2-first-time-setup)**: touch calibration, the setup hotspot, Wi-Fi, then the Spotify login.

SHA-256: `1622d5f19e9718d0e8070cc146584d777f13de058259e9ed91d5e933a729d578`

## What it does

- **Now playing:** a 225 px cover, with the title, artist and progress over it. Titles in Chinese, Japanese, Korean, Latin, Greek and Cyrillic.
- **Controls:** previous, play/pause, next, mute, and like (♥ on the cover).
- **Double-tap the cover** for **Playlists** (play one), **Devices** (move playback to your phone, computer or speaker) and **Volume**.
- **Setup without a computer:** the setup hotspot with a QR code; up to 3 Wi-Fi networks (home plus phone hotspot); a settings page at `http://cyd-spotify.local` with an optional password.
- **For the car:**
  - shows the last song straight after power-on
  - waits for a phone hotspot that comes up late, and tries the network that worked last first
  - survives power cuts
  - restarts itself through a watchdog if anything hangs
- **Idle:** a big clock (time zone on the setup page). The screen turns off after your chosen time, and a touch wakes it.

## Known limitations

- **Volume and mute don't work on phones.** Spotify doesn't let apps change a phone's volume. They work on computers, speakers and many cars.
- **2.4 GHz Wi-Fi only**, with WPA2 or WPA2/WPA3 mixed (not WPA3-only). Set your phone's hotspot to 2.4 GHz.
- **The Spotify login lasts 6 months** (Spotify's limit); then log in again on the setup page.
- **Spotify development-mode apps** only work for the app owner and users added under *User Management*.
- **Half-width katakana** show as `?`.
- Boards sold as ESP32-2432S028R use one of two display chips. If the screen is blank or garbled, **hold a finger on it from power-on for 8 s** to switch.

## Tested on

An ESP32-2432S028R (ESP32-D0WD-V3, CH340 USB), with a Samsung Galaxy S20 FE and a MacBook as Spotify devices, on home Wi-Fi and a phone hotspot:
- 100 automatic track changes with no crash or memory leak
- 30 minutes of playback with stable memory
- repeated power cuts
- the hotspot coming up 60 s after power-on
- set up from scratch using the README

Details per stage are in [docs/](../docs/README.md).
