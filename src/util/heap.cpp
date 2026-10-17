#include "heap.h"
#include "log.h"

void logHeap(const char* tag) {
  HeapInfo h = heapInfo();
  LOGF("heap[%s] free=%u maxAlloc=%u minFree=%u", tag, h.free, h.maxAlloc, h.minFree);
}
