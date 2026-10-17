# Stage 4 — Controls

> **Since then (as of 1.0.0):**
> - The 5-button row became design B's column: previous / play-pause / next / **mute**.
> - The heart moved to the cover's corner.
> - **Volume** moved to the double-tap cover menu, beside **Playlists** and **Devices**.
> - Mute sets the volume to 0 and back, on devices that allow volume.

Firmware `0.4.0-stage4`.

## How it works (src/spotify/control.*, src/app/nowplaying.*)

- **Controls row:** five 64×48 buttons along the bottom: previous, play/pause (green), next, like (red heart when liked, outline when not, grey when unknown), and volume (blue, grey when the device refuses volume).
- **Taps never wait on the network.** The UI queues a command and wakes the Spotify task. Play/pause, like and volume are shown at once, and a confirmation poll about 0.9 s later corrects them if Spotify disagrees. After next/previous it polls again a second later if the track hasn't changed yet.
- **Endpoints**, checked in October 2026: `PUT /me/player/play|pause`, `POST /me/player/next|previous`, `PUT /me/player/volume?volume_percent=N`. Likes use the 2026 library routes: `GET /me/library/contains?uris=…` once per new track, then `PUT`/`DELETE /me/library?uris=…`.
- These requests have no body, so the device sends `Content-Length: 0` itself (`HTTPClient` doesn't, and Spotify answers 411 without one).
- **Volume overlay:** large − and + buttons in steps of 10. Taps add up and are sent once they pause for 350 ms. The overlay closes 3 s after the last touch, or on a tap outside it.
- **Errors in plain language** come from the error body's `reason`. They show in the top strip for 4 s:

| Situation | Message |
|---|---|
| 404 / `NO_ACTIVE_DEVICE` | Open Spotify on your phone |
| `PREMIUM_REQUIRED` | Spotify Premium required |
| `VOLUME_CONTROL_DISALLOW`, or a 403 on volume | This device doesn't allow volume control |
| 429 | Rate limited, retrying in N s |
| no network | No connection to Spotify |

- A 403 on one command no longer sets the account-wide "Premium required" error. Only `PREMIUM_REQUIRED`, or a 403 on the playback-state poll, does.

## Results (owner's board, Galaxy S20 FE)

- [x] Play/pause: 200 in 330–540 ms; the screen flips at once
- [x] Next / previous: 200 in 240–360 ms; the new track appears 1.1 s into it
- [x] Like / unlike: 200 in ~125 ms; the heart matches "Liked Songs" on the phone
- [x] Nothing freezes while a request is in flight
- [x] **Volume:** Spotify reports the Galaxy as `supports_volume: false`. Spotify doesn't allow remote volume on phones, so no app can do this. The button is grey, and a tap says "Spotify can't set volume here - use the phone or car knob" (that message takes effect with the next flash). The overlay itself hasn't been tested on a device that allows volume (desktop app, web player, speakers, many cars).
- [ ] "Open Spotify on your phone" for no active device: not seen in the logs yet

## Fixed

- **Rapid polling near the end of a track.** Spotify can report the position at or past the track's length for a while, and the "poll just after the track ends" rule then polled every ~0.8 s (9 polls in 10 s). That early poll now happens once per track, at least 1.5 s out, and is logged.
