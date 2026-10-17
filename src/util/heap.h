#pragma once
#include <Arduino.h>

struct HeapInfo {
  uint32_t free;
  uint32_t maxAlloc;  // largest free block; keep > 40KB in steady state
  uint32_t minFree;   // low-water mark since boot
};

inline HeapInfo heapInfo() {
  return {ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap()};
}

void logHeap(const char* tag);
