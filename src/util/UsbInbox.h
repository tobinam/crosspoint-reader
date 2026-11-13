#pragma once

#include <cstdint>
#include <cstring>

#include "inbox/InboxStore.h"
#include "util/UsbLivePage.h"

// Saved inbox pages over the USB console, one line per command:
//   XT1 INBOX PUT <id> <owner-hex> <page-id-hex> <revision> <title-hex> <body-hex or ->
//     -> XT1 DONE <id> stored|duplicate|stale|deleted, or XT1 ERROR <id> CONFLICT|FULL|STORAGE|BUSY
//   XT1 INBOX LIST <id>
//     -> XT1 ITEM <id> <owner-hex>,<page-id-hex>,<revision>,unread|read|deleted per page, then XT1 DONE <id> <count>
// Text is UTF-8; the title is one line, the body may contain line breaks.
namespace usbInbox {
enum class Kind { None, Put, List };

struct Command {
  Kind kind = Kind::None;
  uint32_t id = 0;
  inbox::Page page;
};

namespace detail {
inline bool number(const char*& p, uint32_t& out) {
  if (*p < '1' || *p > '9') return false;
  uint32_t value = 0;
  while (*p >= '0' && *p <= '9') {
    const uint32_t digit = *p++ - '0';
    if (value > (UINT32_MAX - digit) / 10) return false;
    value = value * 10 + digit;
  }
  out = value;
  return true;
}

// One space-terminated (or line-final) hex field decoded into out.
inline bool field(const char*& p, char* out, size_t capacity, bool multiline, bool last) {
  const char* end = last ? p + std::strlen(p) : std::strchr(p, ' ');
  if (!end) return false;
  if (!usbLive::decode(p, end - p, out, capacity, multiline)) return false;
  p = last ? end : end + 1;
  return true;
}
}  // namespace detail

// Input must be a complete, NUL-terminated bounded console line.
inline bool parse(const char* line, Command& command) {
  using namespace detail;
  constexpr char PREFIX[] = "XT1 INBOX ";
  if (std::strncmp(line, PREFIX, sizeof(PREFIX) - 1)) return false;
  const char* p = line + sizeof(PREFIX) - 1;
  if (!std::strncmp(p, "LIST ", 5)) {
    p += 5;
    if (!number(p, command.id) || *p) return false;
    command.kind = Kind::List;
    return true;
  }
  if (std::strncmp(p, "PUT ", 4)) return false;
  p += 4;
  inbox::Page& page = command.page;
  if (!number(p, command.id) || *p++ != ' ') return false;
  if (!field(p, page.owner, sizeof(page.owner), false, false) || !inbox::validKey(page.owner)) return false;
  if (!field(p, page.id, sizeof(page.id), false, false) || !inbox::validKey(page.id)) return false;
  if (!number(p, page.revision) || page.revision > INT32_MAX || *p++ != ' ') return false;
  if (!field(p, page.title, sizeof(page.title), false, false)) return false;
  if (!std::strcmp(p, "-")) {
    page.body[0] = 0;
  } else if (!field(p, page.body, sizeof(page.body), true, true)) {
    return false;
  }
  command.kind = Kind::Put;
  return true;
}
}  // namespace usbInbox
