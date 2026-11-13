#pragma once

#include <HalStorage.h>

#include <cstdint>

#include "util/UsbFileTransfer.h"

class HalUsbConsole;

// Writes one pushed story at a time to /stories via a temp file; main-loop owned.
class StoryUploadReceiver {
  HalFile file;
  uint32_t id = 0;
  uint32_t size = 0;
  uint32_t expectedCrc = 0;
  uint32_t crc = 0;
  uint32_t written = 0;
  uint32_t lastFrameMs = 0;
  char name[usbFile::NAME_BYTES + 1]{};

  void abort();
  void list(uint32_t requestId, HalUsbConsole& console);
  void remove(uint32_t requestId, const char* storyName, HalUsbConsole& console);
  bool active() const { return id != 0; }

 public:
  static constexpr uint32_t IDLE_TIMEOUT_MS = 10000;
  // Replies to the host for every command; never blocks on the host.
  void handle(const usbFile::Command& command, HalUsbConsole& console, uint32_t nowMs);
  void expire(uint32_t nowMs);
};
