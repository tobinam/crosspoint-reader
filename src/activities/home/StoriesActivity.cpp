#if XTEINK_INK_STORIES
#include "StoriesActivity.h"

#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "activities/reader/ReaderUtils.h"
#include "components/HeaderBackTapTarget.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "stories/StoryProgress.h"
#include "stories/StoryTextLayout.h"
#include "util/UsbFileTransfer.h"

namespace fui = freeink::ui;
namespace {
extern const uint8_t exampleStart[] asm("_binary_stories_examples_low_tide_bin_start");
extern const uint8_t exampleEnd[] asm("_binary_stories_examples_low_tide_bin_end");
constexpr char EXAMPLE[] = "/stories/low-tide.bin";
}  // namespace

StoriesActivity::StoriesActivity(GfxRenderer& renderer, MappedInputManager& input)
    : UiListActivity("Stories", renderer, input) {}

bool StoriesActivity::installExample() {
  if (!Storage.ensureDirectoryExists("/stories")) return false;
  if (Storage.exists(EXAMPLE)) return true;
  constexpr char TEMP[] = "/stories/.low-tide.tmp";
  if (Storage.exists(TEMP) && !Storage.remove(TEMP)) return false;
  auto file = Storage.open(TEMP, O_WRONLY | O_CREAT | O_EXCL);
  if (!file) return false;
  const size_t length = exampleEnd - exampleStart;
  const bool written = file.write(exampleStart, length) == length;
  file.flush();
  file.close();
  if (!written) {
    Storage.remove(TEMP);
    return false;
  }
  return Storage.rename(TEMP, EXAMPLE);
}

void StoriesActivity::scan() {
  fileCount = 0;
  auto dir = Storage.open("/stories");
  if (!dir || !dir.isDirectory()) {
    view = View::Error;
    return;
  }
  for (size_t scanned = 0; scanned < 256 && fileCount < MAX_FILES; ++scanned) {
    auto file = dir.openNextFile();
    if (!file) break;
    if (file.isDirectory()) continue;
    auto& name = files[fileCount].text;
    name[0] = 0;
    file.getName(name, sizeof(name));
    name[sizeof(name) - 1] = 0;
    const size_t length = std::strlen(name);
    if (name[0] == '.' || length < 5 || length >= sizeof(name) - 1 || std::strchr(name, '/') ||
        std::strchr(name, '\\') || std::strcmp(name + length - 4, ".bin"))
      continue;
    ++fileCount;
  }
  std::sort(files, files + fileCount,
            [](const FileName& a, const FileName& b) { return std::strcmp(a.text, b.text) < 0; });
  for (size_t i = 0; i < fileCount; ++i) plays[i] = storyProgress::plays(files[i].text);
}

void StoriesActivity::onEnter() {
  // Stories follow the reader's orientation; the rest of the UI stays portrait.
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
  UiListActivity::onEnter();
  uiTarget.setFont(fui::GfxRendererTarget::FONT_BODY, NOTOSANS_18_FONT_ID);
  if (!initialized) {
    initialized = true;
    if (!installExample()) LOG_ERR("INK", "Could not install example story");
    scan();
    // After sleeping inside a story, reopen it at the saved page.
    char resume[sizeof(files[0].text)];
    if (storyProgress::takeWakeResume(resume, sizeof(resume))) {
      for (size_t i = 0; i < fileCount; ++i) {
        if (std::strcmp(files[i].text, resume) == 0) {
          RenderLock lock(*this);
          openStory(i);
          break;
        }
      }
    }
  }
  requestUpdate();
}

void StoriesActivity::saveProgress() {
  if ((view != View::Reading && view != View::Choices) || ended || saved || session.failed()) return;
  saved = storyProgress::save(files[openIndex].text, storyCrc, session, static_cast<uint32_t>(textOffset));
}

// Runs before deep sleep (power button, auto-sleep, low battery). The wake
// marker makes the next boot reopen this story at the saved page.
void StoriesActivity::prepareForSleep() {
  if (view != View::Reading && view != View::Choices) return;
  saveProgress();
  if (saved) {
    storyProgress::markWakeResume(files[openIndex].text);
    sleeping = true;
  }
}

void StoriesActivity::onExit() {
  saveProgress();  // e.g. the Home gesture while reading
  if (!sleeping) storyProgress::clearWakeResume();
  UiListActivity::onExit();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
}

const char* StoriesActivity::headerTitle() const { return storyScreen() ? nullptr : tr(STR_STORIES_TITLE); }

void StoriesActivity::drawChrome() {
  if (headerTitle())
    UiListActivity::drawChrome();
  else
    HeaderBackTapTarget::clear();
}

// Story screens show no button hints; the story list does.
void StoriesActivity::drawFooter() {
  if (view == View::Library) {
    const auto labels =
        mappedInput.mapLabels(tr(STR_STORIES_HOLD_EXIT), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (!storyScreen()) {
    UiListActivity::drawFooter();
  }
}

size_t StoriesActivity::passageStart() const {
  const char* text = session.text();
  size_t offset = 0;
  while (text[offset] == '\n' || text[offset] == ' ') ++offset;
  return offset;
}

// Pages are only computed forward, so walk from the passage start to find the
// page before textOffset. Uses the same layout as showPage().
size_t StoriesActivity::previousPageStart() const {
  const char* text = session.text();
  const size_t length = std::strlen(text);
  const Rect area = contentArea();
  const auto style = app.theme().bodyText;
  const int markerSpace = markerWidth("\xC2\xBB") + app.theme().spaceSm * 2;
  size_t start = passageStart();
  while (start < textOffset) {
    const auto page =
        storyText::paginate(uiTarget, style, text + start, length - start, area.width, pageMaxLines, markerSpace);
    size_t next = start + page.length;
    while (next < length && (text[next] == '\n' || text[next] == ' ')) ++next;
    if (next >= textOffset || page.length == 0) break;
    start = next;
  }
  return start;
}

void StoriesActivity::nextPage() {
  if (!lastPage) {
    textOffset = nextOffset;
    showPage();
  } else if (session.choices()) {
    view = View::Choices;
    resetSelection();
  } else {
    returnToLibrary();
  }
}

void StoriesActivity::previousPage() {
  if (textOffset > passageStart()) {
    textOffset = previousPageStart();
    showPage();
  } else if (lastChoice[0]) {
    // Cannot page across a decision; briefly show the choice that led here.
    flashChoice = true;
    flashUntil = millis() + FLASH_MS;
  }
}

bool StoriesActivity::handleCustomInput() {
  using Button = MappedInputManager::Button;
  constexpr unsigned long HOLD_MS = 700;
  if (flashChoice) {
    if (static_cast<long>(millis() - flashUntil) >= 0) {
      flashChoice = false;
      requestUpdate();
    }
    return true;  // Input waits until the page is back.
  }
  // Back must be held: to the story list from a story, or out of Stories.
  if (mappedInput.wasLongPressed(Button::Back, HOLD_MS)) {
    if (view == View::Library) {
      activityManager.goHome();
    } else {
      {
        RenderLock lock(*this);
        saveProgress();
        returnToLibrary();
      }
      requestUpdate();
    }
    return true;
  }
  // A short Back acts like Select in a story (» sits next to it in landscape);
  // wasLongPressed() suppresses the release after a hold.
  if (mappedInput.wasReleased(Button::Back)) {
    if (storyScreen()) activateIndex(nav.selected);
    return true;
  }
  if (view == View::Library) {
    if (fileCount && mappedInput.wasLongPressed(Button::Confirm, HOLD_MS)) {
      {
        RenderLock lock(*this);
        openStory(static_cast<size_t>(std::max(0, static_cast<int>(nav.selected))), true);
      }
      requestUpdate();
      return true;
    }
    return false;
  }
  if (view == View::Error) return false;
  // Up on the first choice (or on a page without choices) goes back a page.
  const bool atTop = (view == View::Reading && !inlineChoices) || nav.selected == 0;
  // NavPrevious/NavNext follow orientation exactly like list navigation and its hints.
  const bool back = mappedInput.wasPressed(Button::NavPrevious);
  if (atTop && back) {
    {
      RenderLock lock(*this);
      if (view == View::Choices)
        showPage();  // Back to the last page of the passage.
      else
        previousPage();
    }
    requestUpdate();
    return true;
  }
  if (view != View::Reading || inlineChoices) return false;
  // Narration page: next acts like Select on »; previous pages back (above).
  if (mappedInput.wasPressed(Button::NavNext)) {
    {
      RenderLock lock(*this);
      nextPage();
    }
    requestUpdate();
    return true;
  }
  // Swallow remaining presses so the one-row list does not repaint.
  for (const auto button : {Button::Left, Button::Right, Button::Up, Button::Down})
    if (mappedInput.wasPressed(button) || mappedInput.wasReleased(button)) return true;
  return false;
}

void StoriesActivity::rememberChoice(int index) {
  std::snprintf(lastChoice, sizeof(lastChoice), "%s", session.choiceText(index));
}

int StoriesActivity::listCount() const {
  if (view == View::Library) return fileCount;
  if (view == View::Choices || (view == View::Reading && inlineChoices)) return session.choices();
  return 1;
}

void StoriesActivity::resetSelection() {
  nav.selected = 0;
  nav.top = 0;
  nav.followOnBuild = true;
}

void StoriesActivity::returnToLibrary() {
  session.clear();
  bytes.reset();
  view = View::Library;
  resetSelection();
  // Select the story just left, so Confirm re-enters it.
  if (openIndex < fileCount) nav.selected = static_cast<int>(openIndex);
}

Rect StoriesActivity::contentArea() const {
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, !storyScreen());
  const int pad = app.theme().spaceSm;
  return Rect{safe.x + pad, safe.y + pad, std::max(1, safe.width - pad * 2), std::max(1, safe.height - pad * 2)};
}

// Symbol rather than a word: it must not read as story text in any language.
const char* StoriesActivity::markerLabel() const {
  if (!lastPage || session.choices()) return "\xC2\xBB";  // »
  return std::strcmp(session.language(), "de") == 0 ? "Ende" : tr(STR_STORIES_FINISHED);
}

int StoriesActivity::markerWidth(const char* label) const {
  const int text = storyText::textWidth(uiTarget, app.theme().bodyText, label, std::strlen(label));
  // Generous padding: the list row adds side padding and insets around the label.
  return std::max<int>(UITheme::getInstance().getMetrics().listRowHeight * 2, text + 64);
}

void StoriesActivity::showPage() {
  const auto& theme = app.theme();
  view = View::Reading;  // Before contentArea(): story screens have no hint band.
  const Rect area = contentArea();
  const auto style = theme.bodyText;
  const int lineHeight = std::max(1, static_cast<int>(uiTarget.lineHeight(style.font)));
  const char* text = session.text();
  const size_t length = std::strlen(text);
  while (textOffset < length && (text[textOffset] == '\n' || text[textOffset] == ' ')) ++textOffset;
  pageMaxLines = std::max(1, area.height / lineHeight);
  // The last line shares its row with the marker, keeping a small gap.
  const int gap = theme.spaceSm * 2;
  markerSpace = markerWidth("\xC2\xBB") + gap;
  auto page = storyText::paginate(uiTarget, style, text + textOffset, length - textOffset, area.width, pageMaxLines,
                                  markerSpace);
  if (page.last && !session.choices()) {
    markerSpace = markerWidth(std::strcmp(session.language(), "de") == 0 ? "Ende" : tr(STR_STORIES_FINISHED)) + gap;
    page = storyText::paginate(uiTarget, style, text + textOffset, length - textOffset, area.width, pageMaxLines,
                               markerSpace);
  }
  nextOffset = textOffset + page.length;
  pageLines = page.lines;
  lastPage = page.last;
  saved = false;
  if (lastPage && !session.choices() && !ended) {
    // Count each ending once; a finished story starts fresh next time.
    ended = true;
    storyProgress::discard(files[openIndex].text);
    storyProgress::addPlay(files[openIndex].text);
    if (plays[openIndex] < UINT16_MAX) ++plays[openIndex];
  }
  inlineChoices = false;
  if (lastPage && session.choices()) {
    // Conservative row estimate: wrapped label plus row padding and gap.
    int needed = static_cast<int>(pageLines) * lineHeight + theme.spaceSm;
    const int labelWidth = std::max(1, area.width - theme.spaceSm * 6);
    for (size_t i = 0; i < session.choices(); ++i) {
      const char* label = session.choiceText(i);
      const int lines = std::max<int>(1, storyText::lineCount(uiTarget, style, label, std::strlen(label), labelWidth));
      needed += std::max<int>(UITheme::getInstance().getMetrics().listRowHeight, lines * lineHeight + theme.spaceSm * 2) +
                theme.spaceSm * 2;
    }
    inlineChoices = needed <= area.height;
  }
  resetSelection();
}

void StoriesActivity::openStory(size_t index, bool startOver) {
  if (index >= fileCount) return;
  openIndex = index;
  ended = false;
  saved = false;
  lastChoice[0] = 0;
  session.clear();
  bytes.reset();
  std::snprintf(path, sizeof(path), "/stories/%s", files[index].text);
  auto file = Storage.open(path);
  const size_t length = file ? file.size() : 0;
  if (length < 64 || length > InkStorySession::MAX_FILE_BYTES) {
    view = View::Error;
    return;
  }
  bytes = HalMemory::allocatePsram(length);
  if (!bytes || file.read(bytes.get(), length) != static_cast<int>(length)) {
    view = View::Error;
    return;
  }
  // InkCPP has upstream internal allocations; admit only with ample default heap.
  const auto heap = HalMemory::getDefaultHeap();
  const auto internal = HalMemory::getInternalHeap();
  if (heap.freeBytes < 128 * 1024 || heap.largestBlockBytes < 64 * 1024 || internal.freeBytes < 64 * 1024 ||
      internal.largestBlockBytes < 16 * 1024) {
    view = View::Error;
    return;
  }
  storyCrc = usbFile::crc32(0, bytes.get(), length);
  uint32_t resumeOffset = 0;
  if (startOver) storyProgress::discard(files[index].text);
  const bool resumed =
      !startOver && storyProgress::restore(files[index].text, storyCrc, bytes.get(), length, session, resumeOffset);
  if (resumed) {
    textOffset = resumeOffset;
  } else if (session.load(bytes.get(), length)) {
    textOffset = 0;
  } else {
    view = View::Error;
    return;
  }
  LOG_INF("INK", "Opened %s (%u bytes)", path, static_cast<unsigned>(length));
  showPage();
  saved = resumed;  // The save on disk already matches this page.
}

void StoriesActivity::activateIndex(int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  {
    RenderLock lock(*this);
    switch (view) {
      case View::Library:
        openStory(index);
        break;
      case View::Reading:
        if (inlineChoices) {
          rememberChoice(index);
          if (!session.choose(index))
            view = View::Error;
          else {
            textOffset = 0;
            showPage();
          }
        } else {
          nextPage();
        }
        break;
      case View::Choices:
        rememberChoice(index);
        if (!session.choose(index))
          view = View::Error;
        else {
          textOffset = 0;
          showPage();
        }
        break;
      case View::Error:
        returnToLibrary();
        break;
    }
  }
  requestUpdate();
}

// Button Back is handled in handleCustomInput (hold only); this serves touch/header back.
void StoriesActivity::onBackButton() {
  if (view == View::Library) {
    activityManager.goHome();
    return;
  }
  {
    RenderLock lock(*this);
    saveProgress();
    returnToLibrary();
  }
  requestUpdate();
}

void StoriesActivity::provideRow(void* context, uint16_t index, fui::ListItem& item) {
  const auto& self = *static_cast<StoriesActivity*>(context);
  if (index >= self.listCount()) return;
  const bool choice = self.view == View::Choices || (self.view == View::Reading && self.inlineChoices);
  item.value = nullptr;
  if (self.view == View::Library && self.plays[index] > 1) {
    static char count[8];  // Valid until the next provider call, as the list requires.
    std::snprintf(count, sizeof(count), "%u\xC3\x97", static_cast<unsigned>(self.plays[index]));  // 3×
    item.value = count;
  }
  item.label = self.view == View::Library ? self.files[index].text
               : choice                   ? self.session.choiceText(index)
               : self.view == View::Error ? tr(STR_BACK)
                                          : self.markerLabel();
  item.actionValue = index;
}

void StoriesActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect area = contentArea();
  const int headerSpace = headerTitle() ? metrics.topPadding + metrics.headerHeight : 0;
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(area.y + headerSpace), static_cast<int16_t>(renderer.getScreenWidth() - area.x - area.width),
      static_cast<int16_t>(renderer.getScreenHeight() - area.y - area.height), static_cast<int16_t>(area.x)});
  auto text = screen.theme().bodyText;
  if (flashChoice) {
    // The decision that led to this passage, shown centred for FLASH_MS.
    auto choice = text;
    choice.bold = true;
    choice.align = fui::TextAlign::Center;
    choice.maxLines = 6;
    const fui::Rect body = screen.body();
    char line[sizeof(lastChoice) + 4];
    std::snprintf(line, sizeof(line), "\xC2\xBB %s", lastChoice);
    const int height = fui::measureWrappedText(screen.target(), line, choice, body.width).height;
    screen.target().text(fui::Rect{body.x, static_cast<int16_t>(body.y + (body.height - height) / 2), body.width,
                                   static_cast<int16_t>(height)},
                         line, choice);
    return;
  }
  const int count = listCount();
  const bool marker = view == View::Reading && !inlineChoices;
  if (view == View::Reading) {
    const int lineHeight = std::max(1, static_cast<int>(screen.target().lineHeight(text.font)));
    const char* page = session.text() + textOffset;
    const size_t pageBytes = nextOffset - textOffset;
    if (inlineChoices) {
      storyText::drawNarrative(screen.target(), screen.takeTop(static_cast<int>(pageLines) * lineHeight,
                                                               screen.theme().spaceSm),
                               page, pageBytes, text, pageMaxLines, markerSpace);
    } else {
      const fui::Rect body = screen.body();
      storyText::drawNarrative(screen.target(), body, page, pageBytes, text, pageMaxLines, markerSpace);
      // Marker sits at the right end of the last text line.
      const int rowHeight = metrics.listRowHeight;
      const int lastLineBottom = std::min<int>(body.height, static_cast<int>(pageMaxLines) * lineHeight);
      const int top = std::max(0, lastLineBottom - rowHeight);
      const int width = std::min<int>(body.width, markerWidth(markerLabel()));
      screen.insetContent(fui::Insets{static_cast<int16_t>(top), 0, 0, static_cast<int16_t>(body.width - width)});
    }
  } else if (view == View::Error || !count) {
    const int reserve = count ? metrics.listRowHeight + metrics.verticalSpacing : 0;
    storyText::drawNarrative(screen.target(), screen.takeTop(std::max(0, screen.body().height - reserve)),
                             view == View::Error ? tr(STR_STORIES_ERROR) : tr(STR_STORIES_EMPTY), text);
  }
  fui::ListProps props;
  props.rowProvider = &StoriesActivity::provideRow;
  props.rowProviderCtx = this;
  props.count = count;
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 3;
  if (marker) props.labelText.align = fui::TextAlign::Center;
  syncListViewport(screen, props);
  screen.list(props);
}
#endif
