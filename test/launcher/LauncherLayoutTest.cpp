#include <gtest/gtest.h>

#include "util/LauncherLayout.h"

namespace fui = freeink::ui;

TEST(LauncherLayout, NavigationWrapsOnce) {
  using launcher::Mode;
  EXPECT_EQ(launcher::moved(Mode::Reader, -1), Mode::Settings);
  EXPECT_EQ(launcher::moved(Mode::Settings, 1), Mode::Reader);
  auto mode = Mode::Reader;
  for (int i = 0; i < launcher::MODE_COUNT; ++i) {
    EXPECT_EQ(static_cast<int>(mode), i);
    mode = launcher::moved(mode, 1);
  }
  EXPECT_EQ(mode, Mode::Reader);
}

TEST(LauncherLayout, FitsPortraitAndLandscapeWithInsets) {
  const fui::Rect bodies[] = {{12, 70, 456, 650}, {22, 60, 446, 655}, {40, 55, 735, 345}, {12, 55, 735, 345}};
  for (const auto body : bodies) {
    const auto tiles = launcher::tileRects(body, 8);
    for (int i = 0; i < launcher::MODE_COUNT; ++i) {
      const auto rect = tiles[i];
      EXPECT_GT(rect.width, 0);
      EXPECT_GT(rect.height, 0);
      EXPECT_GE(rect.x, body.x);
      EXPECT_GE(rect.y, body.y);
      EXPECT_LE(rect.right(), body.right());
      EXPECT_LE(rect.bottom(), body.bottom());
      for (int j = i + 1; j < launcher::MODE_COUNT; ++j) {
        const auto other = tiles[j];
        EXPECT_TRUE(rect.right() <= other.x || other.right() <= rect.x || rect.bottom() <= other.y ||
                    other.bottom() <= rect.y);
      }
    }
    for (int row = 0; row < 3; ++row) {
      EXPECT_EQ(tiles[row * 2].y, tiles[row * 2 + 1].y);
      EXPECT_LT(tiles[row * 2].x, tiles[row * 2 + 1].x);
      if (row > 0) EXPECT_GT(tiles[row * 2].y, tiles[(row - 1) * 2].y);
    }
  }
}
