#include "fs.h"
#include <LittleFS.h>
#include "../util/log.h"

namespace storage {

bool mount() {
  if (!LittleFS.begin(true /* formatOnFail */)) {
    LOGF("fs: LittleFS mount FAILED even after format");
    return false;
  }
  LOGF("fs: LittleFS mounted, total=%u used=%u", LittleFS.totalBytes(), LittleFS.usedBytes());
  return true;
}

bool fsSelfTest() {
  if (!mount()) return false;

  const char* path = "/stage0.txt";
  uint32_t count = 0;
  if (File f = LittleFS.open(path, "r")) {
    count = f.parseInt();
    f.close();
  }
  count++;
  if (File f = LittleFS.open(path, "w")) {
    f.print(count);
    f.close();
  } else {
    LOGF("fs: write FAILED");
    return false;
  }
  uint32_t readBack = 0;
  if (File f = LittleFS.open(path, "r")) {
    readBack = f.parseInt();
    f.close();
  }
  bool ok = readBack == count;
  LOGF("fs: write/read %s (boot #%u)", ok ? "OK" : "MISMATCH", count);
  return ok;
}

}  // namespace storage
