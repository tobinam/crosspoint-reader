#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>

#include "HalStorage.h"
#include "cube/CubeLog.h"
#include "cube/CubeTimer.h"

using cube::Phase;
using cube::Seat;

namespace {
constexpr int64_t MS = 1000;

// Hold the arm key long enough, release at start, stop at start + solve.
void solve(Seat& seat, int64_t start, int64_t solveUs, bool stopWithArm = false) {
  seat.press(true, start - cube::HOLD_US - MS);
  seat.tick(start);
  seat.release(true, start);
  seat.tick(start + solveUs);
  EXPECT_TRUE(seat.press(stopWithArm, start + solveUs));
  seat.release(stopWithArm, start + solveUs + 50 * MS);
}
}  // namespace

TEST(CubeSeat, ShortHoldDoesNotStart) {
  Seat seat;
  seat.press(true, 0);
  seat.tick(cube::HOLD_US - 1);
  EXPECT_EQ(seat.phase, Phase::Holding);
  seat.release(true, cube::HOLD_US - 1);
  EXPECT_EQ(seat.phase, Phase::Idle);
}

TEST(CubeSeat, StartsOnReleaseAndStopsOnEitherKey) {
  Seat seat;
  solve(seat, 1000 * MS, 12345 * MS);
  EXPECT_EQ(seat.lastUs, 12345 * MS);
  EXPECT_EQ(seat.phase, Phase::Idle);
  solve(seat, 30000 * MS, 9876 * MS, true);
  EXPECT_EQ(seat.lastUs, 9876 * MS);
  EXPECT_EQ(seat.bestUs, 9876 * MS);
  EXPECT_EQ(seat.solves, 2u);
}

TEST(CubeSeat, OtherKeyDoesNotArm) {
  Seat seat;
  seat.press(false, 0);
  seat.tick(cube::HOLD_US * 2);
  EXPECT_EQ(seat.phase, Phase::Idle);
}

TEST(CubeSeat, AverageOfFiveDropsBestAndWorst) {
  Seat seat;
  const int64_t times[] = {10, 20, 11, 12, 1};
  for (int i = 0; i < 4; ++i) solve(seat, (i + 1) * 100000 * MS, times[i] * 1000 * MS);
  EXPECT_EQ(seat.average5(), cube::NO_TIME);
  solve(seat, 600000 * MS, times[4] * 1000 * MS);
  EXPECT_EQ(seat.average5(), 11000 * MS);
}

namespace {
// Seat i starts at start and stops after solveUs, through the match.
void race(cube::Match& match, int i, int64_t start, int64_t solveUs) {
  match.press(i, true, start - cube::HOLD_US - MS);
  match.release(i, true, start);
  EXPECT_TRUE(match.press(i, false, start + solveUs));
  match.release(i, false, start + solveUs + MS);
}
}  // namespace

TEST(CubeMatch, BothSolveThenWinnerThenNextRound) {
  cube::Match match;
  match.press(0, true, 0);
  match.press(1, true, 0);
  match.release(0, true, cube::HOLD_US + MS);
  match.release(1, true, cube::HOLD_US + MS);
  EXPECT_TRUE(match.press(1, false, 10000 * MS));
  match.tick(10001 * MS);
  EXPECT_EQ(match.stage, cube::Match::Stage::Play);  // seat 0 still running
  EXPECT_TRUE(match.press(0, true, 12000 * MS));
  match.tick(12001 * MS);
  EXPECT_EQ(match.stage, cube::Match::Stage::Result);
  EXPECT_EQ(match.winner(), 1);
  match.tick(12001 * MS + cube::RESULT_FRAMES * cube::FRAME_US);
  EXPECT_EQ(match.stage, cube::Match::Stage::Play);
  EXPECT_EQ(match.round, 1u);
  EXPECT_EQ(match.roundUs[0], cube::NO_TIME);
}

TEST(CubeMatch, SoloRoundAndWaitForKey) {
  cube::Match match;
  match.autoNext = false;
  race(match, 0, 1000 * MS, 9000 * MS);
  match.tick(20000 * MS);
  EXPECT_EQ(match.winner(), -1);
  match.tick(20000 * MS + cube::RESULT_FRAMES * cube::FRAME_US);
  EXPECT_EQ(match.stage, cube::Match::Stage::Wait);
  // The key that ends the wait does not arm.
  EXPECT_FALSE(match.press(1, true, 30000 * MS));
  EXPECT_EQ(match.stage, cube::Match::Stage::Play);
  EXPECT_EQ(match.seats[1].phase, Phase::Idle);
  EXPECT_EQ(match.round, 1u);
}

TEST(CubeMatch, FinishedSeatCannotSolveTheRoundAgain) {
  cube::Match match;
  match.press(1, true, 0);  // seat 1 holds, so the round stays open
  race(match, 0, 1000 * MS, 5000 * MS);
  EXPECT_FALSE(match.press(0, true, 7000 * MS));
  EXPECT_EQ(match.seats[0].phase, Phase::Idle);
}

TEST(CubeScramble, SameSeedSameScrambleAndNoRedundantMoves) {
  char a[cube::SCRAMBLE_BYTES], b[cube::SCRAMBLE_BYTES];
  std::set<std::string> distinct;
  for (uint32_t n = 0; n < 200; ++n) {
    cube::scramble(42, n, a);
    cube::scramble(42, n, b);
    EXPECT_STREQ(a, b);
    distinct.insert(a);
    std::string text(a);
    for (auto& c : text)
      if (c == '\n') c = ' ';
    std::string faces;
    for (size_t i = 0; i < text.size(); ++i)
      if (std::strchr("UDRLFB", text[i])) faces += text[i];
    ASSERT_EQ(faces.size(), cube::SCRAMBLE_MOVES);
    const auto axis = [](char f) { return std::string("UDRLFB").find(f) / 2; };
    for (size_t i = 1; i < faces.size(); ++i) {
      EXPECT_NE(faces[i], faces[i - 1]);
      if (i >= 2) {
        EXPECT_FALSE(axis(faces[i]) == axis(faces[i - 1]) && axis(faces[i]) == axis(faces[i - 2])) << a;
      }
    }
  }
  EXPECT_EQ(distinct.size(), 200u);
}

TEST(CubeFormat, Times) {
  char out[16];
  cube::formatTime(12345 * MS, 3, out, sizeof(out));
  EXPECT_STREQ(out, "12.345");
  cube::formatTime(62345 * MS, 3, out, sizeof(out));
  EXPECT_STREQ(out, "1:02.345");
  cube::formatTime(12599 * MS, 1, out, sizeof(out));
  EXPECT_STREQ(out, "12.5");
  cube::formatTime(cube::NO_TIME, 3, out, sizeof(out));
  EXPECT_STREQ(out, "-");
}

class CubeLogTest : public ::testing::Test {
 protected:
  void SetUp() override { fake::reset(); }
};

TEST_F(CubeLogTest, LineRoundTrips) {
  char line[cube::LOG_LINE_BYTES];
  ASSERT_TRUE(cube::formatLogLine(line, sizeof(line), 1700000000, 0xABCDEFull, 'B', 7, 12345678, "R U\nF2"));
  EXPECT_STREQ(line, "1700000000,0000000000abcdef,B,7,12345,R U F2");
  char seat;
  int64_t us;
  ASSERT_TRUE(cube::parseLogLine(line, seat, us));
  EXPECT_EQ(seat, 'B');
  EXPECT_EQ(us, 12345 * MS);
  EXPECT_FALSE(cube::parseLogLine("garbage", seat, us));
}

TEST_F(CubeLogTest, BestsPerSeatFromLog) {
  int64_t a, b;
  cube::loadBests(a, b);
  EXPECT_EQ(a, cube::NO_TIME);
  EXPECT_EQ(b, cube::NO_TIME);
  char text[cube::SCRAMBLE_BYTES];
  cube::scramble(1, 0, text);
  ASSERT_TRUE(cube::appendSolve(0, 1, 'A', 0, 15000 * MS, text));
  ASSERT_TRUE(cube::appendSolve(0, 1, 'A', 1, 11000 * MS, text));
  ASSERT_TRUE(cube::appendSolve(0, 1, 'B', 0, 20000 * MS, text));
  cube::loadBests(a, b);
  EXPECT_EQ(a, 11000 * MS);
  EXPECT_EQ(b, 20000 * MS);
}
