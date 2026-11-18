#include "Modes.h"

#if XTEINK_MODES
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <ctime>
#include <utility>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "GfxRenderer.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "activities/home/HomeActivity.h"
#include "fontIds.h"
#if XTEINK_INK_STORIES
#include "activities/home/StoriesActivity.h"
#endif
#if XTEINK_TV_REMOTE
#include "activities/home/TvRemoteActivity.h"
#endif
#if XTEINK_CUBE_TIMER
#include "activities/home/CubeTimerActivity.h"
#endif

extern GfxRenderer renderer;
extern MappedInputManager mappedInputManager;

namespace modes {
namespace {
Mode currentMode = Mode::None;
bool recentWake = false;

bool available(const Mode mode) {
  switch (mode) {
    case Mode::Reader:
    case Mode::Settings:
      return true;
    case Mode::Stories:
#if XTEINK_INK_STORIES
      return true;
#else
      return false;
#endif
    case Mode::Tv:
#if XTEINK_TV_REMOTE
      return true;
#else
      return false;
#endif
    case Mode::Cube:
#if XTEINK_CUBE_TIMER
      return true;
#else
      return false;
#endif
    default:
      return false;
  }
}

// The mode's name alone, large: e-ink takes a moment, the user should see where the press goes.
void announce(const Mode mode) {
  RenderLock lock;
  renderer.clearScreen();
  const int y = renderer.getScreenHeight() / 2 - renderer.getLineHeight(NOTOSANS_18_FONT_ID);
  renderer.drawCenteredText(NOTOSANS_18_FONT_ID, y, name(mode), true, EpdFontFamily::BOLD);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
}  // namespace

bool enabled() { return SETTINGS.powerModes != CrossPointSettings::PWR_MODES_CLASSIC && !SETTINGS.startInTiles; }

bool shortPressSleeps() { return SETTINGS.powerModes == CrossPointSettings::PWR_MODES_SHORT_SLEEP; }
bool longPressSleeps() { return SETTINGS.powerModes == CrossPointSettings::PWR_MODES_SHORT_SWITCH; }

Mode current() { return currentMode; }
void setCurrent(const Mode mode) { currentMode = mode; }

const char* name(const Mode mode) {
  switch (mode) {
    case Mode::Reader:
      return tr(STR_MODE_READER);
    case Mode::Stories:
      return tr(STR_STORIES_TITLE);
    case Mode::Tv:
      return tr(STR_TV_TITLE);
    case Mode::Cube:
      return tr(STR_CUBE_TITLE);
    case Mode::Settings:
      return tr(STR_SETTINGS_TITLE);
    default:
      return "";
  }
}

void open(const Mode mode) {
  currentMode = mode;
  switch (mode) {
    case Mode::Reader:
      if (!APP_STATE.openEpubPath.empty()) {
        activityManager.goToReader(APP_STATE.openEpubPath);
      } else if (auto home = makeUniqueNoThrow<HomeActivity>(renderer, mappedInputManager)) {
        activityManager.replaceActivity(std::move(home));
      }
      return;
    case Mode::Stories:
#if XTEINK_INK_STORIES
      if (auto stories = makeUniqueNoThrow<StoriesActivity>(renderer, mappedInputManager))
        activityManager.replaceActivity(std::move(stories));
#endif
      return;
    case Mode::Tv:
#if XTEINK_TV_REMOTE
      if (auto tv = makeUniqueNoThrow<TvRemoteActivity>(renderer, mappedInputManager))
        activityManager.replaceActivity(std::move(tv));
#endif
      return;
    case Mode::Cube:
#if XTEINK_CUBE_TIMER
      if (auto timer = makeUniqueNoThrow<CubeTimerActivity>(renderer, mappedInputManager))
        activityManager.replaceActivity(std::move(timer));
#endif
      return;
    case Mode::Settings:
      activityManager.goToSettings();
      return;
    default:
      return;
  }
}

void next() {
  int i = currentMode == Mode::None ? -1 : static_cast<int>(currentMode);
  Mode to = Mode::Reader;
  for (int step = 0; step < static_cast<int>(Mode::Count); ++step) {
    i = (i + 1) % static_cast<int>(Mode::Count);
    if (available(static_cast<Mode>(i))) {
      to = static_cast<Mode>(i);
      break;
    }
  }
  LOG_INF("MODE", "next: %s", name(to));
  announce(to);
  open(to);
}

void noteSleep() {
  APP_STATE.lastMode = static_cast<uint8_t>(currentMode);
  APP_STATE.lastSleepAt = static_cast<uint32_t>(time(nullptr));
}

Mode wakeMode(bool& recent) {
  const auto mode = static_cast<Mode>(APP_STATE.lastMode);
  const uint32_t now = static_cast<uint32_t>(time(nullptr));
  const uint32_t at = APP_STATE.lastSleepAt;
  // A clock that went backwards or was never set counts as recent: rather the same screen than lose it.
  recent = !at || now < at || now - at <= SETTINGS.wakeResumeSeconds();
  recentWake = recent;
  return available(mode) ? mode : Mode::None;
}

bool takeRecentWake() { return std::exchange(recentWake, false); }

}  // namespace modes
#endif
