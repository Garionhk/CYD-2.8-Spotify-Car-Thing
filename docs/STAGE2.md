# Stages 1–2 — Wi-Fi provisioning and Spotify login

> **Since then (as of 1.0.0):**
> - Each Wi-Fi attempt now gets **8 s** instead of 15.
> - The **last network that worked is tried first**.
> - A **wrong password** is recognised at once: the screen says so, and the setup hotspot opens for a network never joined.
> - Two retry bugs found by the hotspot test are fixed (see `STAGE6.md`).
> - `/spotify/test-expiry` is only in the `cyd_test` build.
> - The login asks for two more scopes (`playlist-read-private`, `playlist-read-collaborative`) for the playlist picker.

Firmware `0.2.0-stage2`. Build and flash with `pio run -e cyd -t upload`. The Stage 0 hardware test is `pio run -e hwtest -t upload`.

## Wi-Fi (src/net/portal.*)

This follows the CYD Stream Deck's portal: an open hotspot with a captive DNS, one setup page, an `admin` login once a password is set, mDNS, and the log at `/log`. Three changes for the car, from the build plan:

| | Stream Deck | This project |
|---|---|---|
| Saved networks | 1 | 3, tried in order (home Wi-Fi, phone hotspot, …) |
| Join fails | hotspot comes up after 20 s | keeps retrying in the background, no hotspot |
| Hotspot | no network saved, or join failure | no network saved, or **BOOT held 5 s** |
| Address | `cyd-xxxxxx.local` | `cyd-spotify.local` |

**Scan fallback:** on this board, scans sometimes come back empty with the router right there. When a scan sees none of the saved networks, the device tries each one by name. That also covers hidden networks and a phone hotspot that hasn't appeared in scans yet.

## Spotify login (src/spotify/auth.*)

Authorization Code with PKCE. There is no client secret on the device. Checked against Spotify's docs in October 2026:

- **Redirect URI:** `http://127.0.0.1:8888/callback`. Plain HTTP is only allowed to a loopback IP literal, and `localhost` is refused.
- **Scopes:** `user-read-playback-state user-modify-playback-state user-read-currently-playing user-library-read user-library-modify`. The new `/me/library` routes for saving tracks use the library scopes.
- **Refresh:** the device refreshes 5 minutes before expiry, and again on any 401. The refresh request includes `client_id`. A response may omit `refresh_token`; if so, the old one is kept.
- **Refresh tokens expire 6 months after login**, and refreshing doesn't extend that. On `invalid_grant` the device drops the token and shows "Login expired".
- **TLS:** the certificate is verified against `data/x509_crt_bundle.bin`, 155 roots from macOS, built with `tools/gen_ca_bundle.py`. Rebuild the bundle when you refresh the roots.
- Network calls run in their own FreeRTOS task (12 KB stack), so a handshake never blocks touch or the web page. One TLS client is used, for one host at a time.

### Steps (as on the setup page)

1. On developer.spotify.com/dashboard, tap **Create app**. Set the Redirect URI to the one above and tick **Web API**. The app owner needs Premium.
2. Paste the app's **Client ID** into the setup page.
3. Tap **Log in with Spotify** and agree in the new tab.
4. That tab ends on an error page at 127.0.0.1, which is expected. Copy the whole address and paste it back into the setup page.

## Acceptance (measured on the owner's board)

- [x] Stage 1: set up from a phone on the hotspot. After the restart, the device joins without the portal.
- [x] Login: code exchanged, access token valid 3599 s
- [x] Power cycle → authorized call with no user action: join at 7.4 s, refresh at 9.2 s, `GET /me/player` → 204 at 10.1 s
- [x] Forced expiry (`POST /spotify/test-expiry`): 401 → refresh → 204 within 2.5 s
- [ ] Stage 1: the hotspot switched off and on, recovered without a reboot (not tested; you chose to move on)

Heap with Wi-Fi and the TLS client: about 168 KB free in steady state, 144 KB at the lowest. The largest block dropped from 110 KB to 94 KB after a few handshakes, which is still well above the 40 KB target.

## Fixed along the way

- A **204 response with no body blocked the Spotify task**: `HTTPClient` waited on the keep-alive connection until Spotify closed it. The body is now read only when the response announces one. Stage 3 polls will hit 204 whenever nothing is playing.
