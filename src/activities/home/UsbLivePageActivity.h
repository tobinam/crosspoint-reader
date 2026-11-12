#pragma once

#include <HalUsbConsole.h>

#include <atomic>

#include "activities/Activity.h"
#include "util/UsbLivePage.h"

class UsbLivePageActivity final : public Activity {
  const usbLive::Page& page;
  std::atomic<bool> ready{false};
  std::atomic<bool> acknowledged{false};
  uint16_t firstBodyLine = 0;
  HalUsbConsole& console;
  bool& active;

 public:
  UsbLivePageActivity(GfxRenderer& renderer, MappedInputManager& input, const usbLive::Page& page,
                      HalUsbConsole& console, bool& active);
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return false; }
  bool handleHomeGesture() override;
};
