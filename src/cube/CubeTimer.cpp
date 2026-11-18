#if XTEINK_CUBE_TIMER || CUBE_HOST_TEST
#include "CubeTimer.h"

#include <algorithm>
#include <cstdio>

namespace cube {

bool Seat::press(const bool arm, const int64_t t) {
  if (phase == Phase::Running) {
    lastUs = t - since;
    if (bestUs == NO_TIME || lastUs < bestUs) bestUs = lastUs;
    std::copy(recentUs + 1, recentUs + 5, recentUs);
    recentUs[4] = lastUs;
    ++solves;
    phase = Phase::Idle;
    return true;
  }
  if (arm && phase == Phase::Idle) {
    phase = Phase::Holding;
    since = t;
  }
  return false;
}

void Seat::release(const bool arm, const int64_t t) {
  if (!arm) return;
  if (phase == Phase::Holding) phase = Phase::Idle;
  if (phase == Phase::Ready) {
    phase = Phase::Running;
    since = t;
  }
}

void Seat::tick(const int64_t now) {
  if (phase == Phase::Holding && now - since >= HOLD_US) phase = Phase::Ready;
}

int64_t Seat::shownUs(const int64_t now) const { return phase == Phase::Running ? now - since : lastUs; }

int64_t Seat::average5() const {
  if (recentUs[0] == NO_TIME) return NO_TIME;
  const auto [low, high] = std::minmax_element(recentUs, recentUs + 5);
  int64_t sum = 0;
  for (const auto us : recentUs) sum += us;
  return (sum - *low - *high) / 3;
}

bool Match::press(const int seat, const bool arm, const int64_t t) {
  tick(t);
  if (stage == Stage::Wait) {
    nextRound();
    return false;
  }
  if (stage != Stage::Play || roundUs[seat] != NO_TIME) return false;
  if (!seats[seat].press(arm, t)) return false;
  roundUs[seat] = seats[seat].lastUs;
  return true;
}

void Match::release(const int seat, const bool arm, const int64_t t) {
  tick(t);
  if (stage == Stage::Play) seats[seat].release(arm, t);
}

void Match::tick(const int64_t now) {
  for (auto& seat : seats) seat.tick(now);
  if (stage == Stage::Play && !busy(0) && !busy(1) && (roundUs[0] != NO_TIME || roundUs[1] != NO_TIME)) {
    stage = Stage::Result;
    resultSince = now;
  }
  if (stage == Stage::Result && frame(now) >= RESULT_FRAMES) {
    if (autoNext)
      nextRound();
    else
      stage = Stage::Wait;
  }
}

int Match::winner() const {
  if (roundUs[0] == NO_TIME || roundUs[1] == NO_TIME || roundUs[0] == roundUs[1]) return -1;
  return roundUs[0] < roundUs[1] ? 0 : 1;
}

void Match::nextRound() {
  ++round;
  roundUs[0] = roundUs[1] = NO_TIME;
  stage = Stage::Play;
}

namespace {
uint64_t splitmix(uint64_t& state) {
  uint64_t z = (state += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}
}  // namespace

void scramble(const uint64_t seed, const uint32_t n, char (&out)[SCRAMBLE_BYTES]) {
  static constexpr char FACES[] = "UDRLFB";  // axis = index / 2
  static constexpr const char* TURNS[] = {"", "'", "2"};
  uint64_t state = seed ^ (static_cast<uint64_t>(n) * 0xD1B54A32D192ED03ull);
  int previous = -1;
  int beforePrevious = -1;
  size_t length = 0;
  for (size_t i = 0; i < SCRAMBLE_MOVES; ++i) {
    int face;
    // No face twice in a row, and no third move on one axis (R L R is R2 L).
    do {
      face = static_cast<int>(splitmix(state) % 6);
    } while (face == previous || (previous >= 0 && beforePrevious >= 0 && face / 2 == previous / 2 &&
                                  face / 2 == beforePrevious / 2));
    const char* turn = TURNS[splitmix(state) % 3];
    const char* separator = i == 0 ? "" : i == SCRAMBLE_MOVES / 2 ? "\n" : " ";
    length += std::snprintf(out + length, SCRAMBLE_BYTES - length, "%s%c%s", separator, FACES[face], turn);
    beforePrevious = previous;
    previous = face;
  }
}

void formatTime(const int64_t us, const int decimals, char* out, const size_t size) {
  if (us < 0) {
    std::snprintf(out, size, "-");
    return;
  }
  const int64_t ms = us / 1000;
  const long minutes = static_cast<long>(ms / 60000);
  const int seconds = static_cast<int>(ms / 1000 % 60);
  const int fraction = decimals == 3 ? static_cast<int>(ms % 1000) : static_cast<int>(ms % 1000 / 100);
  if (minutes)
    std::snprintf(out, size, "%ld:%02d.%0*d", minutes, seconds, decimals, fraction);
  else
    std::snprintf(out, size, "%d.%0*d", seconds, decimals, fraction);
}

}  // namespace cube
#endif
