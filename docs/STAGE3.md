# Stage 3 — Now-playing data and text UI

> **Since then (as of 1.0.0):**
> - **Polling** while paused or with nothing playing is 4 / 5 s for the first 2 minutes, then 8 / 10 s (it was 10 / 30 s), and a touch polls at once.
> - The **screen layout** below was replaced by design B (`UI_DESIGN.md`).
> - The **fonts** are now Noto (redistributable) in two sizes: `b16` (16 px, 20 px lines) and `s12` (12 px, 15 px lines). The 22 px font was dropped. They use 1.25 MB of LittleFS.

Firmware `0.3.0-stage3`. Flash both images; the fonts live in LittleFS:

```bash
pio run -e cyd -t upload
pio run -e cyd -t uploadfs
```

## Polling (src/spotify/player.*)

- `GET /me/player?additional_types=episode`, checked against Spotify's reference in October 2026. Every field used is still present.
- The response is parsed **straight off the TLS stream** through an ArduinoJson filter (`src/spotify/body_stream.h` handles chunked and Content-Length bodies, and drains what's left so the keep-alive connection can be reused). The full body is never held in RAM.
- **Intervals:** 4 s playing, 10 s paused, 30 s when nothing is active (204). The "Data saver" setting doubles each one.
- The device also polls just after the current track should end, and ~600 ms after a control action (`player::pollSoon()`, used from Stage 4). A 429 waits for `Retry-After`.
- The progress bar advances locally every 250 ms between polls.

## Screen (src/app/nowplaying.*)

The art placeholder (real album art comes in Stage 5) sits beside the title, which scrolls when long, then artist, album (dim), "on <device>", volume, and "Paused". Below them are the progress bar and `m:ss / m:ss`. When no track is active, an idle screen shows "Nothing playing". A status strip at the top shows Spotify problems in plain language plus the Wi-Fi strength.

## Fonts for any script (src/display/textfont.*, tools/gen_font.py)

Titles, artists, albums and device names render in Chinese, Japanese, Korean, accented Latin, Greek and Cyrillic: 10,424 glyphs at 22 px (title) and 16 px (body).

- **Faces:** STHeiti Medium for Chinese, Japanese and Latin (the MP3 player's face), Apple SD Gothic Neo Medium for Korean (STHeiti has no Hangul), and Helvetica for Greek and Cyrillic.
- **Characters:** Big5 level 1 + GB2312 level 1 (as the MP3 player), JIS X 0208 level 1 kanji, KS X 1001 Hangul and the Hangul letters, kana, and Latin-1/Extended-A. Half-width katakana are not covered (no face has them) and show as `?`.
- **No RAM per glyph:** the device binary-searches a sorted index on flash and caches 64 recent glyphs. The MP3 player's `.vlw` costs ~12 bytes of RAM per glyph, about 125 KB for this set, which isn't available once Wi-Fi and TLS are running.
- **2-bit alpha:** a string is rendered once into a packed strip (at most ~12 KB). The scrolling title copies a window of it each frame, without reading flash.
- **Fallback:** if the font files are missing, text falls back to the built-in ASCII fonts, with accents dropped.
- **Regenerating:** run `python3 tools/gen_font.py` (`--no-hangul` or `--no-japanese` to save space), then upload with `uploadfs`. Preview: `docs/font_preview.png`.
- **Licensing:** these are Apple fonts, fine on your own device. The public release (Stage 8) needs Noto Sans CJK (`--face`).

## Partitions

The table was changed for the fonts: the app slot went from 1.94 MB to **1.5 MB** (1.23 MB used), and LittleFS from 1.94 MB to **2.375 MB** (1.94 MB used by fonts, about 540 KB free for album art). NVS didn't move, so settings and the Spotify login were kept across the change.

## Acceptance (measured on the owner's board)

- [x] **Song change shown within 5 s:** skips were noticed 0.6–2.4 s after the track started. Over 30 minutes of playback, 9 natural track changes were noticed 0.2–2.0 s in.
- [x] **Heap stable over 30 minutes of playback** (420 polls): free 136–138 KB, largest block flat at 86 KB, lowest 123 KB. No errors or resets.
- [x] Chinese titles display correctly on the device (Ronald Cheng, 我們).
