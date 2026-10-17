#pragma once
#include <Arduino.h>

// The serial log, also kept in a 4 KB ring and served at http://cyd-spotify.local/log
// (as the CYD Stream Deck). This board restarts whenever its USB serial port is opened,
// and drops off USB now and then, so the copy over Wi-Fi is the reliable one.
class RingLog : public Print {
 public:
  size_t write(uint8_t c) override;
  size_t write(const uint8_t* buf, size_t n) override;
  using Print::write;
  String recent() const;  // oldest first, each line prefixed with uptime
};

extern RingLog Log;

#define LOGF(fmt, ...) Log.printf(fmt "\n", ##__VA_ARGS__)
