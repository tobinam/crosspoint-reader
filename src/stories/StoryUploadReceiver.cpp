#if XTEINK_INK_STORIES
#include "StoryUploadReceiver.h"

#include <HalUsbConsole.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "StoryProgress.h"

namespace {
constexpr char TEMP[] = "/stories/.upload.tmp";
}

void StoryUploadReceiver::abort() {
  if (file) file.close();
  Storage.remove(TEMP);
  id = 0;
}

void StoryUploadReceiver::expire(uint32_t nowMs) {
  if (active() && static_cast<uint32_t>(nowMs - lastFrameMs) > IDLE_TIMEOUT_MS) {
    LOG_ERR("USB", "Story upload %s timed out", name);
    abort();
  }
}

void StoryUploadReceiver::list(uint32_t requestId, HalUsbConsole& console) {
  auto dir = Storage.open("/stories");
  uint32_t count = 0;
  char value[2 * usbFile::NAME_BYTES + 24];
  for (size_t scanned = 0; dir && dir.isDirectory() && scanned < 256; ++scanned) {
    auto file = dir.openNextFile();
    if (!file) break;
    char name[usbFile::NAME_BYTES + 2]{};
    if (file.isDirectory() || !file.getName(name, sizeof(name)) || !usbFile::validName(name)) continue;
    const uint32_t size = static_cast<uint32_t>(file.size());
    uint32_t crc = 0;
    uint8_t chunk[512];
    for (int n; (n = file.read(chunk, sizeof(chunk))) > 0;) crc = usbFile::crc32(crc, chunk, n);
    file.close();
    char* out = value;
    for (const char* c = name; *c; ++c) out += std::snprintf(out, 3, "%02x", static_cast<unsigned char>(*c));
    std::snprintf(out, value + sizeof(value) - out, ",%lu,%08lx", static_cast<unsigned long>(size),
                  static_cast<unsigned long>(crc));
    console.reply(requestId, "ITEM", value);
    ++count;
  }
  char total[12];
  std::snprintf(total, sizeof(total), "%lu", static_cast<unsigned long>(count));
  console.reply(requestId, "DONE", total);
}

void StoryUploadReceiver::remove(uint32_t requestId, const char* storyName, HalUsbConsole& console) {
  char path[112];
  std::snprintf(path, sizeof(path), "/stories/%s", storyName);
  if (!Storage.exists(path)) {
    console.reply(requestId, "ERROR", "NOFILE");
    return;
  }
  if (!Storage.remove(path)) {
    console.reply(requestId, "ERROR", "STORAGE");
    return;
  }
  storyProgress::forget(storyName);
  LOG_INF("USB", "Story deleted %s", path);
  console.reply(requestId, "DONE", "0");
}

void StoryUploadReceiver::handle(const usbFile::Command& command, HalUsbConsole& console, uint32_t nowMs) {
  using usbFile::Kind;
  if (command.kind == Kind::List) return list(command.id, console);
  if (command.kind == Kind::Delete) return remove(command.id, command.name, console);
  if (command.kind == Kind::Begin) {
    // A new BEGIN replaces an abandoned upload.
    if (active()) abort();
    if (!Storage.ensureDirectoryExists("/stories") || (Storage.exists(TEMP) && !Storage.remove(TEMP)) ||
        !(file = Storage.open(TEMP, O_WRONLY | O_CREAT | O_TRUNC))) {
      console.reply(command.id, "ERROR", "STORAGE");
      return;
    }
    id = command.id;
    size = command.size;
    expectedCrc = command.crc;
    crc = 0;
    written = 0;
    lastFrameMs = nowMs;
    std::snprintf(name, sizeof(name), "%s", command.name);
    console.reply(id, "READY", "0");
    return;
  }
  if (!active() || command.id != id) {
    console.reply(command.id, "ERROR", "NOUPLOAD");
    return;
  }
  lastFrameMs = nowMs;
  if (command.kind == Kind::Data) {
    char offset[12];
    if (command.offset != written) {
      // Lets the host resend the expected chunk after a lost ACK.
      std::snprintf(offset, sizeof(offset), "%lu", static_cast<unsigned long>(written));
      console.reply(id, "ACK", offset);
      return;
    }
    if (command.dataLength > size - written || file.write(command.data, command.dataLength) != command.dataLength) {
      console.reply(id, "ERROR", command.dataLength > size - written ? "SIZE" : "WRITE");
      abort();
      return;
    }
    crc = usbFile::crc32(crc, command.data, command.dataLength);
    written += command.dataLength;
    std::snprintf(offset, sizeof(offset), "%lu", static_cast<unsigned long>(written));
    console.reply(id, "ACK", offset);
    return;
  }
  // END
  file.flush();
  file.close();
  const uint32_t doneId = id;
  if (written != size || crc != expectedCrc) {
    console.reply(doneId, "ERROR", written != size ? "SIZE" : "CRC");
    abort();
    return;
  }
  char path[usbFile::PATH_BYTES];
  if (!usbFile::uploadPath(name, path, sizeof(path)) || (Storage.exists(path) && !Storage.remove(path)) ||
      !Storage.rename(TEMP, path)) {
    console.reply(doneId, "ERROR", "RENAME");
    abort();
    return;
  }
  id = 0;
  LOG_INF("USB", "Upload saved %s (%lu bytes)", path, static_cast<unsigned long>(size));
  console.reply(doneId, "DONE", "0");
}
#endif
