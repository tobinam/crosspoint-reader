#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "stories/StoryTextLayout.h"

namespace fui = freeink::ui;
namespace {
class RecordingTarget final : public fui::DrawTarget {
 public:
  std::vector<std::string> lines;
  std::vector<fui::Rect> bounds;
  fui::Size measureText(fui::FontId, const char* text, fui::TextStyle) const override {
    return {static_cast<int16_t>(std::strlen(text) * 6), 12};
  }
  int16_t lineHeight(fui::FontId) const override { return 12; }
  void fill(fui::Rect, fui::Paint, uint8_t, uint8_t) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t, uint8_t) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint, fui::Rotation) override {}
  void text(fui::Rect rect, const char* text, fui::TextStyle style) override {
    EXPECT_EQ(style.maxLines, 1);
    lines.emplace_back(text);
    bounds.push_back(rect);
  }
};
}  // namespace

namespace {
// Monospace target: 6 px per byte, 12 px lines.
std::string pageText(const std::string& text, const storyText::Page& page) { return text.substr(0, page.length); }
}  // namespace

TEST(StoryTextLayout, NewlinesNeverReachGlyphRendererAndParagraphGapsRemain) {
  RecordingTarget target;
  fui::TextStyle style;
  storyText::drawNarrative(target, {8, 10, 500, 500}, "Before.\n\nYou take your coat and step outside.\n", style);
  ASSERT_EQ(target.lines.size(), 2u);  // The blank line advances y but draws nothing.
  EXPECT_EQ(target.lines[0], "Before.");
  EXPECT_EQ(target.lines[1], "You take your coat and step outside.");
  EXPECT_EQ(target.bounds[0].y, 10);
  EXPECT_EQ(target.bounds[1].y - target.bounds[0].y, 24);
  for (const auto& line : target.lines) EXPECT_EQ(line.find('\n'), std::string::npos);
}

TEST(StoryTextLayout, DrawsMoreThanSixteenLines) {
  RecordingTarget target;
  std::string text;
  for (int i = 0; i < 30; ++i) text += "line\n";
  storyText::drawNarrative(target, {0, 0, 300, 30 * 12}, text.c_str(), fui::TextStyle{});
  EXPECT_EQ(target.lines.size(), 30u);
}

TEST(StoryTextLayout, WrapsWithinPortraitAndLandscapeBands) {
  for (const fui::Rect area : {fui::Rect{12, 12, 180, 480}, fui::Rect{30, 8, 400, 180}}) {
    RecordingTarget target;
    storyText::drawNarrative(target, area, "The lantern shines.\nThe tide is returning.\n\nA fox waits by the boat.",
                             fui::TextStyle{});
    ASSERT_FALSE(target.lines.empty());
    EXPECT_EQ(target.bounds.front().y, area.y);
    for (size_t i = 0; i < target.bounds.size(); ++i) {
      const auto rect = target.bounds[i];
      EXPECT_GE(rect.x, area.x);
      EXPECT_GE(rect.y, area.y);
      EXPECT_LE(rect.right(), area.right());
      EXPECT_LE(rect.bottom(), area.bottom());
      EXPECT_LE(target.measureText(0, target.lines[i].c_str(), {}).width, area.width);
    }
  }
}

TEST(StoryTextLayout, TrailingNewlineAddsNoLine) {
  RecordingTarget target;
  EXPECT_EQ(storyText::lineCount(target, {}, "One.\nTwo.\n", 10, 600), 2u);
  EXPECT_EQ(storyText::lineCount(target, {}, "One.\n\nTwo.\n", 11, 600), 3u);
}

TEST(StoryTextLayout, WholeTextFitsOnOnePage) {
  RecordingTarget target;
  const std::string text = "Short story.\nThe end.\n";
  const auto page = storyText::paginate(target, {}, text.c_str(), text.size(), 600, 10);
  EXPECT_TRUE(page.last);
  EXPECT_EQ(page.length, text.size());
  EXPECT_EQ(page.lines, 2u);
}

TEST(StoryTextLayout, PrefersParagraphEndNearPageBottom) {
  RecordingTarget target;
  // 60 px = 10 chars per line, 10 lines per page.
  std::string text;
  for (int i = 0; i < 8; ++i) text += "aaaa bbbb\n";  // 8 one-line paragraphs
  text += "cccc dddd eeee ffff gggg hhhh.\n";       // 3 lines, would cross the bottom
  const auto page = storyText::paginate(target, {}, text.c_str(), text.size(), 60, 10);
  EXPECT_FALSE(page.last);
  EXPECT_EQ(page.lines, 8u);
  EXPECT_EQ(pageText(text, page).back(), '\n');
  EXPECT_EQ(text.substr(page.length, 4), "cccc");
}

TEST(StoryTextLayout, FallsBackToSentenceEnd) {
  RecordingTarget target;
  // One long paragraph; sentence ends inside the soft-break band.
  std::string text;
  for (int i = 0; i < 8; ++i) text += "word word ";
  text += "end. Next sentence keeps going on and on and on and on.";
  const auto page = storyText::paginate(target, {}, text.c_str(), text.size(), 60, 10);
  EXPECT_FALSE(page.last);
  EXPECT_EQ(text.substr(page.length, 4), "Next");
  EXPECT_NE(pageText(text, page).find("end. "), std::string::npos);
}

TEST(StoryTextLayout, SentenceEndHandlesClosingQuotes) {
  const std::string text = "Er sagt: \xC2\xBBGeh.\xC2\xAB Dann geht er.";
  const size_t cut = storyText::sentenceEnd(text.c_str(), 0, 20);
  EXPECT_EQ(text.substr(cut, 4), "Dann");
}

TEST(StoryTextLayout, FallsBackToWordBoundaryAndCoversAllText) {
  RecordingTarget target;
  std::string text;
  for (int i = 0; i < 200; ++i) text += "sea \xE4\xB8\x96\xE7\x95\x8C ";  // no sentence or paragraph ends
  std::string rebuilt;
  size_t offset = 0;
  for (int guard = 0; offset < text.size() && guard < 200; ++guard) {
    const auto page = storyText::paginate(target, {}, text.c_str() + offset, text.size() - offset, 120, 6);
    ASSERT_GT(page.length, 0u);
    ASSERT_LE(page.lines, 6u);
    EXPECT_NE(static_cast<unsigned char>(text[offset + page.length - (page.last ? 0 : 0)]) & 0xc0, 0x80);
    rebuilt += text.substr(offset, page.length);
    offset += page.length;
  }
  EXPECT_EQ(rebuilt, text);
}

TEST(StoryTextLayout, LastLineLeavesRoomForMarker) {
  RecordingTarget target;
  // 10 chars per full line (60 px); the last of 3 lines keeps 30 px free.
  const std::string text = "aaaa bbbb cccc dddd eeee ffff gggg hhhh";
  const auto page = storyText::paginate(target, {}, text.c_str(), text.size(), 60, 3, 30);
  EXPECT_FALSE(page.last);
  EXPECT_EQ(pageText(text, page), "aaaa bbbb cccc dddd eeee ");  // Last line holds one word.
  target.lines.clear();
  storyText::drawNarrative(target, {0, 0, 60, 36}, text.c_str(), page.length, {}, 3, 30);
  ASSERT_EQ(target.lines.size(), 3u);
  EXPECT_EQ(target.lines[2], "eeee");
}
