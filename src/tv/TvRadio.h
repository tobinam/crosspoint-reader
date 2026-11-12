#pragma once

// The X4's TV remote radio (sai: xteink/TV_REMOTE_PROTOCOL.md). Owns NimBLE while the TV app runs, in one of two
// modes:
//  - Remote: a GATT peripheral "X4 Remote" (public address) with the RX/TX service; only the bridge on tix may stay
//    connected. Messages in and out are JSON, chunked by TvFraming.
//  - Keyboard: a BLE HID keyboard "X4 Keyboard" (its own static random address, so tix never connects to it) bonded
//    with the TV: arrows/Enter on the keyboard page, back/home/media/volume/power on the consumer page.
// The page turner (lib/BlePageTurner) uses NimBLE only while a book is open, so the two never overlap.

#include <cstddef>
#include <cstdint>

namespace tv {

enum class Mode : uint8_t { Remote, Keyboard };

// TV keys. Remote: sent as the deck_tv op of the same name; Keyboard: a HID usage.
enum class Op : uint8_t { On, Off, Home, Return, Toggle, Stop, Rew, Fwd, VolUp, VolDown, Mute, Up, Down, Left, Right, Ok };
const char* opName(Op op);

class Radio {
 public:
  bool start(Mode mode);  // false: NimBLE would not start (heap)
  void stop();
  bool running() const;
  Mode mode() const;
  bool connected() const;
  // Changes since the last call (connect/disconnect): the screen redraws.
  bool takeLinkChanged();

  // Remote mode. Completed messages from the bridge (NUL-terminated, valid until the next call); false: none.
  bool takeMessage(char* out, size_t outSize);
  bool send(const char* json, size_t length);

  // Keyboard mode: press and release one key.
  bool tap(Op op);
};

Radio& radio();

}  // namespace tv
