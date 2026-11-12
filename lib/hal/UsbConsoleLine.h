#pragma once

#include <cstddef>

class UsbConsoleLine {
 public:
  static constexpr size_t CAPACITY = 1728;

 private:
  char buffer[CAPACITY]{};
  size_t length = 0;
  bool dropping = false;
  bool complete = false;
  bool rejected = false;

 public:
  void discard() {
    if (complete) return;
    dropping = true;
    length = 0;
  }
  bool partial() const { return !complete && (length || dropping); }
  bool wasRejected() const { return rejected; }
  void consume() {
    complete = false;
    length = 0;
    dropping = false;
    rejected = false;
  }
  const char* line() const { return buffer; }

  bool feed(char c) {
    if (complete) {
      complete = false;
      length = 0;
      dropping = false;
    }
    if (c == '\n') {
      rejected = dropping;
      if (length && buffer[length - 1] == '\r') --length;
      buffer[dropping ? 0 : length] = 0;
      complete = true;
      return true;
    }
    if (!c || length >= CAPACITY - 1) dropping = true;
    if (!dropping) buffer[length++] = c;
    return false;
  }
};
