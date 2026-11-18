#include <gtest/gtest.h>

#include <string>

#include "util/UsbFileTransfer.h"

using usbFile::Command;
using usbFile::Kind;

TEST(UsbFileTransfer, ParsesBegin) {
  Command c;
  // "low-tide.bin", 1504 bytes
  ASSERT_TRUE(usbFile::parse("XT1 FILE BEGIN 7 6c6f772d746964652e62696e 1504 0a0B0c0d", c));
  EXPECT_EQ(c.kind, Kind::Begin);
  EXPECT_EQ(c.id, 7u);
  EXPECT_STREQ(c.name, "low-tide.bin");
  EXPECT_EQ(c.size, 1504u);
  EXPECT_EQ(c.crc, 0x0a0b0c0du);
}

TEST(UsbFileTransfer, RejectsUnsafeNamesAndSizes) {
  Command c;
  EXPECT_FALSE(usbFile::parse("XT1 FILE BEGIN 1 2e2e2f782e62696e 10 00000000", c));  // ../x.bin
  EXPECT_FALSE(usbFile::parse("XT1 FILE BEGIN 1 2e782e62696e 10 00000000", c));      // .x.bin
  EXPECT_FALSE(usbFile::parse("XT1 FILE BEGIN 1 782e696e6b 10 00000000", c));        // x.ink
  EXPECT_FALSE(usbFile::parse("XT1 FILE BEGIN 1 582e62696e 10 00000000", c));        // X.bin
  EXPECT_FALSE(usbFile::parse("XT1 FILE BEGIN 1 782e62696e 262145 00000000", c));    // > 256 KiB
  EXPECT_FALSE(usbFile::parse("XT1 FILE BEGIN 0 782e62696e 10 00000000", c));        // id 0
  EXPECT_FALSE(usbFile::parse("XT1 FILE BEGIN 1 782e62696e 10 000000", c));          // short crc
}

TEST(UsbFileTransfer, ParsesDataAndEnd) {
  Command c;
  ASSERT_TRUE(usbFile::parse("XT1 FILE DATA 3 0 00ff10", c));
  EXPECT_EQ(c.kind, Kind::Data);
  EXPECT_EQ(c.offset, 0u);
  ASSERT_EQ(c.dataLength, 3u);
  EXPECT_EQ(c.data[1], 0xff);
  ASSERT_TRUE(usbFile::parse("XT1 FILE END 3", c));
  EXPECT_EQ(c.kind, Kind::End);
  EXPECT_FALSE(usbFile::parse("XT1 FILE DATA 3 0 0", c));
  EXPECT_FALSE(usbFile::parse("XT1 FILE DATA 3 01 00", c));
  EXPECT_FALSE(usbFile::parse("XT1 FILE END 3 x", c));
}

TEST(UsbFileTransfer, BoundsChunkSize) {
  Command c;
  const std::string max = "XT1 FILE DATA 1 0 " + std::string(usbFile::CHUNK_BYTES * 2, 'a');
  EXPECT_TRUE(usbFile::parse(max.c_str(), c));
  EXPECT_LT(max.size(), 1727u);
  const std::string over = max + "aa";
  EXPECT_FALSE(usbFile::parse(over.c_str(), c));
}

TEST(UsbFileTransfer, Crc32MatchesZlib) {
  const char text[] = "123456789";
  EXPECT_EQ(usbFile::crc32(0, reinterpret_cast<const uint8_t*>(text), 9), 0xcbf43926u);
  EXPECT_EQ(usbFile::crc32(usbFile::crc32(0, reinterpret_cast<const uint8_t*>(text), 4),
                           reinterpret_cast<const uint8_t*>(text + 4), 5),
            0xcbf43926u);
}

TEST(UsbFileTransfer, RoutesUploadPaths) {
  char path[usbFile::PATH_BYTES];
  ASSERT_TRUE(usbFile::uploadPath("low-tide.bin", path, sizeof(path)));
  EXPECT_STREQ(path, "/stories/low-tide.bin");
  ASSERT_TRUE(usbFile::uploadPath("sleep-overlay.png", path, sizeof(path)));
  EXPECT_STREQ(path, "/sleep-overlay.png");
  ASSERT_TRUE(usbFile::uploadPath("sleep-overlay.bmp", path, sizeof(path)));
  EXPECT_STREQ(path, "/sleep-overlay.bmp");
  EXPECT_FALSE(usbFile::uploadPath("other.png", path, sizeof(path)));
  EXPECT_FALSE(usbFile::uploadPath("sleep-overlay.png", path, 18));  // no room for NUL

  Command c;
  // "sleep-overlay.png"
  ASSERT_TRUE(usbFile::parse("XT1 FILE BEGIN 2 736c6565702d6f7665726c61792e706e67 1970 00000000", c));
  EXPECT_STREQ(c.name, "sleep-overlay.png");
  EXPECT_FALSE(usbFile::parse("XT1 FILE BEGIN 2 782e706e67 10 00000000", c));  // x.png
  // Delete stays story-only.
  EXPECT_FALSE(usbFile::parse("XT1 FILE DELETE 2 736c6565702d6f7665726c61792e706e67", c));
}

TEST(UsbFileTransfer, ParsesListAndDelete) {
  Command c;
  ASSERT_TRUE(usbFile::parse("XT1 FILE LIST 9", c));
  EXPECT_EQ(c.kind, Kind::List);
  EXPECT_EQ(c.id, 9u);
  EXPECT_FALSE(usbFile::parse("XT1 FILE LIST 9 x", c));
  EXPECT_FALSE(usbFile::parse("XT1 FILE LIST 0", c));

  ASSERT_TRUE(usbFile::parse("XT1 FILE DELETE 4 6c6f772d746964652e62696e", c));
  EXPECT_EQ(c.kind, Kind::Delete);
  EXPECT_EQ(c.id, 4u);
  EXPECT_STREQ(c.name, "low-tide.bin");
  EXPECT_FALSE(usbFile::parse("XT1 FILE DELETE 4 2e2e2f782e62696e", c));  // ../x.bin
  EXPECT_FALSE(usbFile::parse("XT1 FILE DELETE 4 2e7361766520", c));      // .save
  EXPECT_FALSE(usbFile::parse("XT1 FILE DELETE 4", c));
}
