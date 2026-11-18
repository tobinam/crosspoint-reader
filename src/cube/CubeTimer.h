#pragma once

// Cube timer core: one seat per player, 3x3 scrambles, averages. No hardware, no storage (host-tested).

#include <cstddef>
#include <cstdint>

namespace cube {

// Arm key held this long before its release starts the timer.
constexpr int64_t HOLD_US = 400000;
constexpr size_t SCRAMBLE_MOVES = 20;
// "R U' F2 " per move, newline after move 10, NUL.
constexpr size_t SCRAMBLE_BYTES = SCRAMBLE_MOVES * 3 + 1;
constexpr int64_t NO_TIME = -1;

enum class Phase : uint8_t { Idle, Holding, Ready, Running };

struct Seat {
  Phase phase = Phase::Idle;
  int64_t since = 0;  // hold start while Holding/Ready, start while Running
  int64_t lastUs = NO_TIME;
  int64_t bestUs = NO_TIME;
  uint32_t solves = 0;  // this session
  int64_t recentUs[5] = {NO_TIME, NO_TIME, NO_TIME, NO_TIME, NO_TIME};

  // A key of this seat went down at t; arm = the seat's outer key. True when it stopped a solve (lastUs is new).
  bool press(bool arm, int64_t t);
  void release(bool arm, int64_t t);
  // Promotes a long enough hold to Ready; call with each event's time before handling it, and with now.
  void tick(int64_t now);
  // Elapsed time while Running, else the last result (NO_TIME if none).
  int64_t shownUs(int64_t now) const;
  // WCA average of the last five: best and worst dropped, mean of the rest. NO_TIME before five solves.
  int64_t average5() const;
};

// Result screen: frames of the winner animation, then the next round (autoNext) or a wait for any key.
constexpr int RESULT_FRAMES = 6;
constexpr int64_t FRAME_US = 500000;

// Two seats racing rounds: one scramble per round, each seat solves it at most once.
struct Match {
  enum class Stage : uint8_t { Play, Result, Wait };
  Seat seats[2];
  Stage stage = Stage::Play;
  uint32_t round = 0;  // also the scramble number
  int64_t roundUs[2] = {NO_TIME, NO_TIME};
  int64_t resultSince = 0;
  bool autoNext = true;

  // True when this press stopped the seat's solve (its time is roundUs[seat]).
  bool press(int seat, bool arm, int64_t t);
  void release(int seat, bool arm, int64_t t);
  // Holds become Ready; a round with a result and nobody busy shows the result; the animation ends.
  void tick(int64_t now);
  bool busy(int seat) const { return seats[seat].phase != Phase::Idle; }
  // The faster seat when both solved, else -1.
  int winner() const;
  int frame(int64_t now) const { return static_cast<int>((now - resultSince) / FRAME_US); }

 private:
  void nextRound();
};

// Scramble number n of a session: both seats get the same sequence, so the n-th solves race on equal cubes.
void scramble(uint64_t seed, uint32_t n, char (&out)[SCRAMBLE_BYTES]);

// "12.345" / "1:02.345" with 3 decimals, "12.3" with 1; truncated. NO_TIME gives "-".
void formatTime(int64_t us, int decimals, char* out, size_t size);

}  // namespace cube
