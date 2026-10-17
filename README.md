# CYD Spotify Remote

A "Car Thing"-style Spotify **now-playing screen and remote** for the 2.8" ESP32 **Cheap Yellow Display** (ESP32-2432S028R). It shows the cover, title and artist, and has big buttons for previous / play-pause / next / mute, plus a heart to like the song. Double-tap the cover for **Playlists**, **Devices** and **Volume**.

The CYD plays no audio itself. It remote-controls Spotify on your phone (or computer or speaker) over Wi-Fi, which makes it a dashboard remote in the car, using your phone's hotspot.

- No soldering. No passwords compiled in: everything is set up on the device.
- Titles in Chinese, Japanese, Korean, Latin, Greek and Cyrillic.
- Built for the car: it shows the last song before Wi-Fi is back, waits for a hotspot that comes up late, survives power cuts, and turns the screen off when idle.

**Needs Spotify Premium** (Spotify only allows remote control for Premium accounts).

Current version: **1.0.0**. Design notes and test results per stage: [docs/README.md](docs/README.md).

---

## 1. Install

Download the image: **[release/cyd-spotify-v1.0.0.bin](release/cyd-spotify-v1.0.0.bin)** (4 MB). It's the whole flash in one file (app, fonts and partition table), written at address **0x0**.

Installing erases the board: Wi-Fi, the Spotify login and the touch calibration are set up again afterwards.

### From the browser: esptool.spacehuhn.com (no tools to install)

1. Open **https://esptool.spacehuhn.com/** in **Chrome or Edge on a computer** (it uses Web Serial; Safari, Firefox and phones can't).
2. Plug the CYD in with a USB **data** cable (some cables only charge).
3. Set the baud rate to **115200** and click **Connect**. Pick the serial port (often "USB Serial" or "CH340").
4. In the file row, set the offset to **0x0** and choose `cyd-spotify-v1.0.0.bin`.
5. Click **Program** and wait for it to finish (a few minutes at 115200).
6. Unplug the CYD and plug it back in (or press its **RST** button).

### With esptool (command line)

```bash
esptool.py --chip esp32 --baud 115200 write_flash 0x0 cyd-spotify-v1.0.0.bin
```

Use 115200 baud in both cases: the CYD's CH340 USB chip often fails at 921600.

### Screen blank, white or garbled?

Boards sold as ESP32-2432S028R carry one of two display chips (ST7789 or ILI9341). **Hold a finger on the screen from power-on for 8 seconds**: the blue LED blinks from 4 s, then it switches to the other chip and restarts. Colours inverted (black looks white)? Change **Colours** on the setup page later.

---

## 2. First-time setup

1. **Touch calibration** runs by itself. Press and hold each target, then tap the centre. (Redo it later: hold **BOOT** 5 s to open the setup screen, then tap **Calibrate**.)
2. The screen shows **CYD-Spotify-XXXX** with a QR code. Scan it (or join that Wi-Fi) with your phone, and the setup page opens by itself. If it doesn't, browse to **http://192.168.4.1**.
3. Under **Wi-Fi**, add up to three networks, for example your home Wi-Fi and your **phone's hotspot**. **Scan** lists what's in range. Tap **Save**; the CYD restarts and joins.
   - **2.4 GHz only.** The ESP32 cannot see 5 GHz networks. On a Samsung phone: *Settings → Connections → Mobile Hotspot → Configure → Band: 2.4 GHz*.
   - **WPA2** (WPA2/WPA3 mixed works too; WPA3-only does not).
   - Mistyped a password? The screen says **"Wrong password for …"** and the setup hotspot opens again by itself.
4. Set a **setup page password** on the same page (user `admin`). Until you do, anyone on your Wi-Fi can open it.

### Create your Spotify app (once, about 3 minutes)

Spotify needs every remote control to have its own "app" registration:

1. Go to **https://developer.spotify.com/dashboard** and log in with your Spotify (Premium) account.
2. Tap **Create app**. Name and description can be anything.
3. **Redirect URI:** enter exactly `http://127.0.0.1:8888/callback` and tap **Add**.
4. Under "Which API/SDKs are you planning to use?", tick **Web API**. Accept the terms and save.
5. Open the app's **Settings** and copy the **Client ID**.
6. If someone else's Spotify account will use the CYD, add them under **User Management** (Development-mode apps only work for the owner and listed users).

### Log in

1. On your home Wi-Fi, open **http://cyd-spotify.local**, or **scan the QR code on the CYD**, which opens the page at its Spotify section. If `.local` doesn't load, use the IP address shown on the CYD.
2. In **Spotify**, paste the **Client ID** and tap **Save**.
3. Tap **Log in with Spotify** (it opens a new tab) and **Agree**.
4. That tab ends on an error page at **127.0.0.1**. That's expected. Copy the **whole address** from its address bar (it starts `http://127.0.0.1:8888/callback?code=`), paste it into the box on the setup page, and tap **Finish**.

The login lasts **6 months** (Spotify's limit). After that the screen says "Login expired"; repeat these steps.

---

## 3. Using it

| | |
|---|---|
| Right column | previous · play/pause · next · mute |
| Heart (top right of the cover) | like / unlike the song |
| **Double-tap the cover** | menu: **Playlists** (your playlists, tap to play), **Devices** (move playback to your phone, computer or speaker), **Volume** |
| Touch the screen | if music started on the phone, it shows at once (and the screen wakes if it was off) |
| Nothing playing | a big clock and the date (time zone on the setup page) |
| Starting up | the last song from before power-off, with "Waiting for Wi-Fi..." until it's connected |

**Volume and mute only work on devices that allow it.** Spotify does not let any app change a **phone's** volume. On a phone, use the phone's or the car's volume; the CYD tells you so. Computers, speakers and many cars allow it.

### Setup page (http://cyd-spotify.local)

| Setting | |
|---|---|
| Wi-Fi | up to 3 networks. The one that worked last is tried first, so in the car that's the hotspot |
| Brightness, Colours | apply instantly |
| Panel controller | ST7789 or ILI9341: change it if the screen looks wrong (or use the 8 s finger hold at power-on) |
| Orientation | Normal or Upside down |
| Screen off when idle | Never / 1 / 5 / 15 / 30 min; the backlight turns off while nothing plays or playback is paused |
| Time zone | for the idle clock |
| Spotify updates | Normal, or Data saver (half as many requests) |
| Setup page password | user `admin`; set one so others on the Wi-Fi can't change your settings |
| Recent log | at the bottom; useful when something's wrong |

Hold the **BOOT** button for 5 s to open the setup hotspot again (for example to add a network away from home).

---

## 4. In the car

- **Power:** any USB port or car charger that gives a steady **5 V, 1 A or more**. The CYD restarts whenever the power cuts out; that's fine. A weak port or a long, thin cable can make it reset or drop off USB.
- **Start-up:** it shows the last song straight away, then joins your phone's hotspot when it appears (it keeps trying), usually within 10 s of the hotspot coming up.
- **Mounting:** keep it **out of direct sun**. Heat and glare are hard on the display, and a resistive touchscreen gets hard to read in sunlight. Mount it where you can reach it without stretching; the buttons are big for that reason.
- **Data:** while playing it checks Spotify every 4 s (a few KB each). A cover is about 35 KB per new album. **Data saver** halves the checks.

---

## 5. Troubleshooting

| Problem | What to do |
|---|---|
| Blank, white or garbled screen | Hold a finger on the screen from power-on for 8 s (switches display chip) |
| Colours inverted | Setup page → Colours |
| Touches land in the wrong place | Hold **BOOT** 5 s, then **Calibrate** on the setup screen |
| Won't join the hotspot | Hotspot band **2.4 GHz** and **WPA2**; check the password; the phone's hotspot must be on |
| "Wrong password for …" | A mistyped password from setup: the setup hotspot **opens by itself**, so join it and fix the password. For a network that worked before (its password changed), hold **BOOT** 5 s to open setup |
| Stuck on "Waiting for Wi-Fi" at home | Is the network 2.4 GHz? Is it in the list? Check **Recent log** for "timed out" (out of range) |
| `cyd-spotify.local` doesn't open | Use the IP address shown on the CYD (some Android phones don't support `.local`) |
| "Open Spotify on your phone" | Spotify needs an active device: open Spotify and start any song once |
| "Spotify refused access…" / 403 | The app owner needs **Premium**; other users must be added under **User Management** |
| "Login expired" | Log in again (section 2) |
| Login says the redirect URI is wrong | It must be exactly `http://127.0.0.1:8888/callback` in your Spotify app |
| Playlists: "Log in again" | Your login is from before playlists were added: log in once more (section 2) |
| A device says "can't be controlled" | Spotify blocks remote control of some speakers and TVs |
| Titles show `?` | Characters outside the built-in set (for example half-width katakana) |
| Volume / mute "can't" | The device is a phone: Spotify doesn't allow remote volume on phones |
| Restarts by itself | Check **Recent log**: the first lines give the reason (power-on, WATCHDOG, BROWNOUT…). BROWNOUT means weak power: try another port or cable |

---

## 6. Updating

- **Reinstall** (esptool.spacehuhn.com or esptool, as in section 1): erases everything, so you set it up again.
- **From source with PlatformIO** (`pio run -e cyd -t upload`): keeps Wi-Fi, login, calibration and settings. Add `-t uploadfs` only when the fonts changed; that also clears the cached cover.

---

## 7. Building from source

PlatformIO, Arduino framework (Arduino-ESP32 2.0.17), TFT_eSPI configured in `platformio.ini`.

```bash
pio run -e cyd -t upload        # the app
pio run -e cyd -t uploadfs      # the fonts (LittleFS)
./tools/make_release.sh         # release/cyd-spotify-v<version>.bin
```

| Env | |
|---|---|
| `cyd` | the app (release build) |
| `cyd_test` | the app plus test endpoints (`/spotify/test-skips`, `/spotify/test-expiry`) |
| `hwtest` | the Stage 0 hardware test screen |

Fonts: `python3 tools/gen_font.py` builds `fs/fonts/*.cydf` from **Noto Sans CJK** and **Noto Sans** (download commands at the top of the script). `tools/gen_ca_bundle.py` builds the TLS root-certificate bundle.

`./tools/make_release.sh` builds the release image from the `cyd` env and checks that the test endpoints are not in it.

Design notes, measurements and decisions per stage: [docs/README.md](docs/README.md).

---

## Credits and licences

- This project: [Apache License 2.0](LICENSE-2.0.txt).
- Fonts: [Noto Sans CJK](https://github.com/notofonts/noto-cjk) and [Noto Sans](https://github.com/notofonts/latin-greek-cyrillic), SIL Open Font License 1.1.
- Libraries: TFT_eSPI and TJpg_Decoder (Bodmer), ArduinoJson (Benoît Blanchon), Arduino-ESP32 (Espressif).
- Spotify is a trademark of Spotify AB. This project is not affiliated with or endorsed by Spotify.
