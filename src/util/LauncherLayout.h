#pragma once

#include <FreeInkUILayout.h>

#include <array>

namespace launcher {
enum class Mode : uint8_t { Reader, Stories, Hosts, Tv, Inbox, Settings };
constexpr int MODE_COUNT = 6;

inline Mode moved(Mode current, int delta) {
  return static_cast<Mode>((static_cast<int>(current) + delta + MODE_COUNT) % MODE_COUNT);
}

inline std::array<freeink::ui::Rect, MODE_COUNT> tileRects(freeink::ui::Rect body, int16_t gap) {
  namespace fui = freeink::ui;
  std::array<fui::Rect, MODE_COUNT> rects{};
  fui::layoutLinear(
      body, fui::Axis::Column, gap, 3, [](uint8_t) { return fui::LayoutLength::flexible(); },
      [&](uint8_t row, fui::Rect rowRect) {
        fui::layoutLinear(
            rowRect, fui::Axis::Row, gap, 2, [](uint8_t) { return fui::LayoutLength::flexible(); },
            [&](uint8_t col, fui::Rect rect) { rects[row * 2 + col] = rect; });
      });
  return rects;
}
}  // namespace launcher
