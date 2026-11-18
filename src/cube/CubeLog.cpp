#if XTEINK_CUBE_TIMER || CUBE_HOST_TEST
#include "CubeLog.h"

#include <HalStorage.h>

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace cube {
namespace {
constexpr char DIR[] = "/cube";
constexpr char PATH[] = "/cube/solves.csv";
constexpr char AUTO_PATH[] = "/cube/auto-next";
}  // namespace

bool formatLogLine(char* out, const size_t size, const int64_t unixTime, const uint64_t session, const char seat,
                   const uint32_t number, const int64_t us, const char* scrambleText) {
  const int n = std::snprintf(out, size, "%" PRId64 ",%016" PRIx64 ",%c,%" PRIu32 ",%" PRId64 ",%s", unixTime,
                              session, seat, number, us / 1000, scrambleText);
  if (n < 0 || static_cast<size_t>(n) >= size) return false;
  for (char* c = out; *c; ++c)
    if (*c == '\n') *c = ' ';
  return true;
}

bool parseLogLine(const char* line, char& seat, int64_t& us) {
  const char* field = line;
  for (int i = 0; i < 2; ++i) {
    field = std::strchr(field, ',');
    if (!field) return false;
    ++field;
  }
  if ((field[0] != 'A' && field[0] != 'B') || field[1] != ',') return false;
  seat = field[0];
  field = std::strchr(field + 2, ',');
  if (!field) return false;
  char* end = nullptr;
  const long long ms = std::strtoll(field + 1, &end, 10);
  if (end == field + 1 || *end != ',' || ms < 0) return false;
  us = static_cast<int64_t>(ms) * 1000;
  return true;
}

bool appendSolve(const int64_t unixTime, const uint64_t session, const char seat, const uint32_t number,
                 const int64_t us, const char* scrambleText) {
  char line[LOG_LINE_BYTES];
  if (!formatLogLine(line, sizeof(line) - 1, unixTime, session, seat, number, us, scrambleText)) return false;
  std::strcat(line, "\n");
  if (!Storage.exists(DIR) && !Storage.mkdir(DIR)) return false;
  HalFile file = Storage.open(PATH, O_WRONLY | O_CREAT | O_APPEND);
  if (!file || !file.isOpen()) return false;
  const uint64_t before = file.fileSize64();
  const size_t length = std::strlen(line);
  const bool ok = file.write(line, length) == length;
  if (!ok) file.truncate(before);
  file.flush();
  file.close();
  return ok;
}

void loadBests(int64_t& bestA, int64_t& bestB) {
  bestA = bestB = NO_TIME;
  HalFile file = Storage.open(PATH);
  if (!file || !file.isOpen()) return;
  char chunk[256];
  char line[LOG_LINE_BYTES];
  size_t length = 0;
  bool overlong = false;
  const auto finish = [&] {
    line[length] = 0;
    char seat;
    int64_t us;
    if (!overlong && parseLogLine(line, seat, us)) {
      int64_t& best = seat == 'A' ? bestA : bestB;
      if (best == NO_TIME || us < best) best = us;
    }
    length = 0;
    overlong = false;
  };
  for (int n; (n = file.read(chunk, sizeof(chunk))) > 0;) {
    for (int i = 0; i < n; ++i) {
      if (chunk[i] == '\n') {
        finish();
      } else if (length + 1 < sizeof(line)) {
        line[length++] = chunk[i];
      } else {
        overlong = true;
      }
    }
  }
  file.close();
}

bool loadAutoNext() {
  HalFile file = Storage.open(AUTO_PATH);
  if (!file || !file.isOpen()) return true;
  char c = '1';
  file.read(&c, 1);
  file.close();
  return c != '0';
}

void saveAutoNext(const bool on) {
  if (!Storage.exists(DIR) && !Storage.mkdir(DIR)) return;
  HalFile file = Storage.open(AUTO_PATH, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file || !file.isOpen()) return;
  file.write(on ? "1" : "0", 1);
  file.close();
}

}  // namespace cube
#endif
