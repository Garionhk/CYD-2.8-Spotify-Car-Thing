# Stage 5 — Album art

> **Since then (as of 1.0.0):**
> - The cover is shown at **225×225**: the 300 px image decoded 1:1, with every 4th pixel and row dropped. Title, artist and times are drawn over its darkened bottom.
> - The cover swap became power-cut safe (`STAGE6.md`).
> - `/spotify/test-skips` is only in the `cyd_test` build.

Firmware `0.5.0-stage5` (firmware only; the fonts in LittleFS are unchanged).

## How it works (src/spotify/art.*)

1. When the track's image URL changes, the Spotify task downloads the JPEG and **streams it into LittleFS** (`/art/dl.jpg`, written in 4 KB chunks), so it is never held whole in RAM.
   - It uses **plain HTTP to the image CDN**, on its own kept-alive client. Spotify's image hosts (`i.scdn.co`, `image-cdn-*.spotifycdn.com`) serve the same JPEG over HTTP with no redirect. Covers are public, so nothing private crosses the network, and there's no second TLS session (plan §4). The API's TLS keep-alive is left alone, and later covers reuse the CDN connection (headers in ~160 ms instead of ~450 ms).
   - **HTTPS fallback:** if the CDN ever refuses HTTP, it uses the shared TLS client, closing the API keep-alive first.
2. It checks that TJpgDec can read the file (baseline JPEG; Spotify's covers are baseline), then **renames it to `/art/cur.jpg`** and stores the URL in `/art/cur.url`.
   - The lock is held only for that rename, never during a download, so drawing never waits on the network.
3. The UI **decodes `cur.jpg` straight to the screen** at the largest 1/1, 1/2, 1/4 or 1/8 scale that fits 150×150: the ~300 px cover at 1/2, in about 200 ms.
   - **Fallback:** if there's no ~300 px image, or it fails, the 64 px one is drawn centred on the placeholder.
   - **Cache by URL:** the next track from the same album downloads nothing.

The screen never shows the previous album's cover under a new title. Until the new cover is ready, the placeholder shows.

### Rejected along the way

| Tried | Why not |
|---|---|
| Decode into a 45 KB RAM buffer, store raw pixels | The largest free block fell to 43 KB during the decode and stayed lower afterwards (fragmentation) |
| Decode in 16-row bands to a raw-pixel file | No big allocation, but ~0.75 s of extra flash writes per cover |
| `HTTPClient::writeToStream()` | ~1.4 KB writes; a 30 KB body took ~0.9 s to save |

## Acceptance (owner's board, 100 automatic skips: `POST /spotify/test-skips?n=100&ms=6000`)

- [x] **Art on track change:** from detection to the cover on screen, median 1.30 s, 90th percentile 1.59 s, max 2.20 s. Detection adds ~1.1 s (median) after the actual skip, so from the tap on the phone it's about 2.4 s.
- [x] **100 track changes, no leak, no crash:** 102 tracks, 102 covers drawn, no failures, one continuous boot (720 s). Free heap mid-test 125.3 KB → end 125.4 KB; largest block flat at 69.6 KB; lowest 94 KB.

Downloads: median 0.72 s, max 1.29 s.
