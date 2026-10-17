#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# make_release.sh -- build the release image.
#
#   ./tools/make_release.sh
#
# Writes release/cyd-spotify-v<version>.bin: one image of bootloader, partition table,
# boot_app0, app and the LittleFS fonts, flashed at offset 0 -- with esptool.spacehuhn.com
# or `esptool.py write_flash 0x0 <file>`, both at 115200 baud. The version comes from
# FW_VERSION in src/config.h. Fonts: fs/fonts/*.cydf as tools/gen_font.py made them (Noto,
# redistributable -- never ship an image built with --faces apple).
#
# The image covers the whole 4 MB, settings area included: flashing it is a factory reset
# (Wi-Fi, Spotify login and calibration are erased).
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
PIO="${PIO:-$HOME/.platformio/penv/bin/pio}"
PY="${PY:-$HOME/.platformio/penv/bin/python}"
ESPTOOL="$HOME/.platformio/packages/tool-esptoolpy/esptool.py"
BOOT_APP0="$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"

VERSION=$(sed -n 's/^#define FW_VERSION "\(.*\)"/\1/p' src/config.h)
[ -n "$VERSION" ] || { echo "FW_VERSION not found in src/config.h" >&2; exit 1; }
grep -q "CYD_TEST_HOOKS" <(sed -n '/^\[env:cyd\]/,/^\[/p' platformio.ini) &&
  { echo "env:cyd has CYD_TEST_HOOKS: not a release build" >&2; exit 1; }

echo "== building $VERSION"
"$PIO" run -e cyd
"$PIO" run -e cyd -t buildfs

B=.pio/build/cyd
OUT="release/cyd-spotify-v$VERSION.bin"
mkdir -p release
"$PY" "$ESPTOOL" --chip esp32 merge_bin -o "$OUT" \
  --flash_mode dio --flash_freq 40m --flash_size 4MB --fill-flash-size 4MB \
  0x1000 "$B/bootloader.bin" \
  0x8000 "$B/partitions.bin" \
  0xe000 "$BOOT_APP0" \
  0x10000 "$B/firmware.bin" \
  0x190000 "$B/littlefs.bin"

echo
echo "== release $VERSION"
ls -la "$OUT"
shasum -a 256 "$OUT"
