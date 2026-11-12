#pragma once

#include <cstdint>

class DockSleepPolicy {
  uint32_t lastPoweredAt = 0;

 public:
  bool shouldSleep(uint32_t now, uint32_t lastActivityAt, uint32_t timeoutMs, bool externallyPowered) {
    if (externallyPowered) {
      lastPoweredAt = now;
      return false;
    }
    return timeoutMs > 0 && now - lastActivityAt >= timeoutMs && now - lastPoweredAt >= timeoutMs;
  }
};
