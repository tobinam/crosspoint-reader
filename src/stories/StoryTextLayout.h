#pragma once

#include <FreeInkUICore.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace storyText {
namespace fui = freeink::ui;

// Longest single run measured at once; longer runs are split at word boundaries.
constexpr size_t LINE_BYTES = 240;
// A page may end early by up to this share of its lines to break at a sentence.
constexpr size_t SOFT_BREAK_PERCENT = 30;

struct Line {
  size_t begin;  // Offset of the first visible byte.
  size_t length;
  size_t next;  // Offset after the consumed separator (space or newline).
  bool paragraphEnd;
};

inline int textWidth(const fui::DrawTarget& target, const fui::TextStyle& style, const char* text, size_t length) {
  char buffer[LINE_BYTES + 1];
  if (length > LINE_BYTES) length = LINE_BYTES;
  std::memcpy(buffer, text, length);
  buffer[length] = 0;
  return target.measureText(style.font, buffer, style).width;
}

// Width of line `index`: all lines use `width` except line `narrowLine`, which
// leaves room for the page marker at its right end.
struct Widths {
  int width;
  size_t narrowLine = SIZE_MAX;
  int narrowWidth = 0;
  int operator()(size_t index) const { return index == narrowLine ? narrowWidth : width; }
};

// Greedy word wrap of text[0, length). Hard newlines end lines; "\n\n" yields an
// empty line. A trailing newline does not add an empty line. Stops when emit
// returns false. Words wider than the line are kept whole on their own line.
template <typename Emit>
void wrap(const fui::DrawTarget& target, const fui::TextStyle& style, const char* text, size_t length,
          Widths widths, Emit&& emit) {
  size_t pos = 0;
  size_t index = 0;
  while (pos < length) {
    const int width = widths(index);
    if (text[pos] == '\n') {
      if (!emit(Line{pos, 0, pos + 1, true})) return;
      ++index;
      ++pos;
      continue;
    }
    if (text[pos] == ' ') {
      ++pos;
      continue;
    }
    size_t end = pos;  // End of the longest fitting run of whole words.
    size_t scan = pos;
    while (scan < length && text[scan] != '\n') {
      size_t wordEnd = scan;
      while (wordEnd < length && text[wordEnd] != ' ' && text[wordEnd] != '\n') ++wordEnd;
      if (end != pos && (wordEnd - pos > LINE_BYTES || textWidth(target, style, text + pos, wordEnd - pos) > width))
        break;
      end = wordEnd;
      scan = wordEnd;
      if (scan < length && text[scan] == ' ') ++scan;
    }
    size_t next = end;
    while (next < length && text[next] == ' ') ++next;
    const bool paragraphEnd = next >= length || text[next] == '\n';
    if (next < length && text[next] == '\n') ++next;
    if (!emit(Line{pos, end - pos, next, paragraphEnd})) return;
    ++index;
    pos = next;
  }
}

inline size_t lineCount(const fui::DrawTarget& target, const fui::TextStyle& style, const char* text, size_t length,
                        int width) {
  size_t lines = 0;
  wrap(target, style, text, length, Widths{width}, [&](const Line&) { return ++lines, true; });
  return lines;
}

// After sentence punctuation (optionally followed by closing quotes), at a space.
inline size_t sentenceEnd(const char* text, size_t from, size_t to) {
  for (size_t i = to; i > from; --i) {
    if (text[i - 1] != ' ') continue;
    size_t p = i - 1;
    // Skip closing quotes: " ' » « “ ” ‘ ’ and ) .
    while (p > from) {
      const auto c = static_cast<unsigned char>(text[p - 1]);
      if (c == '"' || c == '\'' || c == ')') {
        --p;
      } else if (p - from >= 2 && (c == 0xbb || c == 0xab) && static_cast<unsigned char>(text[p - 2]) == 0xc2) {
        p -= 2;
      } else if (p - from >= 3 && static_cast<unsigned char>(text[p - 3]) == 0xe2 &&
                 static_cast<unsigned char>(text[p - 2]) == 0x80 && c >= 0x98 && c <= 0x9d) {
        p -= 3;
      } else {
        break;
      }
    }
    if (p == from) continue;
    const auto c = static_cast<unsigned char>(text[p - 1]);
    const bool ellipsis = p - from >= 3 && c == 0xa6 && static_cast<unsigned char>(text[p - 2]) == 0x80 &&
                          static_cast<unsigned char>(text[p - 3]) == 0xe2;
    if (c == '.' || c == '!' || c == '?' || c == ':' || ellipsis) return i;
  }
  return 0;
}

struct Page {
  size_t length;  // Bytes consumed from the input, including trailing separators.
  size_t lines;   // Lines drawn for this page.
  bool last;      // The whole remaining text fits.
};

// Picks how much of text fits in maxLines. When it does not all fit, prefer a
// paragraph end, then a sentence end, within the last SOFT_BREAK_PERCENT of lines.
// The last line is markerWidth narrower (0 = full width).
inline Page paginate(const fui::DrawTarget& target, const fui::TextStyle& style, const char* text, size_t length,
                     int width, size_t maxLines, int markerWidth = 0) {
  if (!maxLines) maxLines = 1;
  constexpr size_t MAX_TRACKED = 64;
  if (maxLines > MAX_TRACKED) maxLines = MAX_TRACKED;
  Line lines[MAX_TRACKED];
  size_t count = 0;
  bool overflow = false;
  wrap(target, style, text, length, Widths{width, maxLines - 1, width - markerWidth}, [&](const Line& line) {
    if (count == maxLines) {
      overflow = true;
      return false;
    }
    lines[count++] = line;
    return true;
  });
  if (!overflow) return Page{length, count, true};

  const size_t soft = maxLines - (maxLines * SOFT_BREAK_PERCENT) / 100;
  const size_t floor = soft ? soft - 1 : 0;  // Index of the earliest acceptable last line.
  for (size_t i = maxLines; i-- > floor;) {
    // An empty line already ends a paragraph above it; do not end a page on it.
    if (lines[i].paragraphEnd && lines[i].length) return Page{lines[i].next, i + 1, false};
  }
  const size_t from = lines[floor].begin;
  const size_t to = lines[maxLines - 1].begin + lines[maxLines - 1].length;
  if (const size_t cut = sentenceEnd(text, from, to)) {
    size_t used = floor;
    while (used < maxLines && lines[used].begin < cut) ++used;
    return Page{cut, used, false};
  }
  return Page{lines[maxLines - 1].next, maxLines, false};
}

// Draws text[0, length) top-aligned, one line per lineHeight, never passing
// newlines to the glyph renderer. Lines past the area are dropped.
// Pass the same maxLines/markerWidth as paginate() so lines wrap identically.
inline void drawNarrative(fui::DrawTarget& target, fui::Rect area, const char* text, size_t length,
                          fui::TextStyle style, size_t maxLines = SIZE_MAX, int markerWidth = 0) {
  const int lineHeight = target.lineHeight(style.font);
  auto single = style;
  single.maxLines = 1;
  single.align = fui::TextAlign::Left;
  int y = area.y;
  const Widths widths = maxLines == SIZE_MAX ? Widths{area.width}
                                             : Widths{area.width, maxLines - 1, area.width - markerWidth};
  wrap(target, style, text, length, widths, [&](const Line& line) {
    if (y + lineHeight > area.y + area.height) return false;
    char buffer[LINE_BYTES + 1];
    const size_t n = line.length < LINE_BYTES ? line.length : LINE_BYTES;
    std::memcpy(buffer, text + line.begin, n);
    buffer[n] = 0;
    if (n) target.text(fui::Rect{area.x, static_cast<int16_t>(y), area.width, static_cast<int16_t>(lineHeight)},
                       buffer, single);
    y += lineHeight;
    return true;
  });
}

inline void drawNarrative(fui::DrawTarget& target, fui::Rect area, const char* text, fui::TextStyle style) {
  drawNarrative(target, area, text, std::strlen(text), style);
}
}  // namespace storyText
