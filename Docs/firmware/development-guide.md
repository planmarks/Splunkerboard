# Firmware development guide

Everything you need to write your own firmware for Splunkerboard: pin maps, and minimal working code for each peripheral.

← [Firmware overview](README.md) · [Main README](../../README.md)

- [Pin map cheat-sheet](#pin-map-cheat-sheet)
- [Display (SSD1306)](#display-ssd1306)
- [Dev board buttons (direct GPIO)](#dev-board-buttons-direct-gpio)
- [Splunkerboard 1 buttons via MCP23017](#splunkerboard-1-buttons-via-mcp23017)
- [Wake from light sleep on a key press](#wake-from-light-sleep-on-a-key-press)
- [microSD card](#microsd-card)
- [Battery: MAX17048 fuel gauge](#battery-max17048-fuel-gauge)
- [I²C scanner](#i2c-scanner)
- [Portable board abstraction](#portable-board-abstraction)

---

## Pin map cheat-sheet

Use the Arduino `Dx` macros. They resolve to the right GPIO for whichever XIAO module you select.

| XIAO pin | Splunkerboard Dev (V1B) | Splunkerboard 1 (V1C) |
|---|---|---|
| D0 | Button **UP** | MCP23017 **INTA** (input) |
| D1 | Button **DOWN** | microSD card-detect / header |
| D2 | Button **OK** | microSD **CS** |
| D3 | Button **BACK** | free (header) |
| D4 | I²C **SDA** | I²C **SDA** |
| D5 | I²C **SCL** | I²C **SCL** |
| D6 | free (UART TX) | free (UART TX, header) |
| D7 | free (UART RX) | free (UART RX, header) |
| D8 | Button **LEFT** | microSD **SCK** |
| D9 | Button **RIGHT** | microSD **MISO** |
| D10 | free | microSD **MOSI** |

| I²C device | Address | Board |
|---|---|---|
| SSD1306 OLED | `0x3C` | both |
| MCP23017 | `0x20` | V1C |
| MAX17048 | `0x36` | V1C |

---

## Display (SSD1306)

The display is a 128 × 64 SSD1306 on the default `Wire` bus (D4/D5). The Gamecard and WarPet firmwares rotate it 180° (`setRotation(2)`) because the OLED is mounted upside-down relative to the button layout. Pick the rotation that matches your board.

**Adafruit SSD1306**

```cpp
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

Adafruit_SSD1306 oled(128, 64, &Wire, -1);

void setup() {
  Wire.begin();                                   // SDA = D4, SCL = D5
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {  // internal charge pump (required on V1C panel)
    for (;;) delay(1000);
  }
  oled.setRotation(2);
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.print("Hello, Splunkerboard!");
  oled.display();
}
void loop() {}
```

**U8g2**

```cpp
#include <Wire.h>
#include <U8g2lib.h>

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R2, U8X8_PIN_NONE);   // R2 = 180°

void setup() {
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 12, "Hello, Splunkerboard!");
  u8g2.sendBuffer();
}
void loop() {}
```

---

## Dev board buttons (direct GPIO)

Each button connects its pin to GND. Use internal pull-ups, so a pressed button reads `LOW`.

```cpp
enum { B_UP, B_DOWN, B_OK, B_BACK, B_LEFT, B_RIGHT, B_COUNT };
const uint8_t BTN_PIN[B_COUNT] = { D0, D1, D2, D3, D8, D9 };

void setup() {
  Serial.begin(115200);
  for (uint8_t i = 0; i < B_COUNT; i++) pinMode(BTN_PIN[i], INPUT_PULLUP);
}

uint8_t readButtons() {               // bit i set = button i held
  uint8_t m = 0;
  for (uint8_t i = 0; i < B_COUNT; i++)
    if (digitalRead(BTN_PIN[i]) == LOW) m |= 1 << i;
  return m;
}

void loop() {
  static uint8_t prev = 0;
  uint8_t now = readButtons();
  uint8_t pressed = now & ~prev;      // rising edges
  if (pressed) Serial.printf("pressed mask: 0x%02X\n", pressed);
  prev = now;
  delay(10);                          // crude debounce
}
```

> ⚠️ On an ESP32-C3, D0, D8 and D9 are strapping pins. Don't hold UP, LEFT or RIGHT during reset.

---

## Splunkerboard 1: buttons via MCP23017

The buttons are on **GPA0–GPA5** (Up, Down, OK, Back, Left, Right), active-low. INTA is wired to **D0**. The driver below talks to the chip's registers directly with `Wire`, so it needs no library. It reads all six buttons in one 2-byte I²C transaction.

```cpp
#include <Wire.h>

namespace mcp {
  const uint8_t ADDR     = 0x20;
  // Register addresses (IOCON.BANK = 0, the power-on default)
  const uint8_t IODIRA   = 0x00, IODIRB  = 0x01;
  const uint8_t GPINTENA = 0x04, INTCONA = 0x08;
  const uint8_t IOCON    = 0x0A;
  const uint8_t GPPUA    = 0x0C, GPPUB   = 0x0D;
  const uint8_t INTCAPA  = 0x10, GPIOA   = 0x12, GPIOB = 0x13;
  const uint8_t OLATB    = 0x15;

  void write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ADDR); Wire.write(reg); Wire.write(val); Wire.endTransmission();
  }
  uint8_t read(uint8_t reg) {
    Wire.beginTransmission(ADDR); Wire.write(reg); Wire.endTransmission(false);
    Wire.requestFrom(ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
  }
}

enum { B_UP, B_DOWN, B_OK, B_BACK, B_LEFT, B_RIGHT };   // = GPA0..GPA5
const uint8_t BUTTON_MASK = 0x3F;

volatile bool g_btnIrq = false;
void IRAM_ATTR onBtnIrq() { g_btnIrq = true; }

void buttonsBegin() {
  Wire.begin();                                  // D4/D5
  mcp::write(mcp::IOCON,    0b01000000);         // MIRROR=1 (INTA also covers port B), push-pull, active-low
  mcp::write(mcp::IODIRA,   0xFF);               // port A all inputs
  mcp::write(mcp::GPPUA,    BUTTON_MASK);        // 100k pull-ups on the button pins
  mcp::write(mcp::INTCONA,  0x00);               // interrupt on any change
  mcp::write(mcp::GPINTENA, BUTTON_MASK);        // enable interrupts for the buttons
  mcp::read(mcp::GPIOA);                         // clear any pending interrupt

  pinMode(D0, INPUT);                            // INTA (push-pull, no pull needed)
  attachInterrupt(digitalPinToInterrupt(D0), onBtnIrq, FALLING);
}

uint8_t readButtons() {                          // bit i set = button i held
  return (~mcp::read(mcp::GPIOA)) & BUTTON_MASK; // also clears INTA
}

void setup() {
  Serial.begin(115200);
  buttonsBegin();
}

void loop() {
  static uint8_t prev = 0;
  if (g_btnIrq) {                                // only touch I²C when something changed
    g_btnIrq = false;
    delay(5);                                    // let the contacts settle (debounce)
    uint8_t now = readButtons();
    uint8_t pressed = now & ~prev;
    if (pressed & (1 << B_OK)) Serial.println("OK pressed");
    prev = now;
  }
}
```

**Polling instead of interrupts.** If you call `readButtons()` every frame, as a game loop would, you can skip the interrupt setup entirely. A 400 kHz I²C read takes about 60 µs.

**Using the spare expander pins** (GPA6, GPA7, GPB0–GPB7 on the headers):

```cpp
mcp::write(mcp::IODIRB, 0x00);        // port B = outputs
mcp::write(mcp::OLATB,  0b00000001);  // GPB0 high
uint8_t b = mcp::read(mcp::GPIOB);    // read back port B
```

If you'd rather use a library, **Adafruit MCP23017 Arduino Library** (`Adafruit_MCP23X17`) works too: `mcp.begin_I2C(0x20)`, `mcp.pinMode(0..5, INPUT_PULLUP)`, `mcp.readGPIOA()`.

---

## Wake from light sleep on a key press

On **V1C**, the MCP23017 pulls INTA (D0) low when a button changes. On the **Dev** board, every button already pulls its own pin low. Both cases work as a GPIO wake source:

```cpp
#include "esp_sleep.h"
#include "driver/gpio.h"

void sleepUntilButton() {
  // V1C: clear pending interrupt so INTA is high before sleeping
  // readButtons();
  gpio_wakeup_enable((gpio_num_t)D0, GPIO_INTR_LOW_LEVEL);  // Dev: D0 = UP; V1C: D0 = INTA
  esp_sleep_enable_gpio_wakeup();
  esp_light_sleep_start();
  gpio_wakeup_disable((gpio_num_t)D0);
}
```

> The Sentinel firmware notes that on the ESP32-C5, going from light sleep straight into heavy crypto/NVS work crashed, so it uses a polling idle instead. Test sleep paths on your target module.

---

## microSD card

V1C only. The slot runs in SPI mode on **SCK = D8, MISO = D9, MOSI = D10, CS = D2**, with card-detect on **D1**. All lines have 4.7 kΩ pull-ups. Format the card as **FAT32**.

```cpp
#include <SPI.h>
#include <SD.h>

const int SD_CS = D2, SD_CD = D1;

void setup() {
  Serial.begin(115200);
  pinMode(SD_CD, INPUT);                       // external 4.7k pull-up on board
  Serial.printf("card-detect level: %d\n", digitalRead(SD_CD));  // check polarity on your unit

  SPI.begin(D8, D9, D10, SD_CS);               // SCK, MISO, MOSI, SS
  if (!SD.begin(SD_CS, SPI, 20000000)) {       // 20 MHz; drop to 4 MHz if unreliable
    Serial.println("SD mount failed");
    return;
  }
  Serial.printf("SD: %llu MB\n", SD.cardSize() / (1024ULL * 1024ULL));

  File f = SD.open("/log.txt", FILE_APPEND);
  f.println("Hello from Splunkerboard 1");
  f.close();
}
void loop() {}
```

---

## Battery: MAX17048 fuel gauge

V1C only. The gauge sits at I²C `0x36`. It reports cell voltage and state of charge with no calibration needed. Registers are 16-bit, big-endian.

| Register | Address | Conversion |
|---|---|---|
| VCELL | `0x02` | `raw × 78.125 µV` |
| SOC | `0x04` | `raw / 256` → % |
| VERSION | `0x08` | chip version |
| CRATE | `0x16` | `(int16) raw × 0.208 %/h` |

```cpp
#include <Wire.h>

uint16_t max17048Read(uint8_t reg) {
  Wire.beginTransmission(0x36); Wire.write(reg); Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)0x36, (uint8_t)2);
  uint16_t hi = Wire.read(), lo = Wire.read();
  return (hi << 8) | lo;
}

float batteryVolts()   { return max17048Read(0x02) * 78.125e-6f; }
float batteryPercent() { return max17048Read(0x04) / 256.0f; }

void setup() {
  Serial.begin(115200);
  Wire.begin();
}
void loop() {
  Serial.printf("Battery: %.2f V, %.1f %%\n", batteryVolts(), batteryPercent());
  delay(2000);
}
```

Library alternative: **Adafruit MAX1704X** (`Adafruit_MAX17048`, `cellVoltage()`, `cellPercent()`).

> The gauge is powered from the battery side of the power switch, so it keeps tracking while the board is off. Right after you connect a battery for the first time, the % reading settles over a few minutes.

---

## I²C scanner

A quick bring-up test. Expect `0x20 0x36 0x3C` on V1C and `0x3C` on the Dev board.

```cpp
#include <Wire.h>
void setup() {
  Serial.begin(115200);
  delay(500);
  Wire.begin();
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) Serial.printf("found 0x%02X\n", a);
  }
}
void loop() {}
```

---

## Portable board abstraction

To make one firmware run on both boards, put the button read behind a compile-time switch:

```cpp
// board.h
#pragma once
// #define BOARD_SPLUNKERBOARD_1    // uncomment for V1C (MCP23017 buttons)

#ifdef BOARD_SPLUNKERBOARD_1
  #include "mcp_buttons.h"          // buttonsBegin()/readButtons() from above
  #define HAS_SD       1
  #define HAS_FUELGAUGE 1
#else
  #include "gpio_buttons.h"         // direct-GPIO version from above
  #define HAS_SD       0
  #define HAS_FUELGAUGE 0
#endif
// Both versions return the same bitmask: bit0 UP, 1 DOWN, 2 OK, 3 BACK, 4 LEFT, 5 RIGHT
```

Keep the bit order **Up, Down, OK, Back, Left, Right** everywhere. It matches the MCP23017 GPA0–GPA5 order, so the V1C read needs no remapping.
