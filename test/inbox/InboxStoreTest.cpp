#include <gtest/gtest.h>

#include <cstdio>
#include <string>

#include "HalStorage.h"
#include "inbox/InboxStore.h"
#include "util/UsbInbox.h"

namespace {
inbox::Page page(const char* owner, const char* id, uint32_t revision, const char* title, const char* body = "") {
  inbox::Page p;
  std::snprintf(p.owner, sizeof(p.owner), "%s", owner);
  std::snprintf(p.id, sizeof(p.id), "%s", id);
  p.revision = revision;
  std::snprintf(p.title, sizeof(p.title), "%s", title);
  std::snprintf(p.body, sizeof(p.body), "%s", body);
  return p;
}

std::string hex(const std::string& text) {
  std::string out;
  char byte[3];
  for (unsigned char c : text) {
    std::snprintf(byte, sizeof(byte), "%02x", c);
    out += byte;
  }
  return out;
}

class InboxStoreTest : public ::testing::Test {
 protected:
  void SetUp() override { fake::reset(); }
};
}  // namespace

TEST_F(InboxStoreTest, StoresAndLoadsAPage) {
  EXPECT_EQ(inbox::receive(page("dex", "build", 1, "Build", "done\nhost: dex")), inbox::Result::Stored);
  inbox::Page loaded;
  ASSERT_TRUE(inbox::load("dex", "build", loaded));
  EXPECT_STREQ(loaded.title, "Build");
  EXPECT_STREQ(loaded.body, "done\nhost: dex");
  EXPECT_EQ(loaded.revision, 1u);
  EXPECT_EQ(inbox::unreadCount(), 1u);
}

TEST_F(InboxStoreTest, RevisionRules) {
  ASSERT_EQ(inbox::receive(page("dex", "a", 2, "T", "x")), inbox::Result::Stored);
  EXPECT_EQ(inbox::receive(page("dex", "a", 2, "T", "x")), inbox::Result::Duplicate);
  EXPECT_EQ(inbox::receive(page("dex", "a", 2, "T", "y")), inbox::Result::Conflict);
  EXPECT_EQ(inbox::receive(page("dex", "a", 1, "T", "old")), inbox::Result::Stale);
  ASSERT_TRUE(inbox::markRead("dex", "a"));
  EXPECT_EQ(inbox::unreadCount(), 0u);
  EXPECT_EQ(inbox::receive(page("dex", "a", 3, "T2", "new")), inbox::Result::Stored);
  EXPECT_EQ(inbox::unreadCount(), 1u);  // a new revision is unread again
  inbox::Page loaded;
  ASSERT_TRUE(inbox::load("dex", "a", loaded));
  EXPECT_STREQ(loaded.body, "new");
}

TEST_F(InboxStoreTest, DeletionLeavesATombstone) {
  ASSERT_EQ(inbox::receive(page("dex", "a", 1, "T")), inbox::Result::Stored);
  ASSERT_TRUE(inbox::remove("dex", "a"));
  inbox::Page loaded;
  EXPECT_FALSE(inbox::load("dex", "a", loaded));
  EXPECT_EQ(inbox::receive(page("dex", "a", 9, "T")), inbox::Result::Deleted);
  inbox::Entry entries[4];
  EXPECT_EQ(inbox::list(entries, 4), 0u);
  ASSERT_EQ(inbox::list(entries, 4, true), 1u);
  EXPECT_TRUE(entries[0].deleted);
}

TEST_F(InboxStoreTest, OwnersHaveSeparateIds) {
  ASSERT_EQ(inbox::receive(page("dex", "a", 1, "Dex")), inbox::Result::Stored);
  ASSERT_EQ(inbox::receive(page("ztx", "a", 1, "Ztx")), inbox::Result::Stored);
  ASSERT_TRUE(inbox::remove("dex", "a"));
  inbox::Page loaded;
  ASSERT_TRUE(inbox::load("ztx", "a", loaded));
  EXPECT_STREQ(loaded.title, "Ztx");
}

TEST_F(InboxStoreTest, ListsNewestFirst) {
  inbox::receive(page("dex", "one", 1, "One"));
  inbox::receive(page("dex", "two", 1, "Two"));
  inbox::receive(page("dex", "one", 2, "One again"));
  inbox::Entry entries[4];
  ASSERT_EQ(inbox::list(entries, 4), 2u);
  EXPECT_STREQ(entries[0].title, "One again");
  EXPECT_STREQ(entries[1].title, "Two");
  EXPECT_TRUE(entries[0].unread);
}

TEST_F(InboxStoreTest, FullRefusesNewIdentitiesButAcceptsUpdates) {
  char id[8];
  for (size_t i = 0; i < inbox::CAPACITY; ++i) {
    std::snprintf(id, sizeof(id), "p%zu", i);
    ASSERT_EQ(inbox::receive(page("dex", id, 1, "T")), inbox::Result::Stored);
  }
  ASSERT_TRUE(inbox::remove("dex", "p0"));  // tombstones still count
  EXPECT_EQ(inbox::receive(page("dex", "new", 1, "T")), inbox::Result::Full);
  EXPECT_EQ(inbox::receive(page("dex", "p1", 2, "T")), inbox::Result::Stored);
}

TEST_F(InboxStoreTest, RejectsBadKeys) {
  EXPECT_FALSE(inbox::validKey(""));
  EXPECT_FALSE(inbox::validKey(".hidden"));
  EXPECT_FALSE(inbox::validKey("a+b"));
  EXPECT_FALSE(inbox::validKey("../x"));
  EXPECT_FALSE(inbox::validKey("Dex"));
  EXPECT_TRUE(inbox::validKey("store-pass_2.x"));
}

TEST_F(InboxStoreTest, ParsesPut) {
  const std::string line = "XT1 INBOX PUT 7 " + hex("dex") + " " + hex("build") + " 3 " + hex("Grüße") + " " +
                           hex("line one\nline two");
  usbInbox::Command command;
  ASSERT_TRUE(usbInbox::parse(line.c_str(), command));
  EXPECT_EQ(command.kind, usbInbox::Kind::Put);
  EXPECT_EQ(command.id, 7u);
  EXPECT_STREQ(command.page.owner, "dex");
  EXPECT_STREQ(command.page.id, "build");
  EXPECT_EQ(command.page.revision, 3u);
  EXPECT_STREQ(command.page.title, "Grüße");
  EXPECT_STREQ(command.page.body, "line one\nline two");
}

TEST_F(InboxStoreTest, ParsesEmptyBodyAndList) {
  usbInbox::Command command;
  ASSERT_TRUE(usbInbox::parse(("XT1 INBOX PUT 1 " + hex("dex") + " " + hex("a") + " 1 " + hex("T") + " -").c_str(),
                              command));
  EXPECT_STREQ(command.page.body, "");
  ASSERT_TRUE(usbInbox::parse("XT1 INBOX LIST 12", command));
  EXPECT_EQ(command.kind, usbInbox::Kind::List);
  EXPECT_EQ(command.id, 12u);
}

TEST_F(InboxStoreTest, RejectsBadPut) {
  usbInbox::Command command;
  const std::string base = "XT1 INBOX PUT 1 ";
  EXPECT_FALSE(usbInbox::parse((base + hex("Dex") + " " + hex("a") + " 1 " + hex("T") + " -").c_str(), command));
  EXPECT_FALSE(usbInbox::parse((base + hex("dex") + " " + hex("a") + " 0 " + hex("T") + " -").c_str(), command));
  EXPECT_FALSE(usbInbox::parse((base + hex("dex") + " " + hex("a") + " 1 " + hex("T\nx") + " -").c_str(), command));
  EXPECT_FALSE(usbInbox::parse((base + hex("dex") + " " + hex("a") + " 1 " + hex(std::string(65, 'x')) + " -").c_str(),
                               command));
  EXPECT_FALSE(usbInbox::parse((base + hex("dex") + " " + hex("a") + " 1 " + hex("T") + " " +
                                hex(std::string(inbox::BODY_BYTES + 1, 'x')))
                                   .c_str(),
                               command));
  EXPECT_FALSE(usbInbox::parse("XT1 INBOX LIST 0", command));
}

TEST_F(InboxStoreTest, LongestPutFitsOneConsoleLine) {
  const std::string line = "XT1 INBOX PUT 4294967295 " + hex(std::string(inbox::KEY_BYTES, 'a')) + " " +
                           hex(std::string(inbox::KEY_BYTES, 'b')) + " 2147483647 " +
                           hex(std::string(inbox::TITLE_BYTES, 'T')) + " " + hex(std::string(inbox::BODY_BYTES, 'x'));
  EXPECT_LT(line.size(), 1728u);  // UsbConsoleLine::CAPACITY
  usbInbox::Command command;
  EXPECT_TRUE(usbInbox::parse(line.c_str(), command));
}
