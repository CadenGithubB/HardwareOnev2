#pragma once
#include <Arduino.h>
extern "C" {
  #include "esp_heap_caps.h"
}

inline bool hasPSRAMAvail() {
#if defined(BOARD_HAS_PSRAM) || defined(CONFIG_SPIRAM)
  return true;
#else
  return false;
#endif
}

inline void* ps_try_malloc(size_t size) {
  if (hasPSRAMAvail()) {
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (p) return p;
  }
  return malloc(size);
}

inline void* ps_try_calloc(size_t n, size_t size) {
  if (hasPSRAMAvail()) {
    void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM);
    if (p) return p;
  }
  return calloc(n, size);
}

inline void* ps_try_realloc(void* ptr, size_t size) {
  if (hasPSRAMAvail()) {
    void* p = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM);
    if (p) return p;
  }
  return realloc(ptr, size);
}
