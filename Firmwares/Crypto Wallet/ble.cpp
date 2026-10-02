#include "ble.h"
#include "config.h"

#include <NimBLEDevice.h>   // NimBLE-Arduino 2.x
#include <vector>

namespace {
bool                  s_initialized = false;  // NimBLE stack brought up (once)
bool                  s_enabled   = false;    // user intent: advertising/connectable
volatile bool         s_connected = false;
NimBLEServer         *s_server    = nullptr;
NimBLECharacteristic *s_tx        = nullptr;

// Per-device identity, derived from the factory eFuse MAC (stable, unique, and
// NOT linked to the seed). Advertised so the app can recognise a Sentinel and
// tell multiple devices apart.
char    s_advName[20] = {0};   // "Sentinel-XXXX"
uint8_t s_devId[6]    = {0};   // low 6 bytes of the eFuse MAC
bool    s_idReady     = false;

// Handoff between the BLE host task and the main loop. The C5 is single-core so
// tasks interleave cooperatively; a volatile flag is sufficient (no heap ops in
// an ISR/critical section). The onWrite guard `!s_psbtReady` prevents the BLE
// task from overwriting a pending line before the main loop consumes it.
String        s_rxBuf;
volatile bool s_psbtReady = false;
String        s_psbtLine;

class ServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *, NimBLEConnInfo &) override { s_connected = true; }
  void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) override {
    s_connected = false;
    // Only re-advertise if BLE is still enabled. When BLE is turned off (toggle
    // or auto-lock) we deliberately disconnect the client and must NOT re-advertise.
    if (s_enabled) NimBLEDevice::startAdvertising();
  }
};

class RxCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &) override {
    NimBLEAttValue v = c->getValue();
    for (size_t i = 0; i < v.length(); i++) {
      char ch = (char)v[i];
      if (ch == '\n' || ch == '\r') {
        if (s_rxBuf.length() > 0 && !s_psbtReady) {
          s_psbtLine  = s_rxBuf;
          s_psbtReady = true;      // publish AFTER the line is fully copied
          s_rxBuf     = "";
        }
      } else if (s_rxBuf.length() < 16384) {
        s_rxBuf += ch;
      }
    }
  }
};

ServerCB s_serverCb;
RxCB     s_rxCb;

// Derive the advertised name + device id from the factory MAC (computed once).
void computeIdentity() {
  if (s_idReady) return;
  uint64_t mac = ESP.getEfuseMac();               // 48-bit factory-unique value
  for (int i = 0; i < 6; i++) s_devId[i] = (uint8_t)(mac >> (8 * i));
  snprintf(s_advName, sizeof(s_advName), "%s-%02X%02X",
           BLE_DEVICE_NAME, s_devId[1], s_devId[0]);
  s_idReady = true;
}
}  // namespace

namespace ble {

void begin() { computeIdentity(); }   // build the device identity (no radio yet)

bool isEnabled()   { return s_enabled; }
bool isConnected() { return s_connected; }
const char *advName() { computeIdentity(); return s_advName; }

void enable(const char *name) {
  if (s_enabled) return;

  // Bring the NimBLE stack up exactly ONCE per boot. Repeated init()/deinit()
  // cycles are a known crash source in NimBLE-Arduino — and deinit() is what was
  // rebooting the device when BLE was turned off. After the first enable the
  // stack stays up and enable/disable only start/stop advertising.
  (void)name;   // the advertised name is derived from the MAC (computeIdentity)
  if (!s_initialized) {
    computeIdentity();
    NimBLEDevice::init(s_advName);              // GAP name = "Sentinel-XXXX"
    NimBLEDevice::setMTU(247);

    s_server = NimBLEDevice::createServer();
    s_server->setCallbacks(&s_serverCb);

    NimBLEService *svc = s_server->createService(BLE_NUS_SERVICE_UUID);
    s_tx = svc->createCharacteristic(BLE_NUS_TX_UUID, NIMBLE_PROPERTY::NOTIFY);
    NimBLECharacteristic *rx = svc->createCharacteristic(
        BLE_NUS_RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    rx->setCallbacks(&s_rxCb);
    svc->start();

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    // Primary advertisement: the per-device name "Sentinel-XXXX" (fits the
    // 31-byte budget with the flags; the 128-bit NUS UUID is intentionally left
    // out and discovered over GATT after connecting).
    adv->setName(s_advName);

    // Scan response: manufacturer-specific data so an app can (a) recognise a
    // genuine Sentinel via the "SNTL" magic and (b) tell devices apart via the
    // 6-byte factory id. Layout: FFFF | 'S''N''T''L' | id[6] | fmt(0x01).
    uint8_t mfg[13];
    mfg[0] = 0xFF; mfg[1] = 0xFF;               // company id 0xFFFF (internal / no company)
    mfg[2] = 'S'; mfg[3] = 'N'; mfg[4] = 'T'; mfg[5] = 'L';
    memcpy(mfg + 6, s_devId, 6);
    mfg[12] = 0x01;                             // identity data format version
    NimBLEAdvertisementData scanData;
    scanData.setManufacturerData(mfg, sizeof(mfg));
    adv->setScanResponseData(scanData);
    adv->enableScanResponse(true);

    s_initialized = true;
  }

  s_enabled = true;              // set before advertising so callbacks observe it
  NimBLEDevice::getAdvertising()->start();
}

void disable() {
  if (!s_enabled) return;

  // Turn BLE "off" WITHOUT tearing down the stack (see enable()): stop
  // advertising and drop any connected client. The controller stays initialized
  // but is no longer discoverable or connectable. This is stable across repeated
  // toggles, unlike NimBLEDevice::deinit(), which was crashing/rebooting the C5.
  s_enabled = false;             // clear first so onDisconnect won't re-advertise
  NimBLEDevice::getAdvertising()->stop();
  if (s_server) {
    std::vector<uint16_t> peers = s_server->getPeerDevices();
    for (uint16_t h : peers) s_server->disconnect(h);
  }
  s_connected = false;
  s_rxBuf = "";
  s_psbtReady = false;
  s_psbtLine = "";
}

bool takePsbt(String &out) {
  if (!s_psbtReady) return false;
  out = s_psbtLine;
  s_psbtLine = "";
  s_psbtReady = false;
  return true;
}

void sendLine(const String &s) {
  if (!s_enabled || !s_tx || !s_connected) return;
  const size_t CHUNK = 20;   // safe for the default BLE MTU
  for (size_t i = 0; i < s.length(); i += CHUNK) {
    String part = s.substring(i, min(i + CHUNK, s.length()));
    s_tx->setValue((const uint8_t *)part.c_str(), part.length());
    s_tx->notify();
    delay(20);
  }
  s_tx->setValue((const uint8_t *)"\n", 1);   // line terminator
  s_tx->notify();
}

}  // namespace ble
