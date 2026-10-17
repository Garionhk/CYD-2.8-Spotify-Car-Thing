# Docs

How the CYD Spotify Remote was built, stage by stage, with the measurements and decisions behind it. To install and use it, see the [main README](../README.md).

Each stage doc records what was true **at that stage**. A **"Since then"** note at its top lists what changed later.

| Doc | Covers | Status |
|---|---|---|
| [STAGE0.md](STAGE0.md) | Hardware check: display, touch, LED, BOOT, LittleFS, heap | done (`hwtest` env) |
| [STAGE2.md](STAGE2.md) | Stages 1–2: Wi-Fi provisioning (setup hotspot, 3 networks) and Spotify login (PKCE, refresh, 6-month expiry) | done |
| [STAGE3.md](STAGE3.md) | Now-playing data (polling, streamed JSON) and the fonts for any script | done |
| [STAGE4.md](STAGE4.md) | Controls: play/pause, next/previous, like, volume, error messages | done |
| [STAGE5.md](STAGE5.md) | Album art: plain-HTTP download, LittleFS cache, 100-skip endurance test | done |
| [STAGE6.md](STAGE6.md) | Car hardening: fast boot, power-cut-safe writes, watchdog, idle clock, screen-off, Wi-Fi fixes | done |
| [UI_DESIGN.md](UI_DESIGN.md) | The Now Playing screen (design B rev. 3), menu, pickers, designs tried | done |
| Stage 7 | Optional extras: rotary encoder, night theme (the device picker is already done) | skipped for now |
| Stage 8 | Release: below | done |

## Stage 8 — Release (1.0.0)

- **Fonts:** rebuilt from **Noto Sans CJK TC** and **Noto Sans** (SIL Open Font License 1.1), so the image can be shared. Line heights now come from the glyphs' real ink (Noto declares 24 px lines for 16 px text). The Apple set remains available as `tools/gen_font.py --faces apple` for your own device only.
- **Release build:** env `cyd`, with the test endpoints compiled out (`CYD_TEST_HOOKS` is only set in `cyd_test`). Version in `src/config.h`.
- **Image:** `./tools/make_release.sh` writes `release/cyd-spotify-v<version>.bin`, a full 4 MB to flash at offset 0:

  | Offset | Content |
  |---|---|
  | 0x1000 | bootloader |
  | 0x8000 | partitions |
  | 0xE000 | boot_app0 |
  | 0x10000 | app |
  | 0x190000 | LittleFS fonts |

  It fills the whole flash, so installing it is a factory reset.
- **Flashing:** the image is written at 0x0 with [esptool.spacehuhn.com](https://esptool.spacehuhn.com/) (browser) or `esptool.py` (command line), both at 115200 baud. An ESP Web Tools installer page was drafted but not tested, so it isn't published.
- **README:** install (esptool.spacehuhn.com / esptool), setup, creating the Spotify app, use, the car, troubleshooting, updating, building.
- **Found during the end-to-end test:** a wrong Wi-Fi password kept the device "joining" forever. It's now detected at once (see `STAGE6.md`).

## Other files

- `mockups/`: the four layout designs (A–D) and the revisions that led to the current screen.
- `font_preview.png`: the Noto fonts rendered off-device from their files (Chinese, Japanese, Korean, accents, Cyrillic).
