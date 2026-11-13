#include "LauncherActivity.h"

#if FREEINK_DEVICE_X4CLASSIC

#include <I18n.h>
#include <Memory.h>

#include <algorithm>

#include "HomeActivity.h"
#if XTEINK_INK_STORIES
#include "StoriesActivity.h"
#endif
#if XTEINK_TV_REMOTE
#include "TvRemoteActivity.h"
#endif
#if XTEINK_INBOX
#include "InboxActivity.h"
#endif
#include "components/UITheme.h"
#if XTEINK_MODES
#include "modes/Modes.h"
#endif

namespace fui = freeink::ui;
namespace {
constexpr fui::ActionId ACTION_TILE = 1;
#if XTEINK_INK_STORIES
constexpr StrId STORY_LABEL = StrId::STR_STORIES_TITLE;
#else
constexpr StrId STORY_LABEL = StrId::STR_LAUNCHER_STORIES;
#endif
#if XTEINK_TV_REMOTE
constexpr StrId TV_LABEL = StrId::STR_TV_TITLE;
#else
constexpr StrId TV_LABEL = StrId::STR_LAUNCHER_TV;
#endif
#if XTEINK_INBOX
constexpr StrId INBOX_LABEL = StrId::STR_INBOX_TITLE;
#else
constexpr StrId INBOX_LABEL = StrId::STR_LAUNCHER_INBOX;
#endif
constexpr StrId LABELS[] = {StrId::STR_LAUNCHER_READER, STORY_LABEL,
                            StrId::STR_LAUNCHER_HOSTS,  TV_LABEL,
                            INBOX_LABEL,                StrId::STR_SETTINGS_TITLE};

static_assert(sizeof(LABELS) / sizeof(LABELS[0]) == launcher::MODE_COUNT);

bool available(launcher::Mode mode) {
#if XTEINK_INK_STORIES
  if (mode == launcher::Mode::Stories) return true;
#endif
#if XTEINK_TV_REMOTE
  if (mode == launcher::Mode::Tv) return true;
#endif
#if XTEINK_INBOX
  if (mode == launcher::Mode::Inbox) return true;
#endif
  return mode == launcher::Mode::Reader || mode == launcher::Mode::Settings;
}
}  // namespace

LauncherActivity::LauncherActivity(GfxRenderer& renderer, MappedInputManager& input, HomeMenuItem initial,
                                   bool cleanInitialRefresh)
    : Activity("Launcher", renderer, input),
      UiAppHost(renderer),
      selected(initial == HomeMenuItem::SETTINGS_MENU ? launcher::Mode::Settings : launcher::Mode::Reader),
      cleanInitialRefresh(cleanInitialRefresh) {}

void LauncherActivity::onEnter() {
  Activity::onEnter();
  resetUi();
  app.on(ACTION_TILE, &LauncherActivity::onTile, this);
  app.setScreen(&LauncherActivity::screenFn, this);
  requestUpdate();
}

void LauncherActivity::move(int delta) {
  selected.store(launcher::moved(selected.load(), delta));
  requestUpdate();
}

void LauncherActivity::activate(launcher::Mode mode) {
  selected.store(mode);

  switch (mode) {
    case launcher::Mode::Reader: {
      auto readerHome = makeUniqueNoThrow<HomeActivity>(renderer, mappedInput);
      if (!readerHome) {
        LOG_ERR("HOME", "OOM: reader home (%u bytes)", static_cast<unsigned>(sizeof(HomeActivity)));
        return;
      }
      app.clearTapFlash();
      activityManager.replaceActivity(std::move(readerHome));
      return;
    }
    case launcher::Mode::Settings:
      app.clearTapFlash();
      activityManager.goToSettings();
      return;
    case launcher::Mode::Stories:
#if XTEINK_INK_STORIES
    {
      auto stories = makeUniqueNoThrow<StoriesActivity>(renderer, mappedInput);
      if (!stories) {
        LOG_ERR("HOME", "OOM: stories");
        return;
      }
      app.clearTapFlash();
      activityManager.replaceActivity(std::move(stories));
      return;
    }
#endif
    case launcher::Mode::Tv:
#if XTEINK_TV_REMOTE
    {
      auto remote = makeUniqueNoThrow<TvRemoteActivity>(renderer, mappedInput);
      if (!remote) {
        LOG_ERR("HOME", "OOM: TV remote");
        return;
      }
      app.clearTapFlash();
      activityManager.replaceActivity(std::move(remote));
      return;
    }
#endif
    case launcher::Mode::Inbox:
#if XTEINK_INBOX
    {
      auto inboxScreen = makeUniqueNoThrow<InboxActivity>(renderer, mappedInput);
      if (!inboxScreen) {
        LOG_ERR("HOME", "OOM: inbox");
        return;
      }
      app.clearTapFlash();
      activityManager.replaceActivity(std::move(inboxScreen));
      return;
    }
#endif
    case launcher::Mode::Hosts:
      requestUpdate();
      return;
  }
}

void LauncherActivity::onTile(const fui::ActionEvent& event, void* user) {
  if (event.value < 0 || event.value >= launcher::MODE_COUNT) return;
  static_cast<LauncherActivity*>(user)->activate(static_cast<launcher::Mode>(event.value));
}

void LauncherActivity::loop() {
  const auto touch = routeTouch(mappedInput);
  if (touch.routed && app.invalidated()) requestUpdate();
  if (touch) return;
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activate(selected.load());
    return;
  }
  navigator.onNext([this] { move(1); });
  navigator.onPrevious([this] { move(-1); });
}

void LauncherActivity::screenFn(UiScreen& screen, void* user) {
  static_cast<LauncherActivity*>(user)->buildScreen(screen);
}

void LauncherActivity::buildScreen(UiScreen& screen) {
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y), static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width),
      static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height), static_cast<int16_t>(safe.x)});
  const auto& theme = screen.theme();
  screen.insetContent(fui::Insets{theme.spaceSm, theme.spaceSm, theme.spaceSm, theme.spaceSm});
  auto title = theme.titleText;
  title.align = fui::TextAlign::Center;
  screen.target().text(screen.takeTop(screen.target().lineHeight(title.font), theme.spaceSm), tr(STR_LAUNCHER_TITLE),
                       title);
  const auto mode = selected.load();
  auto hint = theme.smallText;
  hint.align = fui::TextAlign::Center;
  hint.maxLines = 2;
  screen.target().text(screen.takeBottom(screen.target().lineHeight(hint.font) * 2, theme.spaceSm),
                       mode == launcher::Mode::Reader ? tr(STR_LAUNCHER_READER_HINT)
                       : available(mode)              ? tr(STR_SELECT)
                                                      : tr(STR_LAUNCHER_UNAVAILABLE),
                       hint);
  const auto rects = launcher::tileRects(screen.body(), theme.spaceSm);
  tile.text = theme.titleText;
  tile.text.maxLines = 2;
  for (int i = 0; i < launcher::MODE_COUNT; ++i) {
    tile.label = I18N.get(LABELS[i]);
    tile.action = ACTION_TILE;
    tile.value = i;
    tile.inputMask = fui::InputTouch;
    tile.state = static_cast<int>(mode) == i ? fui::StateSelected : fui::StateNormal;
    screen.button(tile, rects[i]);
  }
}

void LauncherActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderUi();
  const auto labels = mappedInput.mapLabels("", tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(cleanInitialRefresh && !firstRenderDone ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  firstRenderDone = true;
}

#endif
