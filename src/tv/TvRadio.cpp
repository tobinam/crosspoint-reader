#if XTEINK_TV_REMOTE
#include "TvRadio.h"

#include <Logging.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <cstring>
#include <memory>

#include "TvFraming.h"

namespace tv {
namespace {

constexpr char SERVICE[] = "a7c40001-3f1e-4b8e-9d52-6e0b2c8f4a10";
constexpr char RX[] = "a7c40002-3f1e-4b8e-9d52-6e0b2c8f4a10";
constexpr char TX[] = "a7c40003-3f1e-4b8e-9d52-6e0b2c8f4a10";
constexpr char REMOTE_NAME[] = "X4 Remote";
constexpr char KEYBOARD_NAME[] = "X4 Keyboard";
constexpr uint16_t WANT_MTU = 247;
constexpr size_t INBOX = 6;  // messages waiting for the main loop; more: the oldest is dropped

// HID: keyboard (report 1: modifiers, reserved, six keys) and consumer control (report 2: one 16-bit usage).
const uint8_t REPORT_MAP[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,  // Generic Desktop, Keyboard, Collection, Report ID 1
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,  // modifiers
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,                                                              // reserved
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,  // keys
    0xC0,                                                                                            //
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02,  // Consumer, Consumer Control, Collection, Report ID 2
    0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00,  // one usage
    0xC0,
};

struct Usage {
  uint8_t key;        // keyboard page, 0: none
  uint16_t consumer;  // consumer page, 0: none
};

Usage usageFor(const Op op) {
  switch (op) {
    case Op::Up: return {0x52, 0};
    case Op::Down: return {0x51, 0};
    case Op::Left: return {0x50, 0};
    case Op::Right: return {0x4F, 0};
    case Op::Ok: return {0x28, 0};
    case Op::Return: return {0, 0x0224};  // AC Back
    case Op::Home: return {0, 0x0223};    // AC Home
    case Op::Toggle: return {0, 0x00CD};  // Play/Pause
    case Op::Stop: return {0, 0x00B7};
    case Op::Rew: return {0, 0x00B4};
    case Op::Fwd: return {0, 0x00B3};
    case Op::Next: return {0, 0x00B5};  // Scan Next Track
    case Op::Prev: return {0, 0x00B6};  // Scan Previous Track
    case Op::VolUp: return {0, 0x00E9};
    case Op::VolDown: return {0, 0x00EA};
    case Op::Mute: return {0, 0x00E2};
    case Op::On:
    case Op::Off: return {0, 0x0030};  // Power (whether the TV wakes on it is up to the TV)
  }
  return {0, 0};
}

struct State {
  SemaphoreHandle_t lock = nullptr;
  Mode mode = Mode::Remote;
  bool running = false;
  std::atomic<bool> connected{false};
  std::atomic<bool> linkChanged{false};
  std::atomic<uint16_t> conn{BLE_HS_CONN_HANDLE_NONE};
  std::atomic<uint16_t> mtu{23};
  NimBLECharacteristic* tx = nullptr;
  NimBLECharacteristic* keys = nullptr;
  NimBLECharacteristic* consumer = nullptr;
  std::unique_ptr<NimBLEHIDDevice> hid;
  // Remote mode admits only bridges paired here (LE Secure Connections, bonded): their identity addresses, kept by
  // the activity in /.crosspoint/tv_bridges. A bond from keyboard mode (the TV) is not one of them.
  char bridges[MAX_BRIDGES][18]{};
  std::atomic<uint8_t> bridgeCount{0};
  std::atomic<uint32_t> pairUntil{0};       // millis() until which a new bridge may pair; 0: closed
  std::atomic<bool> trusted{false};         // the link is encrypted and bonded with a known bridge
  std::atomic<uint32_t> connectedAt{0};
  std::atomic<bool> bridgesChanged{false};  // a bridge paired: the activity saves the list
  Joiner<TO_X4_MAX> joiner;  // used on NimBLE's task only
  Chunker chunker;           // main loop only
  char inbox[INBOX][TO_X4_MAX + 1]{};
  size_t head = 0, count = 0;
} *S = nullptr;

struct Guard {
  Guard() { xSemaphoreTake(S->lock, portMAX_DELAY); }
  ~Guard() { xSemaphoreGive(S->lock); }
};

std::string idOf(const NimBLEConnInfo& info) {
  std::string a = info.getIdAddress().toString();
  for (auto& c : a) c = static_cast<char>(tolower(c));
  return a;
}

bool knownBridge(const std::string& id) {
  for (uint8_t i = 0; i < S->bridgeCount; ++i)
    if (id == S->bridges[i]) return true;
  return false;
}

bool pairingOpen() {
  const uint32_t until = S->pairUntil;
  return until && static_cast<int32_t>(until - millis()) > 0;
}

class ServerEvents : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
    // Remote mode: a known bridge (it must still encrypt with its bond), or anyone while pairing is open.
    // The identity address of a stranger using a resolvable address is unknown until pairing: only the window helps.
    if (S->mode == Mode::Remote && !knownBridge(idOf(info)) && !pairingOpen()) {
      LOG_INF("TV", "refused %s (not a paired bridge)", idOf(info).c_str());
      server->disconnect(info);
      return;
    }
    S->conn = info.getConnHandle();
    S->mtu = info.getMTU();
    S->trusted = false;
    {
      Guard g;
      S->joiner.reset();
      S->count = 0;
    }
    S->connectedAt = millis();
    S->connected = true;
    S->linkChanged = true;
    LOG_INF("TV", "connected %s", idOf(info).c_str());
    if (S->mode == Mode::Remote) NimBLEDevice::startSecurity(info.getConnHandle());  // encrypt or pair now
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo& info, int reason) override {
    if (info.getConnHandle() != S->conn) return;
    S->conn = BLE_HS_CONN_HANDLE_NONE;
    S->connected = false;
    S->trusted = false;
    S->linkChanged = true;
    LOG_INF("TV", "disconnected (%d)", reason);  // advertising restarts by itself (advertiseOnDisconnect)
  }
  void onMTUChange(uint16_t mtu, NimBLEConnInfo& info) override {
    if (info.getConnHandle() == S->conn) S->mtu = mtu;
  }
  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    if (info.getConnHandle() != S->conn || S->mode != Mode::Remote) return;
    const std::string id = idOf(info);
    if (!info.isEncrypted() || !info.isBonded()) {
      LOG_INF("TV", "%s did not pair/encrypt: dropped", id.c_str());
      NimBLEDevice::getServer()->disconnect(info);
      return;
    }
    if (!knownBridge(id)) {
      if (!pairingOpen() || S->bridgeCount >= MAX_BRIDGES) {
        LOG_INF("TV", "%s bonded outside the pairing window: dropped", id.c_str());
        NimBLEDevice::deleteBond(info.getIdAddress());
        NimBLEDevice::getServer()->disconnect(info);
        return;
      }
      std::strncpy(S->bridges[S->bridgeCount], id.c_str(), sizeof(S->bridges[0]) - 1);
      ++S->bridgeCount;
      S->pairUntil = 0;  // one bridge per window
      S->bridgesChanged = true;
      LOG_INF("TV", "paired bridge %s", id.c_str());
    }
    S->trusted = true;
    S->linkChanged = true;
  }
};

class RxEvents : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
    if (info.getConnHandle() != S->conn || !S->trusted) return;
    const auto value = c->getValue();
    Guard g;
    if (!S->joiner.push(value.data(), value.size())) return;
    if (S->count == INBOX) {  // the main loop is behind: drop the oldest
      S->head = (S->head + 1) % INBOX;
      --S->count;
    }
    auto& slot = S->inbox[(S->head + S->count) % INBOX];
    std::memcpy(slot, S->joiner.message(), S->joiner.length() + 1);
    ++S->count;
  }
};

ServerEvents serverEvents;
RxEvents rxEvents;

// A static random address of its own for keyboard mode, the same on every start (the TV's bond keeps it). Derived
// from the eFuse Bluetooth MAC, not NimBLEDevice::getAddress(): that returns the address of the current type, and the
// type outlives deinit, so a second start derived a new address (11-12: a second "X4 Keyboard" on the TV).
void keyboardAddress() {
  uint8_t mac[6], a[6];
  esp_read_mac(mac, ESP_MAC_BT);
  for (int i = 0; i < 6; ++i) a[i] = mac[5 - i];  // NimBLE: least significant byte first
  a[0] ^= 0x5A;
  a[5] |= 0xC0;  // static random: the two top bits of the most significant byte set
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
  NimBLEDevice::setOwnAddr(a);
}

bool startRemote(NimBLEServer* server) {
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC);  // the address bridges know, also after keyboard mode
  // Bonded LE Secure Connections, Just Works (tix has no screen): the link is encrypted by the radio; who may pair
  // is limited by the pairing window the user opens.
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  auto* service = server->createService(SERVICE);
  auto* rx = service->createCharacteristic(
      RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC);
  rx->setCallbacks(&rxEvents);
  S->tx = service->createCharacteristic(TX, NIMBLE_PROPERTY::NOTIFY);
  server->start();
  auto* adv = NimBLEDevice::getAdvertising();
  adv->setName(REMOTE_NAME);
  adv->addServiceUUID(SERVICE);
  adv->enableScanResponse(true);
  return adv->start();
}

bool startKeyboard(NimBLEServer* server) {
  keyboardAddress();
  NimBLEDevice::setSecurityAuth(true, false, true);  // bonded, Just Works, Secure Connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  S->hid = std::make_unique<NimBLEHIDDevice>(server);
  S->hid->setManufacturer("CrossPoint");
  S->hid->setPnp(0x02, 0x303A, 0x4004, 0x0100);
  S->hid->setHidInfo(0x00, 0x01);
  S->hid->setReportMap(const_cast<uint8_t*>(REPORT_MAP), sizeof(REPORT_MAP));
  S->keys = S->hid->getInputReport(1);
  S->consumer = S->hid->getInputReport(2);
  S->hid->setBatteryLevel(100);
  server->start();
  auto* adv = NimBLEDevice::getAdvertising();
  adv->setName(KEYBOARD_NAME);
  adv->setAppearance(0x03C1);  // HID keyboard
  adv->addServiceUUID(S->hid->getHidService()->getUUID());
  adv->enableScanResponse(true);
  return adv->start();
}

}  // namespace

const char* opName(const Op op) {
  static const char* const NAMES[] = {"ON", "OFF", "HOME", "RETURN", "TOGGLE", "STOP", "REW", "FWD",
                                      "VOL+", "VOL-", "MUTE", "UP", "DOWN", "LEFT", "RIGHT", "OK", "NEXT", "PREV"};
  return NAMES[static_cast<int>(op)];
}

Radio& radio() {
  static Radio r;
  return r;
}

bool Radio::start(const Mode mode) {
  if (!S) {
    S = new (std::nothrow) State();
    if (!S) return false;
    S->lock = xSemaphoreCreateMutex();
  }
  if (S->running) stop();
  S->mode = mode;
  S->connected = false;
  S->conn = BLE_HS_CONN_HANDLE_NONE;
  S->chunker.reset();
  if (NimBLEDevice::isInitialized()) NimBLEDevice::deinit(true);  // a page turner that did not finish its stop
  if (!NimBLEDevice::init(mode == Mode::Remote ? REMOTE_NAME : KEYBOARD_NAME)) {
    LOG_ERR("TV", "NimBLE init failed");
    return false;
  }
  NimBLEDevice::setMTU(WANT_MTU);
  auto* server = NimBLEDevice::createServer();
  server->setCallbacks(&serverEvents, false);
  server->advertiseOnDisconnect(true);
  const bool ok = mode == Mode::Remote ? startRemote(server) : startKeyboard(server);
  S->running = true;
  if (!ok) {
    LOG_ERR("TV", "advertising failed");
    stop();
    return false;
  }
  LOG_INF("TV", "radio up: %s, %s", mode == Mode::Remote ? REMOTE_NAME : KEYBOARD_NAME,
          NimBLEDevice::getAddress().toString().c_str());
  return true;
}

void Radio::stop() {
  if (!S || !S->running) return;
  // Close the link and let it finish BEFORE deinit (as BleKeyboardHost::end): a deinit while the disconnect is
  // still under way crashed NimBLE's host task (InstrFetchProhibited at host_task, 11-12, switching modes).
  auto* server = NimBLEDevice::getServer();
  if (server) server->advertiseOnDisconnect(false);
  NimBLEDevice::getAdvertising()->stop();
  const uint32_t start = millis();
  while (S->connected && millis() - start < 3000) {
    const uint16_t c = S->conn;
    if (server && c != BLE_HS_CONN_HANDLE_NONE) server->disconnect(c);
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  if (S->connected) LOG_ERR("TV", "link did not close in 3 s");
  vTaskDelay(pdMS_TO_TICKS(100));  // the host's own disconnect bookkeeping
  S->tx = S->keys = S->consumer = nullptr;
  S->hid.reset();
  NimBLEDevice::deinit(true);
  S->running = false;
  S->connected = false;
  S->conn = BLE_HS_CONN_HANDLE_NONE;
  LOG_INF("TV", "radio off");
}

bool Radio::running() const { return S && S->running; }
Mode Radio::mode() const { return S ? S->mode : Mode::Remote; }
bool Radio::connected() const {
  return S && S->connected && (S->mode == Mode::Keyboard || S->trusted);  // remote: only a paired bridge counts
}

void Radio::setBridges(const char* const* ids, const size_t count) {
  if (!S) return;
  const size_t n = count < MAX_BRIDGES ? count : MAX_BRIDGES;
  for (size_t i = 0; i < n; ++i) {
    std::strncpy(S->bridges[i], ids[i], sizeof(S->bridges[0]) - 1);
    S->bridges[i][sizeof(S->bridges[0]) - 1] = 0;
  }
  S->bridgeCount = static_cast<uint8_t>(n);
}

size_t Radio::bridges(char (*out)[18], const size_t max) const {
  if (!S) return 0;
  const size_t count = S->bridgeCount;
  const size_t n = count < max ? count : max;
  for (size_t i = 0; i < n; ++i) std::memcpy(out[i], S->bridges[i], 18);
  return n;
}

void Radio::tick() {
  // Remote mode: a link that is not encrypted with a paired bridge within SECURE_MS is dropped (a stranger, or a
  // faked bridge address without its key, would otherwise hold the only connection).
  constexpr uint32_t SECURE_MS = 10000;
  if (!S || !S->running || S->mode != Mode::Remote || !S->connected || S->trusted) return;
  if (millis() - S->connectedAt < SECURE_MS) return;
  const uint16_t c = S->conn;
  LOG_INF("TV", "link not secured in %u ms: dropped", static_cast<unsigned>(SECURE_MS));
  if (c != BLE_HS_CONN_HANDLE_NONE) NimBLEDevice::getServer()->disconnect(c);
  S->connectedAt = millis();  // once per SECURE_MS until the disconnect lands
}

bool Radio::takeBridgesChanged() { return S && S->bridgesChanged.exchange(false); }

void Radio::openPairing(const uint32_t ms) {
  if (S) S->pairUntil = (millis() + ms) | 1;
}

uint32_t Radio::pairingLeftMs() const {
  if (!S || !pairingOpen()) return 0;
  return S->pairUntil - millis();
}

void Radio::forgetBridges() {
  if (!S) return;
  for (uint8_t i = 0; i < S->bridgeCount; ++i) NimBLEDevice::deleteBond(NimBLEAddress(S->bridges[i], BLE_ADDR_PUBLIC));
  S->bridgeCount = 0;
  S->bridgesChanged = true;
  const uint16_t c = S->conn;
  if (S->mode == Mode::Remote && c != BLE_HS_CONN_HANDLE_NONE) NimBLEDevice::getServer()->disconnect(c);
}
bool Radio::takeLinkChanged() { return S && S->linkChanged.exchange(false); }

bool Radio::takeMessage(char* out, const size_t outSize) {
  if (!S || !S->running || !outSize) return false;
  Guard g;
  if (!S->count) return false;
  std::strncpy(out, S->inbox[S->head], outSize - 1);
  out[outSize - 1] = 0;
  S->head = (S->head + 1) % INBOX;
  --S->count;
  return true;
}

bool Radio::send(const char* json, const size_t length) {
  if (!S || !S->running || S->mode != Mode::Remote || !S->trusted || !S->tx || length > FROM_X4_MAX) return false;
  const uint16_t conn = S->conn;
  const size_t room = S->mtu > 3 ? S->mtu - 3 : 20;
  return S->chunker.split(json, length, room, [conn](const uint8_t* data, size_t size) {
    for (int attempt = 0; attempt < 5; ++attempt) {  // NimBLE out of buffers: give it a moment
      if (S->tx->notify(data, size, conn)) return true;
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
  });
}

bool Radio::tap(const Op op) {
  if (!S || !S->running || S->mode != Mode::Keyboard || !S->connected) return false;
  const Usage u = usageFor(op);
  bool ok = true;
  if (u.key && S->keys) {
    uint8_t down[8] = {0, 0, u.key, 0, 0, 0, 0, 0};
    const uint8_t up[8] = {};
    ok = S->keys->notify(down, sizeof(down));
    vTaskDelay(pdMS_TO_TICKS(15));
    ok = S->keys->notify(up, sizeof(up)) && ok;
  } else if (u.consumer && S->consumer) {
    uint8_t down[2] = {static_cast<uint8_t>(u.consumer & 0xFF), static_cast<uint8_t>(u.consumer >> 8)};
    const uint8_t up[2] = {};
    ok = S->consumer->notify(down, sizeof(down));
    vTaskDelay(pdMS_TO_TICKS(15));
    ok = S->consumer->notify(up, sizeof(up)) && ok;
  }
  return ok;
}

}  // namespace tv

#endif
