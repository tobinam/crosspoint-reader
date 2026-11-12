#pragma once

#include <atomic>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"
#include "util/LauncherLayout.h"

class LauncherActivity final : public Activity, public UiAppHost {
  ButtonNavigator navigator;
  std::atomic<launcher::Mode> selected;
  freeink::ui::ButtonProps tile;
  const bool cleanInitialRefresh;
  bool firstRenderDone = false;

  static void screenFn(UiScreen& screen, void* user);
  static void onTile(const freeink::ui::ActionEvent& event, void* user);
  void buildScreen(UiScreen& screen);
  void activate(launcher::Mode mode);
  void move(int delta);

 public:
  LauncherActivity(GfxRenderer& renderer, MappedInputManager& input, HomeMenuItem initial, bool cleanInitialRefresh);
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }
};
