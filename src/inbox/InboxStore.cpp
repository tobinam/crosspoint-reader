#if XTEINK_INBOX || INBOX_HOST_TEST
#include "InboxStore.h"

#include <HalStorage.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace inbox {
namespace {
constexpr char DIR[] = "/inbox";
constexpr char TEMP[] = "/inbox/.page.tmp";
constexpr char SUFFIX[] = ".pg";
constexpr size_t PATH_BYTES = sizeof(DIR) + 2 * KEY_BYTES + 8;
constexpr size_t FILE_BYTES = 40 + TITLE_BYTES + 1 + BODY_BYTES;
uint32_t changes = 0;

struct Header {
  uint32_t revision = 0;
  uint32_t seq = 0;
  char state = 0;  // 'u' unread, 'r' read, 'x' deleted
};

void pathFor(const char* owner, const char* id, char* out) {
  std::snprintf(out, PATH_BYTES, "%s/%s+%s%s", DIR, owner, id, SUFFIX);
}

// "<owner>+<id>.pg" -> owner, id. Anything else in /inbox is ignored.
bool splitName(const char* name, char* owner, char* id) {
  const size_t length = std::strlen(name);
  const size_t suffix = sizeof(SUFFIX) - 1;
  if (length <= suffix || std::strcmp(name + length - suffix, SUFFIX)) return false;
  const char* plus = std::strchr(name, '+');
  if (!plus) return false;
  const size_t ownerLength = plus - name;
  const size_t idLength = length - suffix - ownerLength - 1;
  if (!ownerLength || ownerLength > KEY_BYTES || !idLength || idLength > KEY_BYTES) return false;
  std::memcpy(owner, name, ownerLength);
  owner[ownerLength] = 0;
  std::memcpy(id, plus + 1, idLength);
  id[idLength] = 0;
  return validKey(owner) && validKey(id);
}

// Reads the whole page file; text points into buf after the header line.
bool readFile(const char* path, char* buf, size_t capacity, Header& header, const char*& text) {
  auto file = Storage.open(path);
  if (!file || file.isDirectory()) return false;
  size_t length = 0;
  for (int n; length < capacity - 1 && (n = file.read(buf + length, capacity - 1 - length)) > 0;) length += n;
  file.close();
  buf[length] = 0;
  char* end = nullptr;
  if (std::strncmp(buf, "XPG1 ", 5)) return false;
  header.revision = std::strtoul(buf + 5, &end, 10);
  if (*end != ' ') return false;
  header.seq = std::strtoul(end + 1, &end, 10);
  if (*end != ' ' || (end[1] != 'u' && end[1] != 'r' && end[1] != 'x') || end[2] != '\n') return false;
  header.state = end[1];
  text = end + 3;
  return header.revision > 0;
}

bool writeFile(const char* path, const Header& header, const char* title, const char* body) {
  if (!Storage.exists(DIR) && !Storage.mkdir(DIR)) return false;
  if (Storage.exists(TEMP) && !Storage.remove(TEMP)) return false;
  auto file = Storage.open(TEMP, O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) return false;
  char head[40];
  const int headLength = std::snprintf(head, sizeof(head), "XPG1 %lu %lu %c\n",
                                       static_cast<unsigned long>(header.revision),
                                       static_cast<unsigned long>(header.seq), header.state);
  bool ok = file.write(reinterpret_cast<const uint8_t*>(head), headLength) == static_cast<size_t>(headLength);
  if (header.state != 'x') {
    const size_t titleLength = std::strlen(title);
    const size_t bodyLength = std::strlen(body);
    ok = ok && file.write(reinterpret_cast<const uint8_t*>(title), titleLength) == titleLength &&
         file.write(reinterpret_cast<const uint8_t*>("\n"), 1) == 1 &&
         file.write(reinterpret_cast<const uint8_t*>(body), bodyLength) == bodyLength;
  }
  file.flush();
  file.close();
  if (!ok || (Storage.exists(path) && !Storage.remove(path)) || !Storage.rename(TEMP, path)) {
    Storage.remove(TEMP);
    return false;
  }
  return true;
}

// Calls visit(owner, id, header, text) for every page file; stops early when visit returns false.
template <typename Visit>
void scan(Visit visit) {
  auto dir = Storage.open(DIR);
  if (!dir || !dir.isDirectory()) return;
  for (size_t scanned = 0; scanned < 4 * CAPACITY; ++scanned) {
    auto file = dir.openNextFile();
    if (!file) break;
    char name[2 * KEY_BYTES + 8]{};
    const bool directory = file.isDirectory();
    file.getName(name, sizeof(name));
    file.close();
    char owner[KEY_BYTES + 1];
    char id[KEY_BYTES + 1];
    if (directory || !splitName(name, owner, id)) continue;
    char path[PATH_BYTES];
    pathFor(owner, id, path);
    char buf[FILE_BYTES + 1];
    Header header;
    const char* text = nullptr;
    if (!readFile(path, buf, sizeof(buf), header, text)) continue;
    if (!visit(owner, id, header, text)) break;
  }
}

void copyTitle(const char* text, char* out) {
  size_t length = 0;
  while (text[length] && text[length] != '\n' && length < TITLE_BYTES) ++length;
  std::memcpy(out, text, length);
  out[length] = 0;
}
}  // namespace

bool validKey(const char* key) {
  const size_t length = std::strlen(key);
  if (!length || length > KEY_BYTES || key[0] == '.') return false;
  for (size_t i = 0; i < length; ++i) {
    const char c = key[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
  }
  return true;
}

const char* resultName(Result result) {
  switch (result) {
    case Result::Stored:
      return "stored";
    case Result::Duplicate:
      return "duplicate";
    case Result::Stale:
      return "stale";
    case Result::Deleted:
      return "deleted";
    case Result::Conflict:
      return "CONFLICT";
    case Result::Full:
      return "FULL";
    case Result::StorageError:
      break;
  }
  return "STORAGE";
}

Result receive(const Page& page) {
  if (!validKey(page.owner) || !validKey(page.id) || !page.revision || !page.title[0]) return Result::StorageError;
  char path[PATH_BYTES];
  pathFor(page.owner, page.id, path);
  uint32_t lastSeq = 0;
  size_t count = 0;
  scan([&](const char*, const char*, const Header& header, const char*) {
    lastSeq = std::max(lastSeq, header.seq);
    ++count;
    return true;
  });
  if (Storage.exists(path)) {
    char buf[FILE_BYTES + 1];
    Header header;
    const char* text = nullptr;
    if (readFile(path, buf, sizeof(buf), header, text)) {
      if (header.state == 'x') return Result::Deleted;
      if (page.revision < header.revision) return Result::Stale;
      if (page.revision == header.revision) {
        const char* body = std::strchr(text, '\n');
        const size_t titleLength = body ? static_cast<size_t>(body - text) : std::strlen(text);
        const bool same = std::strlen(page.title) == titleLength &&
                          !std::strncmp(page.title, text, titleLength) && body && !std::strcmp(page.body, body + 1);
        return same ? Result::Duplicate : Result::Conflict;
      }
    }
  } else if (count >= CAPACITY) {
    return Result::Full;
  }
  Header header;
  header.revision = page.revision;
  header.seq = lastSeq + 1;
  header.state = 'u';
  if (!writeFile(path, header, page.title, page.body)) return Result::StorageError;
  ++changes;
  return Result::Stored;
}

size_t list(Entry* out, size_t capacity, bool includeDeleted) {
  size_t count = 0;
  scan([&](const char* owner, const char* id, const Header& header, const char* text) {
    if (header.state == 'x' && !includeDeleted) return true;
    if (count >= capacity) return false;
    Entry& entry = out[count++];
    std::snprintf(entry.owner, sizeof(entry.owner), "%s", owner);
    std::snprintf(entry.id, sizeof(entry.id), "%s", id);
    entry.revision = header.revision;
    entry.seq = header.seq;
    entry.unread = header.state == 'u';
    entry.deleted = header.state == 'x';
    if (entry.deleted)
      entry.title[0] = 0;
    else
      copyTitle(text, entry.title);
    return true;
  });
  std::sort(out, out + count, [](const Entry& a, const Entry& b) { return a.seq > b.seq; });
  return count;
}

bool load(const char* owner, const char* id, Page& page) {
  if (!validKey(owner) || !validKey(id)) return false;
  char path[PATH_BYTES];
  pathFor(owner, id, path);
  char buf[FILE_BYTES + 1];
  Header header;
  const char* text = nullptr;
  if (!readFile(path, buf, sizeof(buf), header, text) || header.state == 'x') return false;
  std::snprintf(page.owner, sizeof(page.owner), "%s", owner);
  std::snprintf(page.id, sizeof(page.id), "%s", id);
  page.revision = header.revision;
  copyTitle(text, page.title);
  const char* body = std::strchr(text, '\n');
  std::snprintf(page.body, sizeof(page.body), "%s", body ? body + 1 : "");
  return true;
}

bool markRead(const char* owner, const char* id) {
  Page page;
  if (!load(owner, id, page)) return false;
  char path[PATH_BYTES];
  pathFor(owner, id, path);
  char buf[FILE_BYTES + 1];
  Header header;
  const char* text = nullptr;
  if (!readFile(path, buf, sizeof(buf), header, text)) return false;
  if (header.state != 'u') return true;
  header.state = 'r';
  return writeFile(path, header, page.title, page.body);
}

bool remove(const char* owner, const char* id) {
  if (!validKey(owner) || !validKey(id)) return false;
  char path[PATH_BYTES];
  pathFor(owner, id, path);
  char buf[FILE_BYTES + 1];
  Header header;
  const char* text = nullptr;
  if (!readFile(path, buf, sizeof(buf), header, text)) return false;
  if (header.state == 'x') return true;
  header.state = 'x';
  return writeFile(path, header, "", "");
}

uint32_t generation() { return changes; }

size_t unreadCount() {
  size_t count = 0;
  scan([&](const char*, const char*, const Header& header, const char*) {
    count += header.state == 'u';
    return true;
  });
  return count;
}
}  // namespace inbox
#endif
