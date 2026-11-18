#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <esp_timer.h>

#include "activities/Activity.h"
#include "cube/CubeTimer.h"

// Cube timer for two players, the device lying landscape between them, each seeing its half upright.
// Each player's pair of bottom keys is its own: hold the outer key and release to start, either key stops.
// The keys are sampled every 1 ms off the main loop, so times do not depend on the loop or the display.
// Side keys: a short press of Up toggles the automatic next round, holding either exits.
class CubeTimerActivity final : public Activity {
 public:
  struct KeyEvent {
    int64_t t;
    uint8_t key;  // index into KEY_PINS order: back, confirm, left, right
    bool down;
  };

 private:
  cube::Match match;
  // What the screen showed last, to render only when that changes.
  char shown[64]{};
  uint64_t session = 0;
  GfxRenderer::Orientation savedOrientation = GfxRenderer::Portrait;
  bool firstFrame = true;
  QueueHandle_t events = nullptr;
  esp_timer_handle_t sampler = nullptr;
  portMUX_TYPE matchMux = portMUX_INITIALIZER_UNLOCKED;
  // Sampler state, touched only by the esp_timer task. A key change commits after this many equal 1 ms samples.
  static constexpr uint8_t STABLE_SAMPLES = 3;
  uint8_t committed = 0;
  uint8_t changedSamples[4]{};
  uint8_t quietSamples[4] = {STABLE_SAMPLES, STABLE_SAMPLES, STABLE_SAMPLES, STABLE_SAMPLES};
  int64_t pendingSince[4]{};

  static void sample(void* self);
  void handle(const KeyEvent& event);
  void describe(const cube::Match& state, int64_t now, char* out, size_t size) const;
  void drawSeat(int index, const cube::Match& state, int64_t now) const;

 public:
  CubeTimerActivity(GfxRenderer& renderer, MappedInputManager& input);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;
};
