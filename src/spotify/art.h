#pragma once
#include <Arduino.h>

// Album art (plan §5.4 / Stage 5). When the track's image URL changes, the Spotify task streams
// the JPEG into LittleFS -- never whole in RAM -- over plain HTTP on its own kept-alive client
// (the API's TLS keep-alive stays up; the image is public; HTTPS on the shared TLS client is the
// fallback), checks TJpgDec can read it, and swaps it in as /art/cur.jpg. The UI decodes it
// straight to the screen at the largest 1/1, 1/2, 1/4 or 1/8 scale that fits 150x150 (the
// ~300 px image at 1/2). The URL is kept with it, so the next track from the same album
// downloads nothing.
namespace art {

constexpr int16_t MAX_SIDE = 150;

// Reads which cover is cached (boot, before the screen's first draw: fast boot shows it at once).
void begin();

// Spotify task only: fetch the art for this URL if it is not the cached one.
void tick(const String& url, const String& fallbackUrl);

// The URL of the image now in /art/cur.jpg (empty if none), and a counter for redraws.
String readyUrl();
uint32_t version();

// UI task: decode the cached cover at the largest 1/1..1/8 scale that fits maxW x maxH, centred
// on (cx, cy). fn gets each block as TJpgDec makes it (RGB565, byte-swapped for pushImage) and
// draws it itself -- design D darkens it, blends text in and keeps copies first. False if none.
using BlockFn = bool (*)(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);
bool decode(int16_t cx, int16_t cy, int16_t maxW, int16_t maxH, BlockFn fn);

// UI task: the size the cached cover decodes to within maxW x maxH (0 if none).
bool decodedSize(int16_t maxW, int16_t maxH, int16_t& w, int16_t& h);

}  // namespace art
