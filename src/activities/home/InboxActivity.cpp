#if XTEINK_INBOX
#include "InboxActivity.h"

#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

#include "components/UITheme.h"
#include "stories/StoryTextLayout.h"

namespace fui = freeink::ui;
namespace {
constexpr unsigned long HOLD_MS = 700;
}

InboxActivity::InboxActivity(GfxRenderer& renderer, MappedInputManager& input)
    : UiListActivity("Inbox", renderer, input) {}

void InboxActivity::onEnter() {
  UiListActivity::onEnter();
  reload();
  requestUpdate();
}

void InboxActivity::reload() {
  entryCount = inbox::list(entries, inbox::CAPACITY);
  loadedGeneration = inbox::generation();
  if (nav.selected >= static_cast<int>(entryCount)) nav.selected = entryCount ? static_cast<int>(entryCount) - 1 : 0;
}

void InboxActivity::openPage(size_t index) {
  if (index >= entryCount) return;
  openIndex = index;
  pageLoaded = inbox::load(entries[index].owner, entries[index].id, page);
  if (pageLoaded && entries[index].unread) {
    if (inbox::markRead(entries[index].owner, entries[index].id)) entries[index].unread = false;
  }
  pageIndex = 0;
  pageStarts[0] = 0;
  pageEnd = 0;
  lastPage = true;
  view = View::Page;
  nav.selected = 0;
  nav.top = 0;
}

void InboxActivity::backToList() {
  view = View::List;
  reload();
  nav.selected = static_cast<int>(std::min(openIndex, entryCount ? entryCount - 1 : 0));
  nav.followOnBuild = true;
}

int InboxActivity::listCount() const {
  if (view == View::List) return static_cast<int>(entryCount);
  if (view == View::ConfirmDelete) return 2;
  return 0;
}

const char* InboxActivity::headerTitle() const {
  if (view == View::List) return tr(STR_INBOX_TITLE);
  if (view == View::ConfirmDelete) return tr(STR_INBOX_DELETE);
  return pageLoaded ? page.title : tr(STR_INBOX_TITLE);
}

void InboxActivity::drawFooter() {
  if (view == View::Page) {
    const auto labels =
        mappedInput.mapLabels(tr(STR_BACK), tr(STR_INBOX_HOLD_DELETE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }
  UiListActivity::drawFooter();
}

bool InboxActivity::handleCustomInput() {
  using Button = MappedInputManager::Button;
  if (view == View::List && inbox::generation() != loadedGeneration) {
    {
      RenderLock lock(*this);
      reload();
    }
    requestUpdate();
  }
  if (view != View::Page) return false;
  if (mappedInput.wasLongPressed(Button::Confirm, HOLD_MS)) {
    if (pageLoaded) {
      view = View::ConfirmDelete;
      nav.selected = 1;  // Keep is preselected
      nav.top = 0;
      requestUpdate();
    }
    return true;
  }
  if (mappedInput.wasPressed(Button::NavNext)) {
    if (!lastPage && pageIndex + 1 < MAX_PAGE_STARTS) {
      pageStarts[++pageIndex] = static_cast<uint16_t>(pageEnd);
      requestUpdate();
    }
    return true;
  }
  if (mappedInput.wasPressed(Button::NavPrevious)) {
    if (pageIndex > 0) {
      --pageIndex;
      requestUpdate();
    }
    return true;
  }
  if (mappedInput.wasReleased(Button::Back)) {
    {
      RenderLock lock(*this);
      backToList();
    }
    requestUpdate();
    return true;
  }
  // Swallow the rest so the empty list does not react.
  for (const auto button : {Button::Confirm, Button::Left, Button::Right, Button::Up, Button::Down})
    if (mappedInput.wasPressed(button) || mappedInput.wasReleased(button)) return true;
  return false;
}

void InboxActivity::onBackButton() {
  if (view == View::ConfirmDelete) {
    view = View::Page;
    requestUpdate();
    return;
  }
  activityManager.goHome();
}

void InboxActivity::activateIndex(int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  {
    RenderLock lock(*this);
    if (view == View::List) {
      openPage(static_cast<size_t>(index));
    } else if (view == View::ConfirmDelete) {
      if (index == 0) {
        if (!inbox::remove(entries[openIndex].owner, entries[openIndex].id))
          LOG_ERR("INBOX", "Could not delete %s+%s", entries[openIndex].owner, entries[openIndex].id);
        backToList();
      } else {
        view = View::Page;
      }
    }
  }
  requestUpdate();
}

void InboxActivity::provideRow(void* context, uint16_t index, fui::ListItem& item) {
  auto& self = *static_cast<InboxActivity*>(context);
  if (self.view == View::ConfirmDelete) {
    item.label = index == 0 ? tr(STR_DELETE) : tr(STR_INBOX_KEEP);
  } else {
    const auto& entry = self.entries[index];
    item.label = entry.title;
    item.subtitle = entry.owner;
    if (entry.unread) item.value = tr(STR_INBOX_NEW);
  }
  item.actionValue = index;
}

void InboxActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true);
  const int pad = app.theme().spaceSm;
  const int top = safe.y + pad + metrics.topPadding + metrics.headerHeight;
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(top), static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width + pad),
      static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height + pad), static_cast<int16_t>(safe.x + pad)});
  auto text = screen.theme().bodyText;
  if (view == View::Page) {
    const fui::Rect body = screen.body();
    if (!pageLoaded) {
      storyText::drawNarrative(screen.target(), body, tr(STR_INBOX_ERROR), text);
      return;
    }
    const int lineHeight = std::max(1, static_cast<int>(screen.target().lineHeight(text.font)));
    const size_t maxLines = std::max(1, body.height / lineHeight);
    const size_t length = std::strlen(page.body);
    const size_t start = std::min<size_t>(pageStarts[pageIndex], length);
    const auto slice = storyText::paginate(screen.target(), text, page.body + start, length - start, body.width, maxLines);
    storyText::drawNarrative(screen.target(), body, page.body + start, slice.length, text, maxLines);
    pageEnd = start + slice.length;
    while (pageEnd < length && (page.body[pageEnd] == '\n' || page.body[pageEnd] == ' ')) ++pageEnd;
    lastPage = slice.last || pageEnd >= length;
    return;
  }
  if (view == View::List && !entryCount) {
    storyText::drawNarrative(screen.target(), screen.body(), tr(STR_INBOX_EMPTY), text);
    return;
  }
  fui::ListProps props;
  props.rowProvider = &InboxActivity::provideRow;
  props.rowProviderCtx = this;
  props.count = listCount();
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = text;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
#endif
