#include "log.h"

RingLog Log;

static const size_t RING = 4096;
static char s_ring[RING];
static size_t s_head = 0;  // next write position
static bool s_wrapped = false;
static bool s_lineStart = true;

static void put(char c) {
  s_ring[s_head++] = c;
  if (s_head == RING) {
    s_head = 0;
    s_wrapped = true;
  }
}

size_t RingLog::write(uint8_t c) {
  if (s_lineStart && c != '\n') {
    // "[  123.456] " -- seconds since boot, so a log read later has a timeline.
    char stamp[16];
    const uint32_t ms = millis();
    snprintf(stamp, sizeof(stamp), "[%6lu.%03lu] ", (unsigned long)(ms / 1000), (unsigned long)(ms % 1000));
    Serial.print(stamp);
    for (const char* p = stamp; *p; p++) put(*p);
    s_lineStart = false;
  }
  Serial.write(c);
  put((char)c);
  if (c == '\n') s_lineStart = true;
  return 1;
}

size_t RingLog::write(const uint8_t* buf, size_t n) {
  for (size_t i = 0; i < n; i++) write(buf[i]);
  return n;
}

String RingLog::recent() const {
  String out;
  out.reserve(RING + 64);
  if (s_wrapped) {
    // Start after the first newline: the oldest line is cut off at the front.
    size_t i = s_head;
    while (i < RING && s_ring[i] != '\n') i++;
    for (i = i + 1; i < RING; i++) out += s_ring[i];
  }
  for (size_t i = 0; i < s_head; i++) out += s_ring[i];
  return out;
}
