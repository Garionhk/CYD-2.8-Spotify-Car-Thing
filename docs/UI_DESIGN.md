# Now Playing screen: design B, revision 3

> **Since then (as of 1.0.0):**
> - With the Noto fonts the text rows over the cover sit slightly higher and are taller: title 20 px, artist and times 15 px lines, starting at cover row 160.
> - The horizontal margins are even: 6 | cover | 5 | buttons | 6.
> - The idle clock is 90% of the screen width.

Firmware `0.7.1-designB`; menu and pickers as of `0.8.0`. Render: `docs/mockups/B3_big_cover.png`. The owner approved it on the device: "looks good and readable".

## Layout (320×240)

- **Cover, 225×225**, top left. The 300 px image is decoded 1:1 and every 4th pixel and row is dropped (TJpgDec only scales by ½ steps, and a ½ decode would give 150 px). A cover only available at 64 px keeps the placeholder rather than being scaled up.
- **Text over the cover's bottom**, which darkens to 30% brightness over its last 100 rows:
  - title in 16 px (scrolls when long)
  - artist in 12 px
  - `1:08 ━━━━ 4:34` in 12 px
  - Every glyph has a 1 px black shadow (right, down and diagonal).
- **Heart:** in its own dark circle on the cover's top-right corner.
- **Button column, 78 px wide, on the right:** previous (52 px tall), play/pause (60, green), next (52), mute (52). The touch zones fill the gaps between buttons.
- **Double-tap the cover** to open the menu: Playlists, Devices, Volume ("not here" on devices that refuse volume), Close. A single tap does nothing.
- **Pickers** (`src/app/list_screen.*`, with sources in `src/app/lists.cpp`): Back, four big rows, ▲/▼ page buttons, and a 30 s timeout.
  - **Playlists:** `GET /me/playlists`, 50 per page; a tap plays with `PUT /me/player/play {"context_uri"}`.
  - **Devices:** `GET /me/player/devices`, fetched fresh each time; a tap moves playback with `PUT /me/player {"device_ids":[id],"play":true}`. "Playing here" is shown in green; restricted devices are dimmed.
- **Messages** appear as a pill across the middle for 4 s.

## How text sits on the picture without a framebuffer

While the cover is decoded, the device:
- blends the artist into the pixels (it doesn't change until the next track);
- copies the title line (7.2 KB) and the two time areas (1 KB each).

The scrolling title and the clock are then recomposed from those copies. The progress bar, heart and buttons are solid shapes that only repaint their own pixels.

## Fonts (LittleFS)

| File | Size | Used for |
|---|---|---|
| `b16.cydf` | 693 KB | title, playlist rows, everything else |
| `s12.cydf` | 443 KB | artist and times over the cover |

Both cover Chinese, Japanese and Korean. Typographic punctuation (’ “ ” – …) comes from Helvetica, because STHeiti drew "She’s" as "She  s". The 22 px font was dropped, which frees 1.18 MB of the 2.375 MB partition for art.

## Designs tried along the way

| | Design | Outcome |
|---|---|---|
| A | Today's layout with a bigger button row | not chosen |
| C | Album-colour background, small cover | not chosen |
| D | Full-screen cover behind everything | built (`0.6.x`), then replaced by B |
| B | Car Thing: cover left, buttons right | B1 had a 150 px cover and a 22 px title; revision 3 has the 225 px cover with text over it |

## Open

- **Mute** needs a device that accepts volume (Spotify has no mute endpoint). On the owner's Galaxy it shows a message. Falling back to pause there is still undecided.
- **Playlists** need one re-login (new scope `playlist-read-private`); not yet tested on the device.
- **Devices:** tested on the owner's board, moving playback MacBook Pro → Galaxy and back (204 each, confirmed by the next poll within ~2 s). The Galaxy is listed under its Connect name, "SM-G7810".
