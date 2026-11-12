#if XTEINK_TV_REMOTE
#include "TvRemoteActivity.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "components/UITheme.h"

namespace fui = freeink::ui;
using Button = MappedInputManager::Button;

namespace {
constexpr char MODE_FILE[] = "/.crosspoint/tv_mode";
constexpr char BRIDGES_FILE[] = "/.crosspoint/tv_bridges";  // paired bridges' identity addresses, one per line
constexpr uint32_t PAIR_WINDOW_MS = 60000;
constexpr unsigned long HOLD_MS = 600;

void copy(char* out, size_t size, const char* in) {
  std::strncpy(out, in ? in : "", size - 1);
  out[size - 1] = 0;
}
}  // namespace

TvRemoteActivity::TvRemoteActivity(GfxRenderer& renderer, MappedInputManager& input)
    : UiListActivity("TvRemote", renderer, input) {}

// --- mode and radio ----------------------------------------------------------

void TvRemoteActivity::loadMode() {
  char text[16] = "";
  Storage.readFileToBuffer(MODE_FILE, text, sizeof(text));
  mode = std::strncmp(text, "keyboard", 8) == 0 ? tv::Mode::Keyboard : tv::Mode::Remote;
}

void TvRemoteActivity::saveMode() const {
  Storage.ensureDirectoryExists("/.crosspoint");
  Storage.writeFile(MODE_FILE, mode == tv::Mode::Keyboard ? "keyboard" : "remote");
}

void TvRemoteActivity::loadBridges() {
  char text[tv::MAX_BRIDGES * 18 + 8] = "";
  Storage.readFileToBuffer(BRIDGES_FILE, text, sizeof(text));
  const char* ids[tv::MAX_BRIDGES];
  size_t n = 0;
  for (char* line = std::strtok(text, "\r\n"); line && n < tv::MAX_BRIDGES; line = std::strtok(nullptr, "\r\n"))
    if (std::strlen(line) == 17) ids[n++] = line;
  tv::radio().setBridges(ids, n);
}

void TvRemoteActivity::saveBridges() {
  char ids[tv::MAX_BRIDGES][18];
  const size_t n = tv::radio().bridges(ids, tv::MAX_BRIDGES);
  String text;
  for (size_t i = 0; i < n; ++i) {
    text += ids[i];
    text += "\n";
  }
  Storage.ensureDirectoryExists("/.crosspoint");
  Storage.writeFile(BRIDGES_FILE, text);
}

void TvRemoteActivity::resetLists() {
  hello = false;
  groupCount = groupsLoaded = 0;
  clipCount = clipsLoaded = 0;
  openGroup = -1;
  rev = 0;
  seq = waitingFor = 0;
  status[0] = 0;
}

void TvRemoteActivity::startRadio() {
  resetLists();
  radioFailed = !tv::radio().start(mode);
  if (!radioFailed) loadBridges();
}

void TvRemoteActivity::switchMode() {
  {
    RenderLock lock(*this);
    tv::radio().stop();
    mode = mode == tv::Mode::Remote ? tv::Mode::Keyboard : tv::Mode::Remote;
    saveMode();
    view = View::Menu;
    startRadio();
  }
  moveSelectionTo(0);
}

void TvRemoteActivity::onEnter() {
  loadMode();
  UiListActivity::onEnter();
  startRadio();
  requestUpdate();
}

void TvRemoteActivity::onExit() {
  tv::radio().stop();
  UiListActivity::onExit();
}

void TvRemoteActivity::prepareForSleep() { tv::radio().stop(); }

// --- the bridge's messages ------------------------------------------------------

void TvRemoteActivity::requestList(const int group, const int offset) {
  char m[48];
  const int n = group < 0 ? std::snprintf(m, sizeof(m), "{\"t\":\"list\",\"o\":%d}", offset)
                          : std::snprintf(m, sizeof(m), "{\"t\":\"list\",\"g\":%d,\"o\":%d}", group, offset);
  tv::radio().send(m, n);
}

void TvRemoteActivity::setStatus(const char* text) { copy(status, sizeof(status), text); }

void TvRemoteActivity::pollRadio() {
  auto& r = tv::radio();
  r.tick();
  if (r.takeBridgesChanged()) {
    saveBridges();
    char ids[tv::MAX_BRIDGES][18];
    const size_t n = r.bridges(ids, tv::MAX_BRIDGES);
    if (n) {
      char line[sizeof(status)];
      std::snprintf(line, sizeof(line), tr(STR_TV_PAIRED), ids[n - 1]);  // the newest: check it is yours
      setStatus(line);
    } else {
      status[0] = 0;
    }
    requestUpdate();
  }
  const uint32_t pairSecs = (r.pairingLeftMs() + 9999) / 10000 * 10;  // redraw every 10 s, not every second
  if (pairSecs != lastPairSecs) {
    lastPairSecs = pairSecs;
    requestUpdate();
  }
  if (r.takeLinkChanged()) {
    RenderLock lock(*this);
    if (!r.connected()) {
      hello = false;
      online = false;
      waitingFor = 0;
    }
    seq = 0;
    requestUpdate();
  }
  if (mode != tv::Mode::Remote) return;
  for (int i = 0; i < 4 && r.takeMessage(message, sizeof(message)); ++i) handleMessage();
}

void TvRemoteActivity::handleMessage() {
  JsonDocument doc;
  if (deserializeJson(doc, message)) return;
  const char* t = doc["t"] | "";
  bool redraw = false;
  RenderLock lock(*this);
  if (!std::strcmp(t, "hello")) {
    hello = true;
    tv::radio().send("{\"t\":\"hello\",\"v\":1}", 19);
  } else if (!std::strcmp(t, "state")) {
    char newPower[8], newHost[8], newTitle[sizeof(title)];
    copy(newPower, sizeof(newPower), doc["power"] | "?");
    copy(newHost, sizeof(newHost), doc["host"] | "");
    copy(newTitle, sizeof(newTitle), doc["title"] | "");
    const bool newOnline = doc["online"] | false;
    const bool newPaused = doc["paused"] | false;
    const int newGroup = doc["playing_group"].is<int>() ? doc["playing_group"].as<int>() : -1;
    // e-ink: redraw only for what this screen shows
    redraw = std::strcmp(newPower, power) || std::strcmp(newHost, host) || newOnline != online ||
             (view != View::Pad && (std::strcmp(newTitle, title) || newPaused != paused || newGroup != playingGroup));
    copy(power, sizeof(power), newPower);
    copy(host, sizeof(host), newHost);
    copy(title, sizeof(title), newTitle);
    online = newOnline;
    paused = newPaused;
    if ((powerGoal == PowerGoal::On && !std::strcmp(power, "ON")) ||
        (powerGoal == PowerGoal::Off && !std::strcmp(power, "OFF"))) {
      powerGoal = PowerGoal::None;
      status[0] = 0;
      redraw = true;
    } else if (powerGoal == PowerGoal::On && !std::strcmp(power, "WAKING")) {
      setStatus(tr(STR_TV_WAKING));
    }
    playingGroup = newGroup;
    const uint32_t r = doc["r"] | 0u;
    if (r != rev && groupsLoaded) requestList(-1, 0);  // groups page 0 usually follows by itself; ask anyway
  } else if (!std::strcmp(t, "groups")) {
    const uint32_t r = doc["r"] | 0u;
    const int o = doc["o"] | 0;
    if (r != rev || o == 0) {
      rev = r;
      groupsLoaded = 0;
      if (openGroup >= 0) {  // the clips shown belong to an older list: load them again
        clipsLoaded = clipCount = 0;
        requestList(openGroup, 0);
      }
    }
    groupCount = static_cast<uint16_t>(std::min<int>(doc["n"] | 0, MAX_GROUPS));
    if (o == groupsLoaded) {
      for (JsonVariant v : doc["items"].as<JsonArray>()) {
        if (groupsLoaded >= groupCount) break;
        copy(groups[groupsLoaded++].text, NAME, v | "");
      }
    }
    if (groupsLoaded < groupCount) requestList(-1, groupsLoaded);
    redraw = view == View::Menu;
  } else if (!std::strcmp(t, "clips")) {
    const int g = doc["g"] | -1;
    const int o = doc["o"] | 0;
    if (g == openGroup && (doc["r"] | 0u) == rev) {
      if (o == 0) clipsLoaded = 0;
      clipCount = static_cast<uint16_t>(std::min<int>(doc["n"] | 0, MAX_CLIPS));
      if (o == clipsLoaded) {
        for (JsonVariant v : doc["items"].as<JsonArray>()) {
          if (clipsLoaded >= clipCount) break;
          copy(clips[clipsLoaded++].text, NAME, v | "");
        }
      }
      if (clipsLoaded < clipCount) requestList(openGroup, clipsLoaded);
      redraw = view == View::Clips;
    }
  } else if (!std::strcmp(t, "ack")) {
    const uint32_t s = doc["s"] | 0u;
    if (s == waitingFor) {
      waitingFor = 0;
      const bool ok = doc["ok"] | false;
      const char* err = doc["err"] | "";
      if (ok) {
        if (powerGoal == PowerGoal::None) setStatus("");
      } else {
        powerGoal = PowerGoal::None;
        char line[sizeof(status)];
        std::snprintf(line, sizeof(line), "%s: %s", tr(STR_TV_FAILED),
                      !std::strcmp(err, "stale") ? tr(STR_TV_STALE) : err);
        setStatus(line);
        if (!std::strcmp(err, "stale")) requestList(-1, 0);
      }
      redraw = true;  // a failure must be seen; an OK clears "Sent"
    }
  }
  if (redraw) requestUpdate();
}

// --- presses -----------------------------------------------------------------

void TvRemoteActivity::press(const tv::Op op, const int times) {
  auto& r = tv::radio();
  if (!r.connected()) {
    setStatus(tr(STR_TV_NOT_CONNECTED));
    requestUpdate();
    return;
  }
  if (mode == tv::Mode::Keyboard) {
    for (int i = 0; i < times; ++i) r.tap(op);
    return;
  }
  char m[64];
  const int n = std::snprintf(m, sizeof(m), "{\"t\":\"act\",\"s\":%u,\"op\":\"%s\",\"n\":%d}",
                              static_cast<unsigned>(++seq), tv::opName(op), times);
  waitingFor = seq;
  if (!r.send(m, n)) setStatus(tr(STR_TV_FAILED));
  // no redraw for a sent press: e-ink would slow every key down; failures redraw (ack)
}

void TvRemoteActivity::play(const int group, const int clip) {
  auto& r = tv::radio();
  if (!r.connected()) {
    setStatus(tr(STR_TV_NOT_CONNECTED));
    requestUpdate();
    return;
  }
  char m[96];
  const int n =
      clip < 0 ? std::snprintf(m, sizeof(m), "{\"t\":\"play\",\"s\":%u,\"r\":%u,\"g\":%d}", static_cast<unsigned>(++seq),
                               static_cast<unsigned>(rev), group)
               : std::snprintf(m, sizeof(m), "{\"t\":\"play\",\"s\":%u,\"r\":%u,\"g\":%d,\"c\":%d}",
                               static_cast<unsigned>(++seq), static_cast<unsigned>(rev), group, clip);
  waitingFor = seq;
  setStatus(r.send(m, n) ? tr(STR_TV_SENT) : tr(STR_TV_FAILED));
  requestUpdate();
}

// Pad and playback views: every key but Back goes to the TV; Back (short or held) always leaves the view.
bool TvRemoteActivity::padInput() {
  if (mappedInput.wasLongPressed(Button::Back, HOLD_MS) || mappedInput.wasReleased(Button::Back)) {
    leaveView();
    return true;
  }
  const bool pad = view == View::Pad;
  if (mappedInput.wasLongPressed(Button::Confirm, HOLD_MS)) {
    press(pad ? tv::Op::Return : tv::Op::Mute);
    return true;
  }
  if (mappedInput.wasReleased(Button::Confirm)) press(pad ? tv::Op::Ok : tv::Op::Toggle);
  // Volume: the side keys the other way round from the D-pad (on hardware, Up turned it down)
  else if (mappedInput.wasPressed(Button::Up)) press(pad ? tv::Op::Up : tv::Op::VolDown);
  else if (mappedInput.wasPressed(Button::Down)) press(pad ? tv::Op::Down : tv::Op::VolUp);
  else if (mappedInput.wasPressed(Button::Left)) press(pad ? tv::Op::Left : tv::Op::Rew);
  else if (mappedInput.wasPressed(Button::Right)) press(pad ? tv::Op::Right : tv::Op::Fwd);
  return true;  // the list underneath gets nothing
}

void TvRemoteActivity::leaveView() {
  if (view == View::Menu) {
    activityManager.goHome();
    return;
  }
  view = View::Menu;
  status[0] = 0;
  moveSelectionTo(0);
}

bool TvRemoteActivity::handleCustomInput() {
  pollRadio();
  if (view == View::Pad || view == View::Playback) return padInput();
  if (mappedInput.wasLongPressed(Button::Back, HOLD_MS)) {
    activityManager.goHome();
    return true;
  }
  return false;
}

void TvRemoteActivity::onBackButton() { leaveView(); }

// --- rows --------------------------------------------------------------------

int TvRemoteActivity::listCount() const {
  switch (view) {
    case View::Menu:
      return FIXED_ROWS + (mode == tv::Mode::Remote ? groupsLoaded + 3 : 1);  // remote: mode, pair, forget
    case View::Clips:
      return 1 + clipsLoaded;
    default:
      return 0;
  }
}

TvRemoteActivity::Row TvRemoteActivity::rowKind(const int index, int* item) const {
  if (view == View::Clips) {
    if (item) *item = index - 1;
    return index == 0 ? Row::PlayAll : Row::Clip;
  }
  if (index == 0) return Row::Power;
  if (index == 1) return Row::Home;
  if (index == 2) return Row::Pad;
  if (index == 3) return Row::Playback;
  const int groupsShown = mode == tv::Mode::Remote ? groupsLoaded : 0;
  if (index - FIXED_ROWS < groupsShown) {
    if (item) *item = index - FIXED_ROWS;
    return Row::Group;
  }
  const int after = index - FIXED_ROWS - groupsShown;
  return after == 1 ? Row::Pair : after == 2 ? Row::Forget : Row::Mode;
}

void TvRemoteActivity::provideRow(void* context, const uint16_t index, fui::ListItem& item) {
  const auto& self = *static_cast<TvRemoteActivity*>(context);
  int i = 0;
  item.value = nullptr;
  item.actionValue = index;
  switch (self.rowKind(index, &i)) {
    case Row::Power:
      item.label = !std::strcmp(self.power, "ON") ? tr(STR_TV_POWER_OFF) : tr(STR_TV_POWER_ON);
      break;
    case Row::Home:
      item.label = tr(STR_TV_HOME);
      break;
    case Row::Pad:
      item.label = tr(STR_TV_PAD);
      break;
    case Row::Playback:
      item.label = tr(STR_TV_PLAYBACK);
      break;
    case Row::Group:
      item.label = self.groups[i].text;
      if (i == self.playingGroup) item.value = "\xE2\x96\xB6";  // ▶
      break;
    case Row::Mode:
      item.label = self.mode == tv::Mode::Remote ? tr(STR_TV_KEYBOARD_MODE) : tr(STR_TV_REMOTE_MODE);
      break;
    case Row::Pair:
      item.label = tr(STR_TV_PAIR_BRIDGE);
      break;
    case Row::Forget:
      item.label = tr(STR_TV_FORGET_BRIDGES);
      break;
    case Row::PlayAll:
      item.label = tr(STR_TV_PLAY_ALL);
      break;
    case Row::Clip:
      item.label = self.clips[i].text;
      break;
  }
}

void TvRemoteActivity::activateIndex(const int index) {
  int i = 0;
  app.clearTapFlash();
  switch (rowKind(index, &i)) {
    case Row::Power: {
      // keyboard mode: the TV's Power key both ways; remote: what the bridge reported
      const bool on = mode == tv::Mode::Keyboard || std::strcmp(power, "ON");
      press(on ? tv::Op::On : tv::Op::Off);
      if (mode == tv::Mode::Remote && tv::radio().connected()) {
        powerGoal = on ? PowerGoal::On : PowerGoal::Off;  // shown until the bridge reports it done
        setStatus(on ? tr(STR_TV_SWITCHING_ON) : tr(STR_TV_SWITCHING_OFF));
        requestUpdate();
      }
      return;
    }
    case Row::Home:
      press(tv::Op::Home);
      return;
    case Row::Pad:
      view = View::Pad;
      status[0] = 0;
      requestUpdate();
      return;
    case Row::Playback:
      view = View::Playback;
      status[0] = 0;
      requestUpdate();
      return;
    case Row::Group: {
      RenderLock lock(*this);
      openGroup = i;
      clipCount = clipsLoaded = 0;
      view = View::Clips;
      status[0] = 0;
      requestList(i, 0);
    }
      moveSelectionTo(0);
      return;
    case Row::Mode:
      switchMode();
      return;
    case Row::Pair:
      tv::radio().openPairing(PAIR_WINDOW_MS);
      requestUpdate();
      return;
    case Row::Forget:
      tv::radio().forgetBridges();  // saved by pollRadio (bridgesChanged)
      requestUpdate();
      return;
    case Row::PlayAll:
      play(openGroup, -1);
      return;
    case Row::Clip:
      play(openGroup, i);
      return;
  }
}

// --- drawing -----------------------------------------------------------------

const char* TvRemoteActivity::headerTitle() const {
  if (view == View::Clips && openGroup >= 0 && openGroup < groupsLoaded) return groups[openGroup].text;
  if (view == View::Pad) return tr(STR_TV_PAD);
  if (view == View::Playback) return tr(STR_TV_PLAYBACK);
  return tr(STR_TV_TITLE);
}

void TvRemoteActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true);
  const int top = safe.y + metrics.topPadding + metrics.headerHeight;
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(top), static_cast<int16_t>(renderer.getScreenWidth() - safe.x - safe.width),
      static_cast<int16_t>(renderer.getScreenHeight() - safe.y - safe.height), static_cast<int16_t>(safe.x)});
  const auto& theme = screen.theme();
  auto text = theme.smallText;
  text.maxLines = 3;

  // Where the link stands, what plays, the last press's trouble.
  char line[200];
  const auto& r = tv::radio();
  if (radioFailed) {
    copy(line, sizeof(line), tr(STR_TV_NO_RADIO));
  } else if (mode == tv::Mode::Keyboard) {
    copy(line, sizeof(line), r.connected() ? tr(STR_TV_KEYBOARD_CONNECTED) : tr(STR_TV_PAIR_HINT));
  } else if (const uint32_t left = r.pairingLeftMs()) {
    std::snprintf(line, sizeof(line), tr(STR_TV_PAIRING_OPEN), static_cast<unsigned>((left + 999) / 1000));
  } else if (!r.connected()) {
    char ids[1][18];
    copy(line, sizeof(line), r.bridges(ids, 1) ? tr(STR_TV_WAITING) : tr(STR_TV_NO_BRIDGE));
  } else if (!online) {
    copy(line, sizeof(line), tr(STR_TV_OFFLINE));
  } else {
    std::snprintf(line, sizeof(line), "%s \xC2\xB7 TV %s%s%s%s", host, power, title[0] ? " \xC2\xB7 " : "",
                  paused ? "\xE2\x8F\xB8 " : "", title);
  }
  const int lineHeight = screen.target().lineHeight(text.font);
  screen.target().text(screen.takeTop(lineHeight * 2, theme.spaceSm), line, text);
  if (status[0]) {  // a separate text: the font has no glyph for a line break
    auto bold = text;
    bold.bold = true;
    bold.maxLines = 1;
    screen.target().text(screen.takeTop(lineHeight, theme.spaceSm), status, bold);
  }

  if (view == View::Pad || view == View::Playback) {
    auto help = theme.bodyText;
    help.maxLines = 8;
    screen.target().text(screen.body(), view == View::Pad ? tr(STR_TV_PAD_HELP) : tr(STR_TV_PLAYBACK_HELP), help);
    return;
  }
  fui::ListProps props;
  props.rowProvider = &TvRemoteActivity::provideRow;
  props.rowProviderCtx = this;
  props.count = listCount();
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = theme.bodyText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void TvRemoteActivity::drawFooter() {
  if (view == View::Pad || view == View::Playback) return;  // the help text says it
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

#endif
