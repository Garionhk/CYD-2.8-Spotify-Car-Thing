#include "clock.h"
#include <time.h>
#include "log.h"

namespace clockutil {

const Zone ZONES[] = {
    {"Vancouver / Los Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"Edmonton / Denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"Phoenix", "MST7"},
    {"Winnipeg / Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"Toronto / New York", "EST5EDT,M3.2.0,M11.1.0"},
    {"Halifax", "AST4ADT,M3.2.0,M11.1.0"},
    {"St. John's", "NST3:30NDT,M3.2.0,M11.1.0"},
    {"Honolulu", "HST10"},
    {"London / Dublin", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Paris / Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Hong Kong", "HKT-8"},
    {"Taipei / Shanghai / Singapore", "CST-8"},
    {"Tokyo", "JST-9"},
    {"Seoul", "KST-9"},
    {"Sydney / Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"UTC", "UTC0"},
};
const int ZONE_COUNT = sizeof(ZONES) / sizeof(ZONES[0]);

static String s_tz = "UTC0";

void setZone(const String& posixTz) {
  s_tz = posixTz.length() ? posixTz : String("UTC0");
  setenv("TZ", s_tz.c_str(), 1);
  tzset();
}

void sync() {
  configTzTime(s_tz.c_str(), "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  LOGF("clock: NTP started (%s)", s_tz.c_str());
}

bool synced() { return time(nullptr) > 1700000000; }  // set at all (after 2023)

static bool now(struct tm& t) {
  if (!synced()) return false;
  const time_t s = time(nullptr);
  localtime_r(&s, &t);
  return true;
}

String hhmm() {
  struct tm t;
  if (!now(t)) return String();
  char b[8];
  strftime(b, sizeof(b), "%H:%M", &t);
  return b;
}

String dateLine() {
  struct tm t;
  if (!now(t)) return String();
  char b[24];
  strftime(b, sizeof(b), "%a %d %b", &t);
  return b;
}

int minuteOfDay() {
  struct tm t;
  if (!now(t)) return -1;
  return t.tm_hour * 60 + t.tm_min;
}

}  // namespace clockutil
