#pragma once

// The X4's TV remote radio (sai: xteink/TV_REMOTE_PROTOCOL.md). Owns NimBLE while the TV app runs, in one of two
// modes:
//  - Remote: a GATT peripheral "X4 Remote" (public address) with the RX/TX service. Only bridges paired here
//    (bonded, encrypted: LE Secure Connections) may stay connected; a new one pairs while the user holds the pairing
//    window open. Messages in and out are JSON, chunked by TvFraming.
//  - Keyboard: a BLE HID keyboard "X4 Keyboard" (its own static random address, so tix never connects to it) bonded
//    with the TV: arrows/Enter on the keyboard page, back/home/media/volume/power on the consumer page.
// The page turner (lib/BlePageTurner) uses NimBLE only while a book is open, so the two never overlap.

#include <cstddef>
#include <cstdint>

namespace tv {

enum class Mode : uint8_t { Remote, Keyboard };
constexpr size_t MAX_BRIDGES = 4;

// TV keys. Remote: sent as the deck_tv op of the same name; Keyboard: a HID usage.
enum class Op : uint8_t { On, Off, Home, Return, Toggle, Stop, Rew, Fwd, VolUp, VolDown, Mute, Up, Down, Left, Right, Ok };
const char* opName(Op op);

class Radio {
 public:
  bool start(Mode mode);  // false: NimBLE would not start (heap)
  void stop();
  bool running() const;
  Mode mode() const;
  bool connected() const;  // remote: a paired bridge on an encrypted link

  // Paired bridges (identity addresses, lowercase): loaded/saved by the activity.
  void setBridges(const char* const* ids, size_t count);
  size_t bridges(char (*out)[18], size_t max) const;
  bool takeBridgesChanged();
  void tick();  // main loop: drops a remote-mode link that is not secured in time
  void openPairing(uint32_t ms);  // a new bridge may pair until then (one per window)
  uint32_t pairingLeftMs() const;
  void forgetBridges();            // their bonds too
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
