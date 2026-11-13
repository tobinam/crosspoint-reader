#pragma once

#include <choice.h>
#include <globals.h>
#include <runner.h>
#include <snapshot.h>
#include <story.h>

#include <cstddef>
#include <cstdint>
#include <memory>

// Bounded presentation around the pinned InkCPP VM; accepts trusted compiled stories.
class InkStorySession {
 public:
  static constexpr size_t MAX_FILE_BYTES = 256 * 1024;
  static constexpr size_t TEXT_BYTES = 8192;
  static constexpr size_t MAX_CHOICES = 16;
  static bool validHeader(const uint8_t* bytes, size_t length);
  // seed 0 keeps InkCPP's clock seed (the device); tools pass a fixed seed for reproducible RANDOM.
  bool load(const uint8_t* bytes, size_t length, uint32_t seed = 0);
  bool choose(size_t index);
  // Streams VM state (globals, visit counts, position) at the current choice point.
  bool saveSnapshot(ink::runtime::snapshot::writer& writer) const;
  // Rebuilds the VM from a snapshot taken on the same story bytes. The VM does
  // not replay output, so the passage text and language come from the caller.
  bool restore(const uint8_t* bytes, size_t length, const uint8_t* snapshot, size_t snapshotLength, const char* text,
               size_t textLength, const char* language);
  void clear();
  const char* text() const { return output; }
  size_t choices() const { return choiceCount; }
  const char* choiceText(size_t index) const;
  bool failed() const { return error; }
  // Two-letter language from a global "# lang: xx" tag, or "" when absent.
  const char* language() const { return lang; }

 private:
  std::unique_ptr<ink::runtime::story> story;
  ink::runtime::runner runner;
  char output[TEXT_BYTES]{};
  size_t choiceCount = 0;
  bool error = false;
  char lang[3]{};
  bool advance();
  void readLanguage();
};
