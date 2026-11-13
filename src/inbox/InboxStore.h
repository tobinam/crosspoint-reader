#pragma once

#include <cstddef>
#include <cstdint>

// Saved pages pushed by a host, kept on the SD card until deleted on the device.
// One file per (owner, id) in /inbox: "<owner>+<id>.pg". Semantics follow sai's
// reference inbox (xteink/inbox.py):
// - same revision and content: duplicate; same revision, other content: conflict
// - lower revision: stale; higher revision: replaces the page and marks it unread
// - deleting keeps a tombstone, so later revisions of that id stay deleted
// - capacity counts tombstones; at capacity a new identity is refused, nothing is evicted
namespace inbox {
constexpr size_t KEY_BYTES = 32;  // owner and id: [a-z0-9._-], not starting with '.'
constexpr size_t TITLE_BYTES = 64;
constexpr size_t BODY_BYTES = 640;  // one console line carries the whole page
constexpr size_t CAPACITY = 64;

enum class Result { Stored, Duplicate, Stale, Deleted, Conflict, Full, StorageError };

struct Page {
  char owner[KEY_BYTES + 1]{};
  char id[KEY_BYTES + 1]{};
  uint32_t revision = 0;
  char title[TITLE_BYTES + 1]{};
  char body[BODY_BYTES + 1]{};
};

struct Entry {
  char owner[KEY_BYTES + 1]{};
  char id[KEY_BYTES + 1]{};
  uint32_t revision = 0;
  uint32_t seq = 0;  // order of arrival; higher is newer
  bool unread = false;
  bool deleted = false;  // tombstone
  char title[TITLE_BYTES + 1]{};
};

bool validKey(const char* key);
const char* resultName(Result result);  // "stored", "duplicate", ... for replies

Result receive(const Page& page);
// Live pages (no tombstones), newest first. Returns how many were written to out.
size_t list(Entry* out, size_t capacity, bool includeDeleted = false);
bool load(const char* owner, const char* id, Page& page);
bool markRead(const char* owner, const char* id);
bool remove(const char* owner, const char* id);  // leaves a tombstone
size_t unreadCount();
// Changes whenever a page is stored, so open screens know to reload.
uint32_t generation();
}  // namespace inbox
