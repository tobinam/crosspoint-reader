#pragma once

#include <FreeInkUILayout.h>

#include <array>

namespace launcher {
enum class Mode : uint8_t { Reader, Stories, Hosts, Tv, Inbox, Cube, Settings };
constexpr int MODE_COUNT = 7;

inline Mode moved(Mode current, int delta) {
  return static_cast<Mode>((static_cast<int>(current) + delta + MODE_COUNT) % MODE_COUNT);
}

inline std::array<freeink::ui::Rect, MODE_COUNT> tileRects(freeink::ui::Rect body, int16_t gap) {
  namespace fui = freeink::ui;
  std::array<fui::Rect, MODE_COUNT> rects{};
  constexpr int ROWS = (MODE_COUNT + 1) / 2;
  fui::layoutLinear(
      body, fui::Axis::Column, gap, ROWS, [](uint8_t) { return fui::LayoutLength::flexible(); },
      [&](uint8_t row, fui::Rect rowRect) {
        // An odd last tile spans its row.
        const uint8_t columns = row * 2 + 1 < MODE_COUNT ? 2 : 1;
        fui::layoutLinear(
            rowRect, fui::Axis::Row, gap, columns, [](uint8_t) { return fui::LayoutLength::flexible(); },
            [&](uint8_t col, fui::Rect rect) { rects[row * 2 + col] = rect; });
      });
  return rects;
}
}  // namespace launcher
