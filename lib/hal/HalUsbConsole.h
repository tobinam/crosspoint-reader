#pragma once

#include <cstdint>

#include "UsbConsoleLine.h"

// Shares the existing debug console. One main-loop owner; no task or UART.
class HalUsbConsole {
  UsbConsoleLine input;
  uint32_t lastByte = 0;

 public:
  const char* pollLine();
  bool inputWasRejected() const { return input.wasRejected(); }
  void consumeLine() { input.consume(); }
  void reply(uint32_t id, const char* kind, const char* value);
};
