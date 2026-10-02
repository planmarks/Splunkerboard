#include "display.h"
#include "config.h"
#include <Wire.h>
#include <string.h>
#include "qrcodegen.h"    // vendored ricmoo/QRCode (renamed to avoid a qrcode.h name clash)

// Layout constants for the 128x64 panel with the 6x12 font.
static const uint8_t TITLE_H   = 13;   // height of the inverted title bar
static const uint8_t LINE_H    = 12;   // per-row height in the body
static const uint8_t BODY_TOP  = TITLE_H + LINE_H;  // baseline of first body row
static const uint8_t MENU_ROWS = 4;    // visible menu items at once

void Display::begin() {
  Wire.begin();                          // XIAO default I2C: SDA=D4, SCL=D5
  u8g2_.setI2CAddress(OLED_I2C_ADDR8);
  u8g2_.begin();
  u8g2_.setBusClock(400000);
  u8g2_.enableUTF8Print();
}

void Display::drawTitleBar_(const char *title) {
  u8g2_.setDrawColor(1);
  u8g2_.drawBox(0, 0, OLED_WIDTH, TITLE_H);
  u8g2_.setDrawColor(0);
  u8g2_.setFont(u8g2_font_6x12_tr);
  u8g2_.drawStr(2, TITLE_H - 3, title);
  u8g2_.setDrawColor(1);
}

void Display::splash(const char *title, const char *subtitle) {
  u8g2_.clearBuffer();
  u8g2_.setFont(u8g2_font_ncenB10_tr);
  int tw = u8g2_.getStrWidth(title);
  u8g2_.drawStr((OLED_WIDTH - tw) / 2, 30, title);
  if (subtitle && subtitle[0]) {
    u8g2_.setFont(u8g2_font_6x12_tr);
    int sw = u8g2_.getStrWidth(subtitle);
    u8g2_.drawStr((OLED_WIDTH - sw) / 2, 48, subtitle);
  }
  u8g2_.sendBuffer();
}

void Display::menu(const char *title, const char *const *items, uint8_t count, uint8_t selected) {
  u8g2_.clearBuffer();
  drawTitleBar_(title);

  // Compute a scroll window that keeps `selected` visible.
  uint8_t first = 0;
  if (count > MENU_ROWS) {
    if (selected >= MENU_ROWS - 1) first = selected - (MENU_ROWS - 1);
    if (first > count - MENU_ROWS) first = count - MENU_ROWS;
  }

  u8g2_.setFont(u8g2_font_6x12_tr);
  for (uint8_t row = 0; row < MENU_ROWS && (first + row) < count; row++) {
    uint8_t idx = first + row;
    int y = BODY_TOP + row * LINE_H;
    u8g2_.drawStr(0, y, (idx == selected) ? ">" : " ");
    u8g2_.drawStr(10, y, items[idx]);
  }
  u8g2_.sendBuffer();
}

void Display::message(const char *title, const char *body) {
  u8g2_.clearBuffer();
  drawTitleBar_(title);
  u8g2_.setFont(u8g2_font_6x12_tr);

  int y = BODY_TOP;
  const char *p = body ? body : "";
  char line[32];
  while (p && *p) {
    const char *nl = strchr(p, '\n');
    size_t len = nl ? (size_t)(nl - p) : strlen(p);
    if (len >= sizeof(line)) len = sizeof(line) - 1;
    memcpy(line, p, len);
    line[len] = '\0';
    u8g2_.drawStr(0, y, line);
    y += LINE_H;
    if (y > OLED_HEIGHT) break;
    if (!nl) break;
    p = nl + 1;
  }
  u8g2_.sendBuffer();
}

void Display::qr(const char *text) {
  // IMPORTANT: the QRCode library does NOT bounds-check data vs. version — it
  // overflows an internal buffer if the data is too big for the chosen version
  // (see its own "@TODO: Return error if data is too big"). So we must pick a
  // version whose capacity already fits the data and call the encoder ONCE with
  // that version — never trial small versions that would overflow.
  //
  // Byte-mode capacities (bytes) at ECC level L, versions 1..12.
  static const uint16_t CAP_L[] = {
    0, 17, 32, 53, 78, 106, 134, 154, 192, 230, 271, 321, 367
  };
  const uint8_t MAX_VER = 12;
  static uint8_t buf[640];                 // >= qrcode_getBufferSize(12) = 529

  size_t dlen = strlen(text);
  uint8_t ver = 0;
  for (uint8_t v = 1; v <= MAX_VER; v++) {
    if (CAP_L[v] >= dlen) { ver = v; break; }
  }

  u8g2_.clearBuffer();
  if (ver == 0) {                          // data too large for MAX_VER
    drawTitleBar_("QR too big");
    u8g2_.sendBuffer();
    return;
  }

  QRCode q;
  if (qrcode_initText(&q, buf, ver, ECC_LOW, text) != 0) {
    drawTitleBar_("QR error");
    u8g2_.sendBuffer();
    return;
  }

  const uint8_t quiet = 2;
  uint16_t total = q.size + 2 * quiet;
  int scale = min((int)(OLED_WIDTH / total), (int)(OLED_HEIGHT / total));
  if (scale < 1) scale = 1;
  uint16_t dim = total * scale;
  int ox = (OLED_WIDTH - dim) / 2;
  int oy = (OLED_HEIGHT - dim) / 2;

  // Lit background (including the quiet zone), dark modules punched out.
  u8g2_.setDrawColor(1);
  u8g2_.drawBox(ox, oy, dim, dim);
  u8g2_.setDrawColor(0);
  for (uint8_t y = 0; y < q.size; y++) {
    for (uint8_t x = 0; x < q.size; x++) {
      if (qrcode_getModule(&q, x, y)) {
        u8g2_.drawBox(ox + (quiet + x) * scale, oy + (quiet + y) * scale, scale, scale);
      }
    }
  }
  u8g2_.setDrawColor(1);
  u8g2_.sendBuffer();
}
