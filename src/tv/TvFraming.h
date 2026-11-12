#pragma once

// Chunk framing of the X4 TV remote link (sai: xteink/TV_REMOTE_PROTOCOL.md). Every GATT write/notification is one
// chunk: byte 0 bit 7 = last chunk, bits 0-6 = message number mod 128; the rest a slice of a UTF-8 JSON message.
// Header-only and free of Arduino types so it builds on the host (test/tv_framing).

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace tv {

constexpr size_t TO_X4_MAX = 1024;   // bridge -> X4 message limit
constexpr size_t FROM_X4_MAX = 256;  // X4 -> bridge message limit

// Collects chunks into one message of at most Max bytes; a new number drops a partial message, a too long one is
// dropped whole. Never allocates.
template <size_t Max>
class Joiner {
  uint8_t buf[Max + 1]{};
  size_t len = 0;
  int number = -1;
  bool tooLong = false;

 public:
  // Feeds one chunk; true when it completed a message (message()/length() until the next push).
  bool push(const uint8_t* chunk, size_t size) {
    if (!size) return false;
    const int n = chunk[0] & 0x7F;
    if (n != number) {
      len = 0;
      tooLong = false;
      number = n;
    }
    const size_t body = size - 1;
    if (!tooLong) {
      if (len + body > Max) {
        len = 0;
        tooLong = true;
      } else {
        std::memcpy(buf + len, chunk + 1, body);
        len += body;
      }
    }
    if (!(chunk[0] & 0x80)) return false;
    number = -1;
    const bool ok = !tooLong;
    tooLong = false;
    buf[ok ? len : 0] = 0;
    if (!ok) len = 0;
    return ok;
  }
  const char* message() const { return reinterpret_cast<const char*>(buf); }  // NUL-terminated
  size_t length() const { return len; }
  void reset() {
    len = 0;
    number = -1;
    tooLong = false;
  }
};

// Splits messages into chunks of at most `room` bytes (header included) and hands each to `emit(data, size)`.
class Chunker {
  uint8_t number = 0;

 public:
  template <typename Emit>
  bool split(const char* msg, size_t size, size_t room, Emit&& emit) {
    if (room < 2) return false;
    uint8_t chunk[256];
    if (room > sizeof(chunk)) room = sizeof(chunk);
    const uint8_t n = number++ & 0x7F;
    const size_t step = room - 1;
    size_t at = 0;
    do {
      const size_t part = size - at < step ? size - at : step;
      const bool last = at + part >= size;
      chunk[0] = static_cast<uint8_t>(n | (last ? 0x80 : 0));
      std::memcpy(chunk + 1, msg + at, part);
      if (!emit(chunk, part + 1)) return false;
      at += part;
    } while (at < size);
    return true;
  }
  void reset() { number = 0; }
};

}  // namespace tv
