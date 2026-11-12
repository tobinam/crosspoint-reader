#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Bounded line protocol for pushing story files over the USB console.
//   XT1 FILE BEGIN <id> <name-hex> <size> <crc32-hex8>
//   XT1 FILE DATA <id> <offset> <payload-hex>
//   XT1 FILE END <id>
namespace usbFile {
constexpr size_t NAME_BYTES = 94;
constexpr size_t CHUNK_BYTES = 768;
constexpr uint32_t MAX_FILE_BYTES = 256 * 1024;

enum class Kind { None, Begin, Data, End };

struct Command {
  Kind kind = Kind::None;
  uint32_t id = 0;
  char name[NAME_BYTES + 1]{};
  uint32_t size = 0;
  uint32_t crc = 0;
  uint32_t offset = 0;
  uint8_t data[CHUNK_BYTES]{};
  size_t dataLength = 0;
};

inline uint32_t crc32(uint32_t crc, const uint8_t* data, size_t length) {
  crc = ~crc;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

// Lowercase story names only: no paths, no hidden files, must end in ".bin".
inline bool validName(const char* name) {
  const size_t length = std::strlen(name);
  if (length < 5 || length > NAME_BYTES || name[0] == '.' || std::strcmp(name + length - 4, ".bin")) return false;
  for (size_t i = 0; i < length; ++i) {
    const char c = name[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
  }
  return true;
}

namespace detail {
inline int nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

inline bool number(const char*& p, uint32_t& out, bool allowZero) {
  if (*p < '0' || *p > '9' || (*p == '0' && (!allowZero || (p[1] >= '0' && p[1] <= '9')))) return false;
  uint32_t value = 0;
  while (*p >= '0' && *p <= '9') {
    const uint32_t digit = *p++ - '0';
    if (value > (UINT32_MAX - digit) / 10) return false;
    value = value * 10 + digit;
  }
  out = value;
  return true;
}

inline size_t hexBytes(const char*& p, uint8_t* out, size_t capacity) {
  size_t count = 0;
  while (nibble(p[0]) >= 0 && nibble(p[1]) >= 0) {
    if (count >= capacity) return 0;
    out[count++] = static_cast<uint8_t>((nibble(p[0]) << 4) | nibble(p[1]));
    p += 2;
  }
  return count;
}

inline bool literal(const char*& p, const char* text) {
  const size_t length = std::strlen(text);
  if (std::strncmp(p, text, length)) return false;
  p += length;
  return true;
}
}  // namespace detail

// Input must be a complete, NUL-terminated bounded console line.
inline bool parse(const char* line, Command& command) {
  using namespace detail;
  const char* p = line;
  if (!literal(p, "XT1 FILE ")) return false;
  Kind kind;
  if (literal(p, "BEGIN "))
    kind = Kind::Begin;
  else if (literal(p, "DATA "))
    kind = Kind::Data;
  else if (literal(p, "END "))
    kind = Kind::End;
  else
    return false;
  if (!number(p, command.id, false)) return false;
  if (kind == Kind::End) {
    if (*p) return false;
  } else if (kind == Kind::Begin) {
    uint8_t name[NAME_BYTES + 1];
    uint8_t crc[4];
    if (!literal(p, " ")) return false;
    const size_t nameLength = hexBytes(p, name, NAME_BYTES);
    if (!nameLength || !literal(p, " ") || !number(p, command.size, false) || command.size > MAX_FILE_BYTES ||
        !literal(p, " ") || hexBytes(p, crc, 4) != 4 || *p)
      return false;
    std::memcpy(command.name, name, nameLength);
    command.name[nameLength] = 0;
    if (!validName(command.name)) return false;
    command.crc = (uint32_t(crc[0]) << 24) | (uint32_t(crc[1]) << 16) | (uint32_t(crc[2]) << 8) | crc[3];
  } else {
    if (!literal(p, " ") || !number(p, command.offset, true) || !literal(p, " ")) return false;
    command.dataLength = hexBytes(p, command.data, CHUNK_BYTES);
    if (!command.dataLength || *p) return false;
  }
  command.kind = kind;
  return true;
}
}  // namespace usbFile
