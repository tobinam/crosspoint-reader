// The X4 TV remote's chunk framing (src/tv/TvFraming.h) against the bridge's rules (sai:
// xteink/TV_REMOTE_PROTOCOL.md; the same cases as x4bridge/src/proto.rs and xteink/tv_sim.py).
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "tv/TvFraming.h"

namespace {
std::vector<std::vector<uint8_t>> split(tv::Chunker& c, const std::string& m, size_t room) {
  std::vector<std::vector<uint8_t>> out;
  c.split(m.data(), m.size(), room, [&](const uint8_t* d, size_t n) {
    out.emplace_back(d, d + n);
    return true;
  });
  return out;
}
}  // namespace

TEST(TvFraming, RoundTrip) {
  tv::Chunker c;
  tv::Joiner<64> j;
  const std::string m = "0123456789abcdefghij";
  const auto parts = split(c, m, 7);
  ASSERT_EQ(parts.size(), 4u);
  EXPECT_EQ(parts[3][0], 0x80);
  bool done = false;
  for (const auto& p : parts) done = j.push(p.data(), p.size());
  ASSERT_TRUE(done);
  EXPECT_EQ(std::string(j.message(), j.length()), m);
  EXPECT_EQ(split(c, "x", 7)[0][0], 0x81);  // the next message's number
}

TEST(TvFraming, DropsPartialAndTooLong) {
  tv::Joiner<4> j;
  const uint8_t a[] = {0x05, 'a'}, b[] = {0x86, 'b'};
  EXPECT_FALSE(j.push(a, 2));
  ASSERT_TRUE(j.push(b, 2));  // a new number: the partial 'a' is gone
  EXPECT_STREQ(j.message(), "b");
  const uint8_t l1[] = {0x07, '1', '2', '3'}, l2[] = {0x87, '4', '5'}, ok[] = {0x88, 'o', 'k'};
  EXPECT_FALSE(j.push(l1, 4));
  EXPECT_FALSE(j.push(l2, 3));  // 5 bytes > 4: dropped
  ASSERT_TRUE(j.push(ok, 3));
  EXPECT_STREQ(j.message(), "ok");
  EXPECT_FALSE(j.push(nullptr, 0));
}

TEST(TvFraming, MtuSizedAndEmpty) {
  tv::Chunker c;
  const auto parts = split(c, std::string(600, 'x'), 244);  // MTU 247
  ASSERT_EQ(parts.size(), 3u);
  for (const auto& p : parts) EXPECT_LE(p.size(), 244u);
  const auto empty = split(c, "", 20);
  ASSERT_EQ(empty.size(), 1u);
  EXPECT_EQ(empty[0].size(), 1u);
  EXPECT_EQ(empty[0][0] & 0x80, 0x80);
}
