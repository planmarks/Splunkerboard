#include "display.h"
#include "mirror.h"
#include <Wire.h>
#include <Arduino.h>

static uint8_t fb[FB_BYTES];
static uint8_t oledAddr = OLED_ADDR_PRIMARY;
static bool oledReady = false;

// 5x7 font, column-major, LSB = top. Covers 0x20-0x5A plus a few extras.
static const uint8_t FONT5[][5] = {
  {0x00,0x00,0x00,0x00,0x00}, // space
  {0x00,0x00,0x5F,0x00,0x00}, // !
  {0x00,0x07,0x00,0x07,0x00}, // "
  {0x14,0x7F,0x14,0x7F,0x14}, // #
  {0x24,0x2A,0x7F,0x2A,0x12}, // $
  {0x23,0x13,0x08,0x64,0x62}, // %
  {0x36,0x49,0x55,0x22,0x50}, // &
  {0x00,0x05,0x03,0x00,0x00}, // '
  {0x00,0x1C,0x22,0x41,0x00}, // (
  {0x00,0x41,0x22,0x1C,0x00}, // )
  {0x14,0x08,0x3E,0x08,0x14}, // *
  {0x08,0x08,0x3E,0x08,0x08}, // +
  {0x00,0x50,0x30,0x00,0x00}, // ,
  {0x08,0x08,0x08,0x08,0x08}, // -
  {0x00,0x60,0x60,0x00,0x00}, // .
  {0x20,0x10,0x08,0x04,0x02}, // /
  {0x3E,0x51,0x49,0x45,0x3E}, // 0
  {0x00,0x42,0x7F,0x40,0x00}, // 1
  {0x42,0x61,0x51,0x49,0x46}, // 2
  {0x21,0x41,0x45,0x4B,0x31}, // 3
  {0x18,0x14,0x12,0x7F,0x10}, // 4
  {0x27,0x45,0x45,0x45,0x39}, // 5
  {0x3C,0x4A,0x49,0x49,0x30}, // 6
  {0x01,0x71,0x09,0x05,0x03}, // 7
  {0x36,0x49,0x49,0x49,0x36}, // 8
  {0x06,0x49,0x49,0x29,0x1E}, // 9
  {0x00,0x36,0x36,0x00,0x00}, // :
  {0x00,0x56,0x36,0x00,0x00}, // ;
  {0x08,0x14,0x22,0x41,0x00}, // <
  {0x14,0x14,0x14,0x14,0x14}, // =
  {0x00,0x41,0x22,0x14,0x08}, // >
  {0x02,0x01,0x51,0x09,0x06}, // ?
  {0x32,0x49,0x79,0x41,0x3E}, // @
  {0x7E,0x11,0x11,0x11,0x7E}, // A
  {0x7F,0x49,0x49,0x49,0x36}, // B
  {0x3E,0x41,0x41,0x41,0x22}, // C
  {0x7F,0x41,0x41,0x22,0x1C}, // D
  {0x7F,0x49,0x49,0x49,0x41}, // E
  {0x7F,0x09,0x09,0x09,0x01}, // F
  {0x3E,0x41,0x49,0x49,0x7A}, // G
  {0x7F,0x08,0x08,0x08,0x7F}, // H
  {0x00,0x41,0x7F,0x41,0x00}, // I
  {0x20,0x40,0x41,0x3F,0x01}, // J
  {0x7F,0x08,0x14,0x22,0x41}, // K
  {0x7F,0x40,0x40,0x40,0x40}, // L
  {0x7F,0x02,0x0C,0x02,0x7F}, // M
  {0x7F,0x04,0x08,0x10,0x7F}, // N
  {0x3E,0x41,0x41,0x41,0x3E}, // O
  {0x7F,0x09,0x09,0x09,0x06}, // P
  {0x3E,0x41,0x51,0x21,0x5E}, // Q
  {0x7F,0x09,0x19,0x29,0x46}, // R
  {0x46,0x49,0x49,0x49,0x31}, // S
  {0x01,0x01,0x7F,0x01,0x01}, // T
  {0x3F,0x40,0x40,0x40,0x3F}, // U
  {0x1F,0x20,0x40,0x20,0x1F}, // V
  {0x3F,0x40,0x38,0x40,0x3F}, // W
  {0x63,0x14,0x08,0x14,0x63}, // X
  {0x07,0x08,0x70,0x08,0x07}, // Y
  {0x61,0x51,0x49,0x45,0x43}, // Z
};

static void cmd(uint8_t c) {
  Wire.beginTransmission(oledAddr);
  Wire.write(0x00);
  Wire.write(c);
  Wire.endTransmission();
}

static void cmd2(uint8_t c, uint8_t v) {
  Wire.beginTransmission(oledAddr);
  Wire.write(0x00);
  Wire.write(c);
  Wire.write(v);
  Wire.endTransmission();
}

static void cmds(const uint8_t* c, int n) {
  Wire.beginTransmission(oledAddr);
  Wire.write(0x00);
  Wire.write(c, n);
  Wire.endTransmission();
}

static bool probe(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void displayBegin() {
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  Wire.setTimeOut(8);
  delay(50);
  oledReady = false;
  if (probe(OLED_ADDR_PRIMARY)) oledAddr = OLED_ADDR_PRIMARY;
  else if (probe(OLED_ADDR_SECONDARY)) oledAddr = OLED_ADDR_SECONDARY;
  else {
    Serial.println(F("OLED not found on 0x3C/0x3D"));
    return;
  }

  cmd(0xAE);
  cmd2(0xD5, 0x80);
  cmd2(0xA8, 0x3F);
  cmd2(0xD3, 0x00);
  cmd(0x40);
  cmd2(0x8D, 0x14);
  cmd2(0x20, 0x00);
#if OLED_ROTATE_180
  cmd(0xA0);  // segment remap
  cmd(0xC0);  // COM scan increment  → 180° vs A1/C8
#else
  cmd(0xA1);
  cmd(0xC8);
#endif
  cmd2(0xDA, 0x12);
  cmd2(0x81, 0xCF);
  cmd2(0xD9, 0xF1);
  cmd2(0xDB, 0x40);
  cmd(0xA4);
  cmd(0xA6);
  cmd(0xAF);
  displayClear();
  displayPresent();
  oledReady = true;
}

bool displayOk() { return oledReady; }
uint8_t displayAddr() { return oledAddr; }
uint8_t* displayBuffer() { return fb; }

void displayClear() {
  memset(fb, 0, sizeof(fb));
}

void displaySet(int x, int y, bool on) {
  if ((unsigned)x >= OLED_W || (unsigned)y >= OLED_H) return;
  uint8_t* p = &fb[x + (y >> 3) * OLED_W];
  uint8_t m = (uint8_t)(1 << (y & 7));
  if (on) *p |= m;
  else *p &= (uint8_t)~m;
}

bool displayGet(int x, int y) {
  if ((unsigned)x >= OLED_W || (unsigned)y >= OLED_H) return false;
  return (fb[x + (y >> 3) * OLED_W] & (1 << (y & 7))) != 0;
}

void displayHLine(int x, int y, int w, bool on) {
  for (int i = 0; i < w; i++) displaySet(x + i, y, on);
}

void displayVLine(int x, int y, int h, bool on) {
  for (int i = 0; i < h; i++) displaySet(x, y + i, on);
}

void displayFillRect(int x, int y, int w, int h, bool on) {
  int x1 = wm_clamp((int16_t)x, 0, OLED_W);
  int y1 = wm_clamp((int16_t)y, 0, OLED_H);
  int x2 = wm_clamp((int16_t)(x + w), 0, OLED_W);
  int y2 = wm_clamp((int16_t)(y + h), 0, OLED_H);
  for (int yy = y1; yy < y2; yy++) {
    for (int xx = x1; xx < x2; xx++) displaySet(xx, yy, on);
  }
}

void displayLine(int x0, int y0, int x1, int y1, bool on) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    displaySet(x0, y0, on);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void displayCircle(int cx, int cy, int r, bool on, bool fill) {
  if (r < 0) return;
  for (int dy = -r; dy <= r; dy++) {
    for (int dx = -r; dx <= r; dx++) {
      int d = dx * dx + dy * dy;
      if (fill) {
        if (d <= r * r) displaySet(cx + dx, cy + dy, on);
      } else if (d >= (r - 1) * (r - 1) && d <= r * r) {
        displaySet(cx + dx, cy + dy, on);
      }
    }
  }
}

void displayInvertRect(int x, int y, int w, int h) {
  int x1 = wm_clamp((int16_t)x, 0, OLED_W);
  int y1 = wm_clamp((int16_t)y, 0, OLED_H);
  int x2 = wm_clamp((int16_t)(x + w), 0, OLED_W);
  int y2 = wm_clamp((int16_t)(y + h), 0, OLED_H);
  for (int yy = y1; yy < y2; yy++) {
    for (int xx = x1; xx < x2; xx++) displaySet(xx, yy, !displayGet(xx, yy));
  }
}

void displayBlitOr(const uint8_t* src) {
  for (int i = 0; i < FB_BYTES; i++) fb[i] |= src[i];
}

static int glyphIndex(char c) {
  if (c >= 'a' && c <= 'z') c = (char)(c - 32);
  if (c < 32 || c > 90) {
    if (c == '_') return '-' - 32;
    return '?' - 32;
  }
  return c - 32;
}

void displayChar(int x, int y, char c, bool on) {
  int gi = glyphIndex(c);
  for (int col = 0; col < 5; col++) {
    uint8_t bits = FONT5[gi][col];
    for (int row = 0; row < 7; row++) {
      if (bits & (1 << row)) displaySet(x + col, y + row, on);
    }
  }
}

int displayTextWidth(const char* s) {
  int n = 0;
  while (*s++) n++;
  return n * 6;
}

void displayText(int x, int y, const char* s, bool on) {
  while (*s) {
    displayChar(x, y, *s++, on);
    x += 6;
  }
}

void displayCenterText(int y, const char* s, bool on) {
  int w = displayTextWidth(s);
  displayText((OLED_W - w) / 2, y, s, on);
}

void displayPresent() {
  if (oledReady) {
    const uint8_t win[] = {0x21, 0x00, 0x7F, 0x22, 0x00, 0x07};
    cmds(win, 6);
    for (int i = 0; i < FB_BYTES; i += 64) {
      Wire.beginTransmission(oledAddr);
      Wire.write(0x40);
      Wire.write(fb + i, 64);
      Wire.endTransmission();
    }
  }
  mirrorPush();
}
