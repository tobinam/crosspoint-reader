#include "UsbLivePageActivity.h"

#if FREEINK_DEVICE_X4CLASSIC

#include <I18n.h>

#include <cstring>

#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr size_t MAX_LINE_BYTES = 89;
constexpr uint16_t LINES_PER_PAGE = 14;

const char* advanceLine(const char* cursor, int maxColumns) {
  int columns = 0;
  while (*cursor && *cursor != '\n' && columns < maxColumns) {
    const unsigned char c = static_cast<unsigned char>(*cursor);
    cursor += c < 0x80 ? 1 : (c < 0xe0 ? 2 : (c < 0xf0 ? 3 : 4));
    ++columns;
  }
  if (*cursor == '\n') ++cursor;
  return cursor;
}

uint16_t lineCount(const char* text, int maxColumns) {
  uint16_t count = 0;
  while (*text && count < 256) {
    const char* next = advanceLine(text, maxColumns);
    if (next == text) break;
    text = next;
    ++count;
  }
  return count;
}
}  // namespace

UsbLivePageActivity::UsbLivePageActivity(GfxRenderer& renderer, MappedInputManager& input, const usbLive::Page& page,
                                         HalUsbConsole& console, bool& active)
    : Activity("USBPage", renderer, input), page(page), console(console), active(active) {}

void UsbLivePageActivity::onEnter() {
  Activity::onEnter();
  ready = false;
  acknowledged = false;
  requestUpdate();
}

bool UsbLivePageActivity::handleHomeGesture() {
  console.reply(page.id, "ACTION", "DISMISS");
  active = false;
  Activity::finish();
  return true;
}

void UsbLivePageActivity::loop() {
  if (!ready) return;
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true);
  const int bodyFontHeight = renderer.getFontAscenderSize(NOTOSANS_16_FONT_ID);
  const int columns = (safe.width - 2 * UITheme::getInstance().getMetrics().contentSidePadding) / bodyFontHeight;
  const uint16_t lineTotal = lineCount(page.body, columns > 0 ? columns : 1);
  if (mappedInput.wasReleased(MappedInputManager::Button::NavNext) && firstBodyLine + LINES_PER_PAGE < lineTotal) {
    firstBodyLine += LINES_PER_PAGE;
    requestUpdate();
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::NavPrevious) && firstBodyLine > 0) {
    firstBodyLine = firstBodyLine > LINES_PER_PAGE ? firstBodyLine - LINES_PER_PAGE : 0;
    requestUpdate();
  }
  if (!mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      !mappedInput.wasReleased(MappedInputManager::Button::Confirm))
    return;
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    console.reply(page.id, "ACTION", "OPEN");
    active = false;
    Activity::finish();
    return;
  }
  console.reply(page.id, "ACTION", "DISMISS");
  active = false;
  Activity::finish();
}

void UsbLivePageActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int margin = metrics.contentSidePadding;
  const int left = safe.x + margin;
  const int width = safe.width - margin * 2;
  int y = safe.y + metrics.topPadding;
  renderer.drawText(NOTOSANS_18_FONT_ID, left, y, page.title, true, EpdFontFamily::BOLD);
  y += renderer.getFontAscenderSize(NOTOSANS_18_FONT_ID) + metrics.verticalSpacing;
  const int bodyBottom = safe.y + safe.height - metrics.buttonHintsHeight;
  const int bodyFontHeight = renderer.getFontAscenderSize(NOTOSANS_16_FONT_ID);
  const int lineHeight = bodyFontHeight + metrics.verticalSpacing;
  const int availableLines = (bodyBottom - y) / lineHeight;
  const int maxLines = availableLines > 34 ? 34 : availableLines;
  const int charsPerLine = width / bodyFontHeight > 0 ? width / bodyFontHeight : 1;
  const uint16_t totalLines = lineCount(page.body, charsPerLine);
  const char* cursor = page.body;
  for (uint16_t line = 0; line < firstBodyLine && *cursor; ++line) cursor = advanceLine(cursor, charsPerLine);
  char line[MAX_LINE_BYTES];
  int count = 0;
  while (*cursor && count < maxLines) {
    const char* start = cursor;
    size_t bytes = 0;
    int columns = 0;
    while (*cursor && *cursor != '\n' && columns < charsPerLine && bytes + 4 < sizeof(line)) {
      const unsigned char c = static_cast<unsigned char>(*cursor);
      const size_t widthBytes = c < 0x80 ? 1 : (c < 0xe0 ? 2 : (c < 0xf0 ? 3 : 4));
      if (bytes + widthBytes >= sizeof(line)) break;
      bytes += widthBytes;
      cursor += widthBytes;
      ++columns;
    }
    if (cursor == start) break;
    std::memcpy(line, start, bytes);
    line[bytes] = 0;
    renderer.drawText(NOTOSANS_16_FONT_ID, left, y + count * lineHeight, line);
    if (*cursor == '\n') ++cursor;
    ++count;
  }
  renderer.drawText(UI_12_FONT_ID, left, bodyBottom - metrics.verticalSpacing, tr(STR_LIVE_PAGE_ACTION_HINT));
  char pageLabel[16];
  std::snprintf(pageLabel, sizeof(pageLabel), "%u/%u", firstBodyLine / LINES_PER_PAGE + 1,
                (totalLines + LINES_PER_PAGE - 1) / LINES_PER_PAGE);
  renderer.drawText(UI_12_FONT_ID, safe.x + safe.width - margin - 48, bodyBottom - metrics.verticalSpacing, pageLabel);
  auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
  ready = true;
  if (!acknowledged.exchange(true)) console.reply(page.id, "RECEIVED", "1");
}

#endif
