# Splunkerboard 1

## Short description

A handheld dev board for the Seeed Studio XIAO ESP32 family (C3, C5, C6, S3, S5, S6). It has a 128×64 OLED, a 6-button D-pad, a microSD slot, a LiPo fuel gauge and breakout headers. Plug in a XIAO and a battery and you're ready to write firmware for your next pocket gadget.

---

## Product description

**Stop breadboarding your handheld. Start shipping firmware.**

Every handheld project starts the same way: an OLED on a breadboard, six buttons wired with jumpers, a LiPo taped to the back and an SD module hanging off the side. It works until a wire comes loose. Splunkerboard 1 puts all of that on one tidy 4-layer PCB, so you can spend your evenings on firmware instead of wiring.

**Pick your brain: any XIAO ESP32.** The board takes any Seeed Studio XIAO ESP32 module with the standard XIAO footprint: **C3, C5, C6, S3, S5 or S6**. It has pads for both XIAO battery-pad layouts, so any of them gets battery power. Start on a C3 you already have, move to an S3 when you need more power, or use a **C5** for dual-band 2.4 / 5 GHz Wi-Fi. Your firmware keeps the same button, display and SD wiring on every module, so you only change the target in your build settings.

**The parts a handheld needs are already on the board:**

- **0.96" 128×64 OLED (SSD1306, I²C).** It works with Adafruit GFX, U8g2, LVGL and most other display libraries.
- **Six tactile buttons: Up / Down / Left / Right / OK / Back.** They run through an **MCP23017 I/O expander** with an interrupt line to the MCU. No button uses an ESP32 strapping pin, so the board won't boot into download mode because a button was held. Your XIAO GPIOs stay free, and you can wake from sleep on a keypress.
- **microSD slot (push-push, card-detect).** Use it for logs, assets, save games, captured data or config files.
- **LiPo power with a real fuel gauge.** The **MAX17048** reports battery percentage over I²C, so you can draw an accurate battery icon without guessing from a voltage divider. Charging runs through the XIAO's USB-C, and a slide switch turns the device off.

**Room to grow.** Headers are broken out for everything you might bolt on later:

- **10 spare expander GPIOs** (GPA6–7, GPB0–7), plus the 6 button lines on the same header
- **4 direct XIAO GPIOs** (D1, D3, D6, D7), which covers UART TX/RX for GPS modules, LoRa radios and similar add-ons
- **I²C header (SDA/SCL)** for sensors, IMUs, RTCs, NFC readers and more
- **Power header (5 V / 3.3 V / GND)** to supply your add-ons

Every expansion header uses 2.54 mm pitch, so standard jumper wires, perfboard and your own shield designs fit.

**Made for rapid iteration.** Plug the XIAO in over USB-C, flash from Arduino IDE, ESP-IDF or PlatformIO, and test on real hardware in seconds. The layout is identical on every unit, so firmware you write on your desk runs the same on the board in your pocket and on your friend's.

**Some things people build with it:**

- Pocket game consoles with ESP-NOW multiplayer
- Dual-band Wi-Fi / BLE survey and wardriving tools
- Virtual pets and other "tamagotchi"-style toys
- Offline password managers and air-gapped signing devices
- Remote controls, macro pads and smart-home controllers
- Sensor loggers that write to microSD
- A test bench for any I²C or UART module you want to try

Two M2 mounting holes and a compact 47 × 79 mm outline make it easy to design a 3D-printed enclosure or mount the board on a project plate.

*Designed by Papathought in Tallinn, Estonia.*

---

## Technical specifications

### Core

| | |
|---|---|
| Compatible MCU modules | Seeed Studio XIAO ESP32-C3, C5, C6, S3, S5, S6 (standard XIAO footprint) |
| Mounting | XIAO module is surface-soldered on top of the PCB |
| MCU, wireless, memory, antenna | As specified by Seeed Studio for the chosen XIAO module |
| Battery pads | Pads for both XIAO ESP32 battery-pad layouts on the PCB |
| Programming / charging | USB-C on the XIAO module |
| Toolchains | Arduino IDE (esp32 core 3.x), ESP-IDF, PlatformIO |

### Display

| | |
|---|---|
| Type | 0.96" monochrome OLED |
| Resolution | 128 × 64 px |
| Controller | SSD1306 |
| Interface / address | I²C, `0x3C` |

### Input

| | |
|---|---|
| Buttons | 6 × tactile: Up, Down, Left, Right, OK, Back |
| Interface | MCP23017 16-bit I/O expander (I²C `0x20`), buttons on GPA0–GPA5, active-low |
| Interrupt | MCP23017 INTA → XIAO D0 (interrupt-driven input / wake-on-press) |

### Storage

| | |
|---|---|
| Slot | microSD, push-in / push-out (CUI MSD-11-A) |
| Interface | SPI: CS = D2, SCK = D8, MISO = D9, MOSI = D10 |
| Card detect | D1 |

### Power

| | |
|---|---|
| Battery | Single-cell 3.7 V LiPo (not included) |
| Battery connector | 2-pin, 2.0 mm pitch (+ / −) |
| Charging | Through the XIAO module's built-in LiPo charger over USB-C |
| Fuel gauge | MAX17048: state of charge (%) and cell voltage over I²C (`0x36`) |
| Power switch | Slide switch between the battery and the MCU |
| Logic voltage | 3.3 V |

### I²C bus map

| Device | Address |
|---|---|
| MCP23017 I/O expander | `0x20` |
| MAX17048 fuel gauge | `0x36` |
| SSD1306 OLED | `0x3C` |

Shared bus on D4 (SDA) / D5 (SCL) with onboard 4.7 kΩ pull-ups, broken out to a header.

### Expansion headers (2.54 mm pitch)

| Header | Pins |
|---|---|
| Expander port A | GPA0–GPA5 (buttons), GPA6, GPA7 (8 pins) |
| Expander port B | GPB0–GPB7 (8 pins) |
| MCU GPIO | D1, D3, D6 (TX), D7 (RX) (4 pins) |
| I²C | SCL, SDA (2 pins) |
| Power | 5 V (VBUS), 3.3 V, GND (3 pins) |

Note: D1 is shared with the microSD card-detect line.

### Board

| | |
|---|---|
| Dimensions | 47.1 × 79.4 mm |
| PCB | 4-layer, 1.6 mm FR-4 |
| Mounting | 2 × Ø2.1 mm holes (M2) |
| Passives | 0805 SMD |

### What's in the box

- Splunkerboard 1 PCB
- *(Edit to match your SKU: XIAO module, pre-soldered headers, OLED fitted, battery, enclosure)*
