#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace usbLive {
constexpr size_t TITLE_BYTES = 64;
constexpr size_t BODY_BYTES = 768;
constexpr size_t LINE_BYTES = 1728;

struct Page {
  uint32_t id = 0;
  char title[TITLE_BYTES + 1]{};
  char body[BODY_BYTES + 1]{};
};

inline bool validText(const char* text, bool multiline) {
  const auto* p = reinterpret_cast<const unsigned char*>(text);
  while (*p) {
    uint32_t code = *p++;
    if (code < 0x80) {
      if ((code < 0x20 && !(multiline && code == '\n')) || code == 0x7f) return false;
      continue;
    }
    int count;
    uint32_t minimum;
    if (code >= 0xc2 && code <= 0xdf) {
      code &= 0x1f;
      count = 1;
      minimum = 0x80;
    } else if (code >= 0xe0 && code <= 0xef) {
      code &= 0x0f;
      count = 2;
      minimum = 0x800;
    } else if (code >= 0xf0 && code <= 0xf4) {
      code &= 7;
      count = 3;
      minimum = 0x10000;
    } else {
      return false;
    }
    for (int i = 0; i < count; ++i) {
      if ((*p & 0xc0) != 0x80) return false;
      code = (code << 6) | (*p++ & 0x3f);
    }
    if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
  }
  return true;
}

inline int nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

inline bool decode(const char* begin, size_t length, char* out, size_t capacity, bool multiline) {
  if (!length || length % 2 || length / 2 >= capacity) return false;
  for (size_t i = 0; i < length; i += 2) {
    const int high = nibble(begin[i]);
    const int low = nibble(begin[i + 1]);
    if (high < 0 || low < 0 || !(high || low)) return false;
    out[i / 2] = static_cast<char>((high << 4) | low);
  }
  out[length / 2] = 0;
  return validText(out, multiline);
}

// Input must be a complete, NUL-terminated bounded console line.
inline bool parse(const char* line, Page& page) {
  constexpr char PREFIX[] = "XT1 PAGE ";
  if (std::strncmp(line, PREFIX, sizeof(PREFIX) - 1)) return false;
  const char* p = line + sizeof(PREFIX) - 1;
  uint32_t id = 0;
  if (*p < '1' || *p > '9') return false;
  while (*p >= '0' && *p <= '9') {
    const uint32_t digit = *p++ - '0';
    if (id > (UINT32_MAX - digit) / 10) return false;
    id = id * 10 + digit;
  }
  if (*p++ != ' ') return false;
  const uint32_t parsedId = id;
  const char* split = std::strchr(p, ' ');
  if (!split) return false;
  if (!decode(p, split - p, page.title, sizeof(page.title), false) ||
      !decode(split + 1, std::strlen(split + 1), page.body, sizeof(page.body), true))
    return false;
  page.id = parsedId;
  return true;
}
}  // namespace usbLive
