#include "mirror.h"

#if WM_MIRROR

#include "display.h"
#include <Arduino.h>
#include <string.h>

static bool sOn = false;
static bool sHaveAvail = false;
static uint16_t sSeq = 0;
static char sLine[16];
static uint8_t sLineN = 0;
static uint8_t sPkt[8 + FB_BYTES + 1];

static uint8_t crc8Feed(uint8_t c, const uint8_t* d, int n) {
  for (int i = 0; i < n; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++) {
      c = (c & 1) ? (uint8_t)((c >> 1) ^ 0x8C) : (uint8_t)(c >> 1);
    }
  }
  return c;
}

bool mirrorActive() { return sOn; }

void mirrorPoll() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      sLine[sLineN] = 0;
      if (sLineN == 4 && sLine[0] == 'W' && sLine[1] == 'M' && sLine[2] == 'O' && sLine[3] == 'N') {
        sOn = true;
        Serial.println(F("MIRROR ON"));
      } else if (sLineN == 5 && sLine[0] == 'W' && sLine[1] == 'M' &&
                 sLine[2] == 'O' && sLine[3] == 'F' && sLine[4] == 'F') {
        sOn = false;
        Serial.println(F("MIRROR OFF"));
      }
      sLineN = 0;
    } else if (sLineN < sizeof(sLine) - 1) {
      sLine[sLineN++] = c;
    } else {
      sLineN = 0;
    }
  }
}

void mirrorPush() {
  if (!sOn) return;
  const uint16_t n = (uint16_t)FB_BYTES;
  const int pktLen = 8 + (int)n + 1;
  int avail = Serial.availableForWrite();
  if (avail > 0) sHaveAvail = true;
  // Queue the whole frame or skip. A small TX buffer used to block here
  // and cut recordings to ~15 fps.
  if (sHaveAvail && avail < pktLen) return;

  uint16_t seq = ++sSeq;
  sPkt[0] = 0xAA;
  sPkt[1] = 0x55;
  sPkt[2] = (uint8_t)'W';
  sPkt[3] = (uint8_t)'F';
  sPkt[4] = (uint8_t)seq;
  sPkt[5] = (uint8_t)(seq >> 8);
  sPkt[6] = (uint8_t)n;
  sPkt[7] = (uint8_t)(n >> 8);
  memcpy(sPkt + 8, displayBuffer(), n);
  uint8_t c = 0xFF;
  c = crc8Feed(c, sPkt + 4, 4 + (int)n);
  sPkt[8 + n] = c;
  Serial.write(sPkt, (size_t)pktLen);
}

#endif
