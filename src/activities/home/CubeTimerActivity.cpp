#if XTEINK_CUBE_TIMER
#include "CubeTimerActivity.h"

#include <BoardConfig.h>
#include <I18n.h>
#include <Logging.h>
#include <driver/gpio.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "cube/CubeLog.h"
#include "fontIds.h"
#if XTEINK_MODES
#include "modes/Modes.h"
#endif

namespace {
constexpr int64_t SAMPLE_US = 1000;
constexpr unsigned long EXIT_HOLD_MS = 800;
constexpr int64_t RUNNING_STEP_US = 500000;
constexpr int PAD = 10;
constexpr int MAX_DIGIT_HEIGHT = 120;

// Seat 0 sits at the Back end of the key row (bottom edge of the panel's native landscape), seat 1 at the Right end.
struct SeatKeys {
  GfxRenderer::Orientation orientation;
  uint8_t arm;
  uint8_t other;
  char name;
};
constexpr SeatKeys SEATS[2] = {{GfxRenderer::LandscapeCounterClockwise, 0, 1, 'A'},
                               {GfxRenderer::LandscapeClockwise, 3, 2, 'B'}};

int8_t pinOf(const uint8_t key) {
  const auto& input = BoardConfig::ACTIVE.input;
  const int8_t pins[4] = {input.back, input.confirm, input.left, input.right};
  return pins[key];
}

bool keyDown(const uint8_t key) { return gpio_get_level(static_cast<gpio_num_t>(pinOf(key))) == 0; }

// Seven segments, bit 0 = top, clockwise, bit 6 = middle.
constexpr uint8_t DIGITS[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

int advance(const char c, const int h) {
  const int t = std::max(2, h / 9);
  return (c >= '0' && c <= '9') || c == '-' ? h / 2 + t : 2 * t;
}

void drawSegments(const GfxRenderer& r, const char* text, const int centerX, const int y, const int h,
                  const bool black) {
  const int t = std::max(2, h / 9);
  const int w = h / 2;
  int width = 0;
  for (const char* c = text; *c; ++c) width += advance(*c, h);
  int x = centerX - (width - t) / 2;
  for (const char* c = text; *c; ++c) {
    if (*c == '.') {
      r.fillRect(x, y + h - t, t, t, black);
    } else if (*c == ':') {
      r.fillRect(x, y + h / 3 - t / 2, t, t, black);
      r.fillRect(x, y + 2 * h / 3 - t / 2, t, t, black);
    } else {
      const uint8_t mask = *c == '-' ? 0x40 : DIGITS[*c - '0'];
      const int half = h / 2;
      if (mask & 0x01) r.fillRect(x, y, w, t, black);
      if (mask & 0x02) r.fillRect(x + w - t, y, t, half, black);
      if (mask & 0x04) r.fillRect(x + w - t, y + half, t, h - half, black);
      if (mask & 0x08) r.fillRect(x, y + h - t, w, t, black);
      if (mask & 0x10) r.fillRect(x, y + half, t, h - half, black);
      if (mask & 0x20) r.fillRect(x, y, t, half, black);
      if (mask & 0x40) r.fillRect(x, y + half - t / 2, w, t, black);
    }
    x += advance(*c, h);
  }
}

// A seat's big number: running in half seconds, 0.000 while arming, else its result this round (or the last one).
void timeText(const cube::Match& match, const int index, const int64_t now, char* out, const size_t size) {
  using cube::Phase;
  const auto& seat = match.seats[index];
  if (seat.phase == Phase::Holding || seat.phase == Phase::Ready) {
    cube::formatTime(0, 3, out, size);
  } else if (seat.phase == Phase::Running) {
    const int64_t us = seat.shownUs(now);
    cube::formatTime(us - us % RUNNING_STEP_US, 1, out, size);
  } else {
    cube::formatTime(match.roundUs[index], 3, out, size);
  }
}

// Digits as large as fit the box, centered in it.
void drawClock(const GfxRenderer& r, const char* text, const int top, const int bottom, const bool black) {
  constexpr int PROBE = 100;
  int probeWidth = 0;
  for (const char* c = text; *c; ++c) probeWidth += advance(*c, PROBE);
  const int width = r.getScreenWidth() - 4 * PAD;
  const int h = std::min(bottom - top, probeWidth ? width * PROBE / probeWidth : bottom - top);
  drawSegments(r, text, r.getScreenWidth() / 2, top + (bottom - top - h) / 2, h, black);
}
}  // namespace

CubeTimerActivity::CubeTimerActivity(GfxRenderer& renderer, MappedInputManager& input)
    : Activity("CubeTimer", renderer, input) {}

void CubeTimerActivity::onEnter() {
  Activity::onEnter();
#if XTEINK_MODES
  modes::setCurrent(modes::Mode::Cube);  // the power button's next mode follows from here
#endif
  savedOrientation = renderer.getOrientation();
  session = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
  cube::loadBests(match.seats[0].bestUs, match.seats[1].bestUs);
  match.autoNext = cube::loadAutoNext();
  for (uint8_t key = 0; key < 4; ++key)
    if (keyDown(key)) committed |= 1u << key;
  // 32 events cover far more key edges than two players make between two loop passes.
  events = xQueueCreate(32, sizeof(KeyEvent));
  esp_timer_create_args_t args{};
  args.callback = &CubeTimerActivity::sample;
  args.arg = this;
  args.dispatch_method = ESP_TIMER_TASK;
  args.name = "cube_keys";
  args.skip_unhandled_events = true;
  if (!events || esp_timer_create(&args, &sampler) != ESP_OK || esp_timer_start_periodic(sampler, SAMPLE_US) != ESP_OK)
    LOG_ERR("CUBE", "Key sampler not started");
  requestUpdate();
}

void CubeTimerActivity::onExit() {
  if (sampler) {
    esp_timer_stop(sampler);
    esp_timer_delete(sampler);
    sampler = nullptr;
  }
  if (events) {
    vQueueDelete(events);
    events = nullptr;
  }
  renderer.setOrientation(savedOrientation);
  Activity::onExit();
}

void CubeTimerActivity::sample(void* arg) {
  auto& self = *static_cast<CubeTimerActivity*>(arg);
  const int64_t now = esp_timer_get_time();
  for (uint8_t key = 0; key < 4; ++key) {
    const bool down = keyDown(key);
    if (down == static_cast<bool>(self.committed & (1u << key))) {
      self.changedSamples[key] = 0;
      if (self.quietSamples[key] < STABLE_SAMPLES) ++self.quietSamples[key];
      continue;
    }
    // A change after a quiet key is the real edge; one after a short bounce keeps the first edge's time.
    if (self.quietSamples[key] >= STABLE_SAMPLES) self.pendingSince[key] = now;
    self.quietSamples[key] = 0;
    if (++self.changedSamples[key] < STABLE_SAMPLES) continue;
    self.committed ^= 1u << key;
    self.changedSamples[key] = 0;
    const KeyEvent event{self.pendingSince[key], key, down};
    xQueueSend(self.events, &event, 0);
  }
}

void CubeTimerActivity::handle(const KeyEvent& event) {
  const int index = event.key < 2 ? 0 : 1;
  const bool arm = event.key == SEATS[index].arm;
  bool stopped = false;
  taskENTER_CRITICAL(&matchMux);
  if (event.down)
    stopped = match.press(index, arm, event.t);
  else
    match.release(index, arm, event.t);
  const uint32_t round = match.round;
  const int64_t us = match.roundUs[index];
  taskEXIT_CRITICAL(&matchMux);
  if (!stopped) return;
  char text[cube::SCRAMBLE_BYTES];
  cube::scramble(session, round, text);
  const time_t now = time(nullptr);
  // Before 2020 the clock was never set.
  if (!cube::appendSolve(now > 1577836800 ? now : 0, session, SEATS[index].name, round, us, text))
    LOG_ERR("CUBE", "Solve not saved");
}

void CubeTimerActivity::describe(const cube::Match& state, const int64_t now, char* out, const size_t size) const {
  char a[16], b[16];
  timeText(state, 0, now, a, sizeof(a));
  timeText(state, 1, now, b, sizeof(b));
  const int frame = state.stage == cube::Match::Stage::Result ? state.frame(now) : -1;
  std::snprintf(out, size, "%d %lu %d %d%d %s %s %d", static_cast<int>(state.stage),
                static_cast<unsigned long>(state.round), frame, static_cast<int>(state.seats[0].phase),
                static_cast<int>(state.seats[1].phase), a, b, state.autoNext);
}

void CubeTimerActivity::loop() {
  KeyEvent event;
  while (events && xQueueReceive(events, &event, 0) == pdTRUE) handle(event);
  const int64_t now = esp_timer_get_time();

  using Button = MappedInputManager::Button;
  if (mappedInput.wasLongPressed(Button::Up, EXIT_HOLD_MS) || mappedInput.wasLongPressed(Button::Down, EXIT_HOLD_MS)) {
    activityManager.goHome();
    return;
  }
  const bool toggle = mappedInput.wasReleased(Button::Up);
  taskENTER_CRITICAL(&matchMux);
  if (toggle) match.autoNext = !match.autoNext;
  match.tick(now);
  cube::Match state = match;
  taskEXIT_CRITICAL(&matchMux);
  if (toggle) cube::saveAutoNext(state.autoNext);

  char current[sizeof(shown)];
  describe(state, now, current, sizeof(current));
  if (std::strcmp(current, shown)) {
    std::strcpy(shown, current);
    requestUpdate();
  }
}

bool CubeTimerActivity::preventAutoSleep() { return match.busy(0) || match.busy(1); }

void CubeTimerActivity::drawSeat(const int index, const cube::Match& state, const int64_t now) const {
  using cube::Phase;
  using Stage = cube::Match::Stage;
  const auto& seat = state.seats[index];
  renderer.setOrientation(SEATS[index].orientation);
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int top = height / 2;
  const int large = renderer.getLineHeight(NOTOSANS_18_FONT_ID);
  const int small = renderer.getLineHeight(UI_12_FONT_ID);
  const int bottomLine = height - PAD - small;
  renderer.fillRect(0, top - 1, width, 2);
  char digits[24];
  timeText(state, index, now, digits, sizeof(digits));

  if (state.stage == Stage::Play && state.busy(index)) {
    const bool ready = seat.phase == Phase::Ready;
    if (ready) renderer.fillRect(0, top + 1, width, height - top - 1);
    if (seat.phase == Phase::Holding) {
      renderer.drawCenteredText(UI_12_FONT_ID, bottomLine, tr(STR_CUBE_HINT_HOLD));
      drawClock(renderer, digits, top + PAD, bottomLine - PAD, true);
    } else {
      drawClock(renderer, digits, top + PAD, height - PAD, !ready);
    }
    return;
  }

  if (state.stage == Stage::Play && state.roundUs[index] == cube::NO_TIME) {
    char scrambleText[cube::SCRAMBLE_BYTES];
    cube::scramble(session, state.round, scrambleText);
    int y = top + 2 * PAD;
    for (char* line = scrambleText; line;) {
      char* next = std::strchr(line, '\n');
      if (next) *next++ = 0;
      renderer.drawCenteredText(NOTOSANS_18_FONT_ID, y, line, true, EpdFontFamily::BOLD);
      y += large + PAD;
      line = next;
    }
    char best[16], average[16], stats[96];
    cube::formatTime(seat.bestUs, 3, best, sizeof(best));
    cube::formatTime(seat.average5(), 3, average, sizeof(average));
    std::snprintf(stats, sizeof(stats), "%s %s    %s %s    %s %lu", tr(STR_CUBE_BEST), best, tr(STR_CUBE_AO5),
                  average, tr(STR_CUBE_SOLVES), static_cast<unsigned long>(seat.solves));
    renderer.drawCenteredText(UI_12_FONT_ID, bottomLine, stats);
    renderer.drawCenteredText(UI_12_FONT_ID, bottomLine - small - PAD / 2, tr(STR_CUBE_HINT_IDLE));
    renderer.drawCenteredText(UI_12_FONT_ID, bottomLine - 2 * small - PAD,
                              state.autoNext ? tr(STR_CUBE_AUTO_ON) : tr(STR_CUBE_AUTO_OFF));
    return;
  }

  if (state.stage == Stage::Play) {
    renderer.drawCenteredText(UI_12_FONT_ID, bottomLine, tr(STR_CUBE_WAITING));
    drawClock(renderer, digits, top + PAD, bottomLine - PAD, true);
    return;
  }

  // Result: the winner's half flashes, then keeps a frame; the times stay large.
  const int winner = state.winner();
  const bool flash = state.stage == Stage::Result && winner == index && state.frame(now) % 2 == 0;
  if (flash) renderer.fillRect(0, top + 1, width, height - top - 1);
  if (winner == index && !flash) {
    constexpr int LINE = 6;
    for (const int inset : {0, 2 * LINE}) {
      const int y0 = top + 1 + inset;
      const int boxWidth = width - 2 * inset;
      const int boxHeight = height - y0 - inset;
      renderer.fillRect(inset, y0, boxWidth, LINE);
      renderer.fillRect(inset, y0 + boxHeight - LINE, boxWidth, LINE);
      renderer.fillRect(inset, y0, LINE, boxHeight);
      renderer.fillRect(inset + boxWidth - LINE, y0, LINE, boxHeight);
    }
  }
  char verdict[64] = "";
  const int64_t mine = state.roundUs[index];
  if (mine == cube::NO_TIME) {
    std::snprintf(verdict, sizeof(verdict), "%s", tr(STR_CUBE_NO_SOLVE));
  } else if (winner == index) {
    std::snprintf(verdict, sizeof(verdict), "%s", tr(STR_CUBE_WINNER));
  } else if (winner >= 0) {
    char behind[16];
    cube::formatTime(mine - state.roundUs[winner], 3, behind, sizeof(behind));
    std::snprintf(verdict, sizeof(verdict), "+%s", behind);
  }
  if (mine != cube::NO_TIME && mine == seat.bestUs) {
    const size_t length = std::strlen(verdict);
    std::snprintf(verdict + length, sizeof(verdict) - length, "%s%s", length ? "   " : "", tr(STR_CUBE_NEW_BEST));
  }
  const bool waiting = state.stage == Stage::Wait;
  const int verdictY = (waiting ? bottomLine - small : height - PAD) - large;
  renderer.drawCenteredText(NOTOSANS_18_FONT_ID, verdictY, verdict, !flash, EpdFontFamily::BOLD);
  if (waiting) renderer.drawCenteredText(UI_12_FONT_ID, bottomLine, tr(STR_CUBE_PRESS_NEXT));
  drawClock(renderer, digits, top + 2 * PAD, verdictY - PAD, !flash);
}

void CubeTimerActivity::render(RenderLock&&) {
  taskENTER_CRITICAL(&matchMux);
  const cube::Match state = match;
  taskEXIT_CRITICAL(&matchMux);
  const int64_t now = esp_timer_get_time();
  renderer.clearScreen();
  for (int i = 0; i < 2; ++i) drawSeat(i, state, now);
  renderer.setOrientation(savedOrientation);
  renderer.displayBuffer(firstFrame ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
  firstFrame = false;
}
#endif
