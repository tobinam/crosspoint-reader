#pragma once

#include <cstdint>
#include <string>

namespace HalSystem {
struct StackFrame {
  uint32_t sp;
  uint32_t spp[8];
};

void begin();

// Dump panic info to SD card if necessary
void checkPanic();
void clearPanic();

std::string getPanicInfo(bool full = false);
bool isRebootFromPanic();
// Why the chip started this time (esp_reset_reason()), e.g. "DEEPSLEEP" after a wake, "POWERON" after power loss.
const char* resetReason();
}  // namespace HalSystem
