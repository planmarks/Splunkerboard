#pragma once
#include "config.h"

struct RxPkt {
  uint8_t mac[6];
  uint8_t len;
  uint8_t data[PKT_MAX];
};

bool netBegin();
void netGetMac(uint8_t out[6]);
bool netSend(const uint8_t mac[6], const uint8_t* data, uint8_t len);
bool netBroadcast(const uint8_t* data, uint8_t len);
bool netPop(RxPkt& out);
void netFlush();
void netEnsurePeer(const uint8_t mac[6]);
void netLockChannel();
void netTick();
uint16_t netRawCount();
