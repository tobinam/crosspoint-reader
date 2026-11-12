#include <gtest/gtest.h>

#include <cstring>

#include <fstream>
#include <iterator>
#include <vector>

#include "stories/InkStorySession.h"

namespace {
std::vector<uint8_t> example() {
  std::ifstream file(STORY_EXAMPLE_PATH, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
}  // namespace

TEST(InkStorySession, LanternBranchTracksInventory) {
  const auto bytes = example();
  InkStorySession session;
  ASSERT_TRUE(session.load(bytes.data(), bytes.size()));
  ASSERT_EQ(session.choices(), 2u);
  EXPECT_NE(std::strstr(session.text(), "tide"), nullptr);
  ASSERT_TRUE(session.choose(0));  // lighthouse
  ASSERT_TRUE(session.choose(0));  // borrow lantern, then sand
  ASSERT_EQ(session.choices(), 3u);
  ASSERT_TRUE(session.choose(1));  // missing light
  EXPECT_NE(std::strstr(session.text(), "brass lantern"), nullptr);
  ASSERT_TRUE(session.choose(0));
  EXPECT_EQ(session.choices(), 0u);
  EXPECT_NE(std::strstr(session.text(), "A light for strangers"), nullptr);
  EXPECT_FALSE(session.choose(0));
}

TEST(InkStorySession, VoyageAndHomeEndingsAndReload) {
  const auto bytes = example();
  InkStorySession session;
  ASSERT_TRUE(session.load(bytes.data(), bytes.size()));
  ASSERT_TRUE(session.choose(1));  // sand
  ASSERT_TRUE(session.choose(0));  // crossing
  ASSERT_TRUE(session.choose(0));  // voyage
  EXPECT_NE(std::strstr(session.text(), "An unfinished map"), nullptr);
  ASSERT_TRUE(session.load(bytes.data(), bytes.size()));
  ASSERT_TRUE(session.choose(1));
  ASSERT_TRUE(session.choose(2));
  EXPECT_NE(std::strstr(session.text(), "A place to return to"), nullptr);
  session.clear();
  EXPECT_EQ(session.choices(), 0u);
  EXPECT_STREQ(session.text(), "");
}

TEST(InkStorySession, RejectsWrongAndTruncatedHeadersBeforeVm) {
  auto bytes = example();
  ASSERT_TRUE(InkStorySession::validHeader(bytes.data(), bytes.size()));
  EXPECT_FALSE(InkStorySession::validHeader(bytes.data(), 63));
  EXPECT_FALSE(InkStorySession::validHeader(bytes.data(), bytes.size() - 100));
  bytes[0] = 0;
  EXPECT_FALSE(InkStorySession::validHeader(bytes.data(), bytes.size()));
  bytes = example();
  bytes[6] = 99;
  EXPECT_FALSE(InkStorySession::validHeader(bytes.data(), bytes.size()));
  bytes = example();
  bytes[8] = 1;
  EXPECT_FALSE(InkStorySession::validHeader(bytes.data(), bytes.size()));
}

TEST(InkStorySession, ReadsLanguageGlobalTag) {
  auto bytes = example();
  InkStorySession session;
  ASSERT_TRUE(session.load(bytes.data(), bytes.size()));
  EXPECT_STREQ(session.language(), "en");
  session.clear();
  EXPECT_STREQ(session.language(), "");
}

namespace {
struct VectorWriter final : ink::runtime::snapshot::writer {
  std::vector<uint8_t> data;
  bool write(const void* bytes, ink::size_t length) override {
    const auto* p = static_cast<const uint8_t*>(bytes);
    data.insert(data.end(), p, p + length);
    return true;
  }
};
}  // namespace

TEST(InkStorySession, SnapshotRestoreKeepsInventoryAndChoices) {
  const auto bytes = example();
  InkStorySession original;
  ASSERT_TRUE(original.load(bytes.data(), bytes.size()));
  ASSERT_TRUE(original.choose(0));  // lighthouse
  ASSERT_TRUE(original.choose(0));  // borrow lantern, then sand: has_lantern = true
  ASSERT_EQ(original.choices(), 3u);
  const std::string text = original.text();
  std::vector<std::string> choices;
  for (size_t i = 0; i < original.choices(); ++i) choices.emplace_back(original.choiceText(i));

  VectorWriter writer;
  ASSERT_TRUE(original.saveSnapshot(writer));
  original.clear();

  InkStorySession restored;
  auto snapshot = writer.data;
  ASSERT_TRUE(restored.restore(bytes.data(), bytes.size(), snapshot.data(), snapshot.size(), text.data(), text.size(),
                               "en"));
  std::fill(snapshot.begin(), snapshot.end(), 0);  // VM must not borrow the snapshot buffer.
  EXPECT_EQ(restored.text(), text);
  EXPECT_STREQ(restored.language(), "en");
  ASSERT_EQ(restored.choices(), choices.size());
  for (size_t i = 0; i < choices.size(); ++i) EXPECT_EQ(restored.choiceText(i), choices[i]);
  ASSERT_TRUE(restored.choose(1));  // missing light: needs the restored inventory
  EXPECT_NE(std::strstr(restored.text(), "brass lantern"), nullptr);
  ASSERT_TRUE(restored.choose(0));
  EXPECT_NE(std::strstr(restored.text(), "A light for strangers"), nullptr);
}

TEST(InkStorySession, RestoreRejectsGarbage) {
  const auto bytes = example();
  InkStorySession session;
  const uint8_t junk[16] = {1, 2, 3};
  EXPECT_FALSE(session.restore(bytes.data(), bytes.size(), junk, 0, "", 0, ""));
}
