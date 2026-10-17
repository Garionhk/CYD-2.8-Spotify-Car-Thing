#pragma once
#include <Arduino.h>

// Wall-clock time for the idle screen: NTP once Wi-Fi is up, shown in the owner's time zone
// (a POSIX TZ string from the setup page, e.g. "PST8PDT,M3.2.0,M11.1.0" for Vancouver).
namespace clockutil {

void setZone(const String& posixTz);  // applies at once (no network needed)
void sync();                          // start NTP (call when Wi-Fi joins)
bool synced();                        // the clock has been set since boot

String hhmm();      // "14:05"
String dateLine();  // "Thu 16 Oct"
int minuteOfDay();  // changes once a minute: for redraws

// The time zones offered on the setup page: label, POSIX TZ.
struct Zone {
  const char* label;
  const char* tz;
};
extern const Zone ZONES[];
extern const int ZONE_COUNT;

}  // namespace clockutil
