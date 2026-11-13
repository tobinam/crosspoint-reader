#pragma once

// X4C modes (XTEINK_MODES): Reader, Stories, TV, Settings, switched by the power button's long press (or short
// press, swapped in Settings: "Power button and modes"). Waking reopens the mode slept in, and within the
// "Wake into the same screen" time the very screen. The tiles view stays, reached from Settings.

#include <cstdint>

namespace modes {

enum class Mode : uint8_t { Reader = 0, Stories, Tv, Settings, Count, None = 0xFF };

// Power button and modes in use (not Classic) and not "Start in the tiles view".
bool enabled();
// The power button's short/long press per setting (only with a mode setting, Classic: false for both).
bool shortPressSleeps();
bool longPressSleeps();

Mode current();
void setCurrent(Mode mode);  // a mode's entry point records itself (tiles, switch, wake)
const char* name(Mode mode);

// Shows the next mode's name large, then opens it.
void next();
// Opens a mode's main screen (Reader: the open book if any, else the reader home).
void open(Mode mode);

// Before deep sleep: the mode and the time, into APP_STATE (saved by the caller).
void noteSleep();
// At wake: the mode slept in and whether it was recent enough for the very screen.
Mode wakeMode(bool& recent);
// Once, for the mode woken into: whether it should restore its very screen (wakeMode found it recent).
bool takeRecentWake();

}  // namespace modes
