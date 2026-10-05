#include "net.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>

static uint8_t selfMac[6];
static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static RxPkt queue[RX_QUEUE];
static volatile uint8_t qHead = 0;
static volatile uint8_t qTail = 0;
static volatile uint16_t sRawCount = 0;

static bool macEq(const uint8_t* a, const uint8_t* b) {
  return memcmp(a, b, 6) == 0;
}

static void enqueue(const uint8_t* mac, const uint8_t* data, int len) {
  if (len < 1 || len > PKT_MAX) return;
  sRawCount++;
  uint8_t h = qHead;
  uint8_t t = qTail;
  // SNAPSHOT type is byte 3 (magic2 + ver + type). Keep only the latest.
  if (len > 3 && data[3] == 7) {
    uint16_t nseq = (len >= 11) ? (uint16_t)(data[9] | ((uint16_t)data[10] << 8)) : 0;
    for (uint8_t i = t; i != h; i = (uint8_t)((i + 1) % RX_QUEUE)) {
      if (queue[i].len > 3 && queue[i].data[3] == 7) {
        uint16_t oseq = (queue[i].len >= 11)
                            ? (uint16_t)(queue[i].data[9] | ((uint16_t)queue[i].data[10] << 8))
                            : 0;
        if ((int16_t)(nseq - oseq) < 0) return;
        memcpy(queue[i].mac, mac, 6);
        queue[i].len = (uint8_t)len;
        memcpy(queue[i].data, data, len);
        return;
      }
    }
  }
  uint8_t n = (uint8_t)((h + 1) % RX_QUEUE);
  if (n == t) return; // drop
  memcpy(queue[h].mac, mac, 6);
  queue[h].len = (uint8_t)len;
  memcpy(queue[h].data, data, len);
  qHead = n;
}

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
static void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (!info || !data || !info->src_addr) return;
  enqueue(info->src_addr, data, len);
}
#else
static void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
  if (!mac || !data) return;
  enqueue(mac, data, len);
}
#endif

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
static void onSent(const esp_now_send_info_t* info, esp_now_send_status_t status) {
  (void)info;
  (void)status;
}
#else
static void onSent(const uint8_t* mac, esp_now_send_status_t status) {
  (void)mac;
  (void)status;
}
#endif

void netLockChannel() {
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
}

void netEnsurePeer(const uint8_t mac[6]) {
  if (!mac || esp_now_is_peer_exist(mac)) return;
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, mac, 6);
  p.channel = 0; // follow current WiFi channel
  p.encrypt = false;
  p.ifidx = WIFI_IF_STA;
  esp_now_add_peer(&p);
}

void netTick() {
  static uint32_t last;
  uint32_t now = millis();
  if (now - last < 4000) return;
  last = now;
  esp_wifi_set_ps(WIFI_PS_NONE);
}

uint16_t netRawCount() { return sRawCount; }

bool netBegin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  // wifioff must stay false — ESP-NOW needs the radio on.
  WiFi.disconnect(false, false, 100);
  delay(40);

  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  netLockChannel();
  esp_wifi_set_max_tx_power(84);

  if (esp_read_mac(selfMac, ESP_MAC_WIFI_STA) != ESP_OK) {
    WiFi.macAddress(selfMac);
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println(F("esp_now_init failed"));
    return false;
  }
  esp_now_register_recv_cb(onRecv);
  esp_now_register_send_cb(onSent);

  netLockChannel();
  esp_wifi_set_ps(WIFI_PS_NONE);
  netEnsurePeer(BCAST);

  Serial.printf("MAC %02X:%02X:%02X:%02X:%02X:%02X  ch=1  esp-now ok\n",
                selfMac[0], selfMac[1], selfMac[2], selfMac[3], selfMac[4], selfMac[5]);
  return true;
}

void netGetMac(uint8_t out[6]) { memcpy(out, selfMac, 6); }

bool netSend(const uint8_t mac[6], const uint8_t* data, uint8_t len) {
  if (!data || len == 0 || !mac) return false;
  netEnsurePeer(mac);
  return esp_now_send(mac, data, len) == ESP_OK;
}

bool netBroadcast(const uint8_t* data, uint8_t len) {
  return netSend(BCAST, data, len);
}

bool netPop(RxPkt& out) {
  if (qTail == qHead) return false;
  uint8_t t = qTail;
  out = queue[t];
  qTail = (uint8_t)((t + 1) % RX_QUEUE);
  return true;
}

void netFlush() {
  qTail = qHead;
}
