#include "HalUsbConsole.h"

#if FREEINK_DEVICE_X4CLASSIC
#include <Arduino.h>
#include <Logging.h>

#include <cstdio>

const char* HalUsbConsole::pollLine() {
  const uint32_t now = millis();
  if (input.partial() && static_cast<uint32_t>(now - lastByte) > 3000) input.discard();
  // Bound serial work so a partial frame cannot stall buttons or reading.
  for (int budget = 512; budget > 0 && logSerial.available() > 0; --budget) {
    const int byte = logSerial.read();
    if (byte < 0) break;
    lastByte = now;
    if (input.feed(static_cast<char>(byte))) return input.line();
  }
  return nullptr;
}

void HalUsbConsole::reply(uint32_t id, const char* kind, const char* value) {
  char frame[256];
  const int length =
      std::snprintf(frame, sizeof(frame), "XT1 %s %lu %s\n", kind, static_cast<unsigned long>(id), value);
  if (length > 0 && static_cast<size_t>(length) < sizeof(frame)) {
    logSerial.write(reinterpret_cast<const uint8_t*>(frame), length);
  }
}
#endif
