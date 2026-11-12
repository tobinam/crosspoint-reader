#if XTEINK_INK_STORIES
#include "StoryProgress.h"

#include <HalMemory.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "InkStorySession.h"

namespace storyProgress {
namespace {
constexpr char DIR[] = "/stories/.save";
constexpr char MAGIC[4] = {'X', 'T', 'S', '1'};
constexpr size_t HEADER_BYTES = 20;  // magic, crc, page offset, text length, language
constexpr size_t MAX_SAVE_BYTES = 64 * 1024;

bool path(char* out, size_t size, const char* name, const char* ext) {
  const int n = std::snprintf(out, size, "%s/%s%s", DIR, name, ext);
  return n > 0 && static_cast<size_t>(n) < size;
}

void put32(uint8_t* p, uint32_t v) {
  p[0] = v;
  p[1] = v >> 8;
  p[2] = v >> 16;
  p[3] = v >> 24;
}

uint32_t get32(const uint8_t* p) {
  return p[0] | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

struct FileWriter final : ink::runtime::snapshot::writer {
  HalFile& file;
  explicit FileWriter(HalFile& f) : file(f) {}
  bool write(const void* data, ink::size_t length) override {
    return file.write(static_cast<const uint8_t*>(data), length) == length;
  }
};
}  // namespace

bool save(const char* name, uint32_t crc, const InkStorySession& session, uint32_t pageOffset) {
  char target[160];
  char temp[160];
  if (!path(target, sizeof(target), name, ".sav") || !path(temp, sizeof(temp), name, ".tmp") ||
      !Storage.ensureDirectoryExists(DIR))
    return false;
  auto file = Storage.open(temp, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) return false;
  const size_t textLength = std::strlen(session.text());
  uint8_t header[HEADER_BYTES]{};
  std::memcpy(header, MAGIC, 4);
  put32(header + 4, crc);
  put32(header + 8, pageOffset);
  put32(header + 12, textLength);
  std::memcpy(header + 16, session.language(), std::strlen(session.language()));
  FileWriter writer(file);
  const bool ok = file.write(header, sizeof(header)) == sizeof(header) &&
                  file.write(reinterpret_cast<const uint8_t*>(session.text()), textLength) == textLength &&
                  session.saveSnapshot(writer);
  file.flush();
  file.close();
  if (!ok || (Storage.exists(target) && !Storage.remove(target)) || !Storage.rename(temp, target)) {
    Storage.remove(temp);
    LOG_ERR("INK", "Could not save progress for %s", name);
    return false;
  }
  LOG_INF("INK", "Saved progress for %s", name);
  return true;
}

bool restore(const char* name, uint32_t crc, const uint8_t* bytes, size_t length, InkStorySession& session,
             uint32_t& pageOffset) {
  char target[160];
  if (!path(target, sizeof(target), name, ".sav") || !Storage.exists(target)) return false;
  auto file = Storage.open(target);
  const size_t size = file ? file.size() : 0;
  if (size <= HEADER_BYTES || size > MAX_SAVE_BYTES) return false;
  // Transient PSRAM copy; the VM copies what it needs during restore.
  auto data = HalMemory::allocatePsram(size);
  if (!data || file.read(data.get(), size) != static_cast<int>(size)) return false;
  file.close();
  const uint8_t* p = data.get();
  const uint32_t textLength = get32(p + 12);
  char language[3]{static_cast<char>(p[16]), static_cast<char>(p[17]), 0};
  if (std::memcmp(p, MAGIC, 4) || get32(p + 4) != crc || textLength > size - HEADER_BYTES ||
      !session.restore(bytes, length, p + HEADER_BYTES + textLength, size - HEADER_BYTES - textLength,
                       reinterpret_cast<const char*>(p + HEADER_BYTES), textLength, language)) {
    LOG_INF("INK", "Ignoring stale or invalid save for %s", name);
    session.clear();
    return false;
  }
  pageOffset = get32(p + 8);
  if (pageOffset > textLength) pageOffset = 0;
  LOG_INF("INK", "Resumed %s", name);
  return true;
}

void discard(const char* name) {
  char target[160];
  if (path(target, sizeof(target), name, ".sav") && Storage.exists(target)) Storage.remove(target);
}

uint16_t plays(const char* name) {
  char target[160];
  if (!path(target, sizeof(target), name, ".plays") || !Storage.exists(target)) return 0;
  auto file = Storage.open(target);
  char text[8]{};
  if (!file || file.read(reinterpret_cast<uint8_t*>(text), sizeof(text) - 1) <= 0) return 0;
  const unsigned long value = std::strtoul(text, nullptr, 10);
  return value > 65535 ? 65535 : static_cast<uint16_t>(value);
}

void addPlay(const char* name) {
  char target[160];
  if (!path(target, sizeof(target), name, ".plays") || !Storage.ensureDirectoryExists(DIR)) return;
  const unsigned count = plays(name) + 1u;
  auto file = Storage.open(target, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) return;
  char text[8];
  const int n = std::snprintf(text, sizeof(text), "%u", count > 65535 ? 65535 : count);
  file.write(reinterpret_cast<const uint8_t*>(text), n);
  file.close();
}
namespace {
constexpr char WAKE[] = "/stories/.save/.resume";
}

void markWakeResume(const char* name) {
  if (!Storage.ensureDirectoryExists(DIR)) return;
  auto file = Storage.open(WAKE, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) return;
  file.write(reinterpret_cast<const uint8_t*>(name), std::strlen(name));
  file.flush();
  file.close();
}

bool wakeResumePending() { return Storage.exists(WAKE); }

bool takeWakeResume(char* name, size_t size) {
  if (!size || !Storage.exists(WAKE)) return false;
  auto file = Storage.open(WAKE);
  const int n = file ? file.read(reinterpret_cast<uint8_t*>(name), size - 1) : 0;
  file.close();
  Storage.remove(WAKE);
  name[n > 0 ? n : 0] = 0;
  return n > 0;
}

void clearWakeResume() {
  if (Storage.exists(WAKE)) Storage.remove(WAKE);
}
}  // namespace storyProgress
#endif
