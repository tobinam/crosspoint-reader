#pragma once

// The TV remote (sai: xteink/README.md "TV remote", xteink/TV_REMOTE_PROTOCOL.md). Remote mode talks to x4bridge
// on tix over BLE: TV power, a D-pad view, a playback view, the clip groups (one clip or a whole group). Keyboard
// mode makes the X4 a BLE keyboard paired with the TV itself (D-pad, playback, power), chosen in this app; the
// choice is remembered.

#include "activities/UiListActivity.h"
#include "tv/TvRadio.h"

class TvRemoteActivity final : public UiListActivity {
  static constexpr size_t MAX_GROUPS = 32;
  static constexpr size_t MAX_CLIPS = 64;
  static constexpr size_t NAME = 49;
  static constexpr int FIXED_ROWS = 4;  // power, home, pad, playback  // the bridge's 48 bytes + NUL
  enum class View : uint8_t { Menu, Clips, Pad, Playback };
  enum class Row : uint8_t { Power, Home, Pad, Playback, Group, Mode, Pair, Forget, PlayAll, Clip };
  struct Name {
    char text[NAME]{};
  };

  View view = View::Menu;
  tv::Mode mode = tv::Mode::Remote;
  bool radioFailed = false;

  // What the bridge said last (remote mode).
  bool hello = false;
  char power[8] = "?";
  char host[8] = "";
  bool online = false;
  bool paused = false;
  char title[100] = "";
  int playingGroup = -1;
  uint32_t rev = 0;
  Name groups[MAX_GROUPS];
  uint16_t groupCount = 0, groupsLoaded = 0;
  int openGroup = -1;  // the group whose clips are shown
  Name clips[MAX_CLIPS];
  uint16_t clipCount = 0, clipsLoaded = 0;

  // Presses: numbered per connection; the last one's answer is shown.
  uint32_t seq = 0;
  uint32_t waitingFor = 0;
  char status[64] = "";
  enum class PowerGoal : uint8_t { None, On, Off };
  PowerGoal powerGoal = PowerGoal::None;

  char message[1025]{};

  static void provideRow(void* context, uint16_t index, freeink::ui::ListItem& item);
  Row rowKind(int index, int* item) const;
  void loadMode();
  void loadBridges();
  void saveBridges();
  uint32_t lastPairSecs = 0;  // the countdown shown, to redraw once per step
  void saveMode() const;
  void startRadio();
  void switchMode();
  void pollRadio();
  void handleMessage();
  void requestList(int group, int offset);
  void press(tv::Op op, int times = 1);
  void play(int group, int clip);
  void setStatus(const char* text);
  bool padInput();
  void leaveView();
  void resetLists();

 protected:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  const char* headerTitle() const override;
  void drawFooter() override;
  bool handleCustomInput() override;

 public:
  TvRemoteActivity(GfxRenderer& renderer, MappedInputManager& input);
  void onEnter() override;
  void onExit() override;
  void prepareForSleep() override;
  bool skipLoopDelay() override { return false; }
};
