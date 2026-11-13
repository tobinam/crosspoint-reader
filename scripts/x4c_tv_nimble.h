// X4C TV remote (src/tv): room for more bonds than the prebuilt core's 3. Bridges (tix, a laptop) and the TV in
// keyboard mode filled it, and NimBLE then dropped the oldest bond. -include'd like s3_nimble_psram.h: after the
// prebuilt sdkconfig.h, before NimBLE's host sources.
#pragma once
#include <sdkconfig.h>

#undef CONFIG_BT_NIMBLE_MAX_BONDS
#define CONFIG_BT_NIMBLE_MAX_BONDS 8
