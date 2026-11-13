#if XTEINK_INK_STORIES || defined(INK_STORY_HOST_TEST)
#include "InkStorySession.h"

#include <cstring>

namespace {
uint32_t read32(const uint8_t* p) {
  return p[0] | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
}  // namespace

bool InkStorySession::validHeader(const uint8_t* data, size_t length) {
  if (!data || length < 64 || length > MAX_FILE_BYTES || read32(data) != 0x494e4b42 || data[6] != 2 || data[7] != 0)
    return false;
  for (size_t field = 8; field < 64; field += 8) {
    const size_t start = read32(data + field);
    const size_t bytes = read32(data + field + 4);
    if (bytes && (start < 64 || start % 16 || start > length || bytes > length - start)) return false;
  }
  const size_t strings = read32(data + 8);
  const size_t stringBytes = read32(data + 12);
  return stringBytes && data[strings + stringBytes - 1] == 0 && read32(data + 60) > 0;
}

void InkStorySession::clear() {
  runner = nullptr;
  story.reset();
  output[0] = 0;
  choiceCount = 0;
  error = false;
  lang[0] = 0;
}

bool InkStorySession::load(const uint8_t* bytes, size_t length, uint32_t seed) {
  clear();
  if (!validHeader(bytes, length)) return false;
  // InkCPP owns its VM allocations, but not the caller's PSRAM story bytes.
  story.reset(ink::runtime::story::from_binary(bytes, length, false));
  if (!story) return false;
  runner = story->new_runner();
  if (runner && seed) runner->set_rng_seed(seed);
  return runner && advance();
}

bool InkStorySession::advance() {
  size_t used = 0;
  output[0] = 0;
  size_t lines = 0;
  while (runner->can_continue()) {
    if (++lines > 64) {
      error = true;
      return false;
    }
    const char* line = runner->getline_alloc();
    if (!line) {
      error = true;
      return false;
    }
    // Global tags are readable once the first line has run.
    if (lines == 1 && !lang[0]) readLanguage();
    const size_t length = std::strlen(line);
    if (length >= sizeof(output) - used) {
      error = true;
      return false;
    }
    std::memcpy(output + used, line, length + 1);
    used += length;
  }
  choiceCount = runner->num_choices();
  if (choiceCount > MAX_CHOICES) {
    error = true;
    return false;
  }
  return true;
}

bool InkStorySession::choose(size_t index) {
  if (!runner || index >= choiceCount || error) return false;
  runner->choose((runner->begin() + index)->index());
  return advance();
}

void InkStorySession::readLanguage() {
  if (!runner->has_global_tags()) return;
  for (size_t i = 0; i < runner->num_global_tags(); ++i) {
    const char* tag = runner->get_global_tag(i);
    if (!tag || std::strncmp(tag, "lang:", 5)) continue;
    tag += 5;
    while (*tag == ' ') ++tag;
    if (tag[0] >= 'a' && tag[0] <= 'z' && tag[1] >= 'a' && tag[1] <= 'z') {
      lang[0] = tag[0];
      lang[1] = tag[1];
      lang[2] = 0;
    }
    return;
  }
}

bool InkStorySession::saveSnapshot(ink::runtime::snapshot::writer& writer) const {
  return runner && !error && runner->stream_snapshot_to(writer) > 0;
}

bool InkStorySession::restore(const uint8_t* bytes, size_t length, const uint8_t* snapshotBytes, size_t snapshotLength,
                              const char* text, size_t textLength, const char* language) {
  clear();
  if (!validHeader(bytes, length) || !snapshotBytes || !snapshotLength || textLength >= sizeof(output)) return false;
  story.reset(ink::runtime::story::from_binary(bytes, length, false));
  if (!story) return false;
  std::unique_ptr<ink::runtime::snapshot> snapshot(
      ink::runtime::snapshot::from_binary(snapshotBytes, snapshotLength, false));
  if (!snapshot) return false;
  auto globals = story->new_globals_from_snapshot(*snapshot);
  if (!globals) return false;
  runner = story->new_runner_from_snapshot(*snapshot, globals);
  if (!runner) return false;
  choiceCount = runner->num_choices();
  if (choiceCount > MAX_CHOICES) {
    clear();
    return false;
  }
  std::memcpy(output, text, textLength);
  output[textLength] = 0;
  if (language && language[0] && language[1] && !language[2]) {
    lang[0] = language[0];
    lang[1] = language[1];
    lang[2] = 0;
  }
  return true;
}

const char* InkStorySession::choiceText(size_t index) const {
  return runner && index < choiceCount ? (runner->begin() + index)->text() : "";
}
#endif
