#pragma once

// Cube timer solve log: /cube/solves.csv, one line per solve, kept for sai to collect over USB.
// Line: <unix time or 0>,<session seed hex>,<seat A|B>,<scramble number>,<ms>,<scramble on one line>

#include <cstddef>
#include <cstdint>

#include "CubeTimer.h"

namespace cube {

constexpr size_t LOG_LINE_BYTES = 128;

// The log line for a solve, without the newline; false if it does not fit.
bool formatLogLine(char* out, size_t size, int64_t unixTime, uint64_t session, char seat, uint32_t number,
                   int64_t us, const char* scrambleText);
// Seat and time of a log line; false for a line that is not a solve.
bool parseLogLine(const char* line, char& seat, int64_t& us);

// Appends one solve; a failed write leaves no torn line.
bool appendSolve(int64_t unixTime, uint64_t session, char seat, uint32_t number, int64_t us, const char* scrambleText);
// Best time per seat over the whole log (NO_TIME when none).
void loadBests(int64_t& bestA, int64_t& bestB);

// The next-round toggle, /cube/auto-next ("1"/"0"); on when missing.
bool loadAutoNext();
void saveAutoNext(bool on);

}  // namespace cube
