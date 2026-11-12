#include <gtest/gtest.h>

#include "hal/UsbConsoleLine.h"
#include "util/UsbLivePage.h"

TEST(UsbLiveLine, CompleteAndResynchronizes) {
  UsbConsoleLine line;
  EXPECT_FALSE(line.feed('O'));
  EXPECT_FALSE(line.feed('K'));
  EXPECT_TRUE(line.feed('\n'));
  EXPECT_STREQ(line.line(), "OK");
  EXPECT_FALSE(line.feed('x'));
  EXPECT_TRUE(line.feed('\n'));
  EXPECT_STREQ(line.line(), "x");
}

TEST(UsbLiveLine, OversizeDropsUntilNewline) {
  UsbConsoleLine line;
  for (size_t i = 0; i < UsbConsoleLine::CAPACITY + 10; ++i) EXPECT_FALSE(line.feed('x'));
  EXPECT_TRUE(line.feed('\n'));
  EXPECT_TRUE(line.wasRejected());
  EXPECT_STREQ(line.line(), "");
  EXPECT_FALSE(line.feed('O'));
  EXPECT_TRUE(line.feed('\n'));
  EXPECT_FALSE(line.wasRejected());
  EXPECT_STREQ(line.line(), "O");
}

TEST(UsbLivePage, DecodesBoundedUtf8AndNewlines) {
  const char* frame = "XT1 PAGE 42 5469746c65 48656c6c6f0aE28094E4B896E7958C";
  usbLive::Page page;
  ASSERT_TRUE(usbLive::parse(frame, page));
  EXPECT_EQ(page.id, 42u);
  EXPECT_STREQ(page.title, "Title");
  EXPECT_STREQ(page.body, "Hello\n—世界");
}

TEST(UsbLivePage, RejectsMalformedFrames) {
  const char* bad[] = {"OK",
                       "XT1 PAGE 0 54 42",
                       "XT1 PAGE 42949672960 54 42",
                       "XT1 PAGE 1 5 42",
                       "XT1 PAGE 1 00 42",
                       "XT1 PAGE 1 54 09",
                       "XT1 PAGE 1 54 c080",
                       "XT1 PAGE 1 54 eda080",
                       "XT1 PAGE 1 54 f4908080"};
  for (const auto* frame : bad) {
    usbLive::Page page;
    EXPECT_FALSE(usbLive::parse(frame, page)) << frame;
  }
}
