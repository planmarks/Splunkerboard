# Human Description

Splunkerboard is a handheld rapid development and prototyping platform based on the XIAO ESP32 series. It is made for both entry-level hobbyists and professional developers, and accommodates any module with the standard Seeed Studio XIAO footprint (e.g. ESP32-C3 / C5 / C6 / S3). Splunkerboard can be used as a learning project for those who want to practice soldering with an iron or a hot plate. Assembled, it's a pocketable target for on-the-go firmware development and testing of handheld projects that need a display and buttons. Splunkerboard 1 adds a built-in microSD connector for debugging and data storage, an MCP23017 I/O expander that frees the XIAO's pins and keeps the buttons off the ESP32 strapping pins, a MAX17048 LiPo fuel gauge for accurate battery-percentage reporting, and a custom expansion board for additional module/component integration. Splunkerboard is an open-source project with public hardware schematics and fabrication files, and firmware open for assessment and modification.

## Options

1. Dev Module Bare PCB
2. Dev DIY Kit
3. Pre-Assembled Splunkerboard Dev (V1B)
4. Splunkerboard 1 Bare PCB
5. Splunkerboard 1 DIY Kit
6. Pre-Assembled Splunkerboard 1 (V1C)

## Hardware Specifications

### Splunkerboard Dev (V1B)
The compact base variant — XIAO, display and buttons, nothing else to get in the way.

- XIAO ESP32-C3 / C5 / C6 / S3 (by Seeed Studio; standard XIAO footprint)
- 0.96" OLED display module (128 × 64 px, SSD1306, I²C)
- 6 push buttons (Up / Down / Left / Right / OK / Back)
- On/off slide switch
- (optional) single-cell 3.7 V LiPo battery

### Splunkerboard 1 (V1C)
The full prototyping variant — adds storage, an I/O expander, a fuel gauge and a breakout header.

- XIAO ESP32-C3 / C5 / C6 / S3 (by Seeed Studio; standard XIAO footprint; C5 adds dual-band 2.4 / 5 GHz Wi-Fi)
- 0.96" OLED display **panel** (128 × 64 px, SSD1306, I²C `0x3C`)
- **MicroSD** card slot (push-push with card-detect, CUI MSD-11-A) over SPI
  (CS = D2, SCK = D8, MISO = D9, MOSI = D10; card-detect = D1)
- **MCP23017 GPIO expander** (QFN, I²C `0x20`) — the 6 buttons run on GPA0–GPA5 (active-low), with INTA → XIAO D0 for interrupt-driven input / wake-on-press
- **MAX17048 LiPo fuel gauge** (I²C `0x36`) — reports state of charge (%) and cell voltage
- 6 push buttons (Up / Down / Left / Right / OK / Back)
- On/off slide switch
- (optional) single-cell 3.7 V LiPo battery, charged through the XIAO's USB-C
- Splunkerboard prototyping expansion board + 2.54 mm breakout headers
  (expander ports A/B, spare XIAO GPIO D1/D3/D6/D7, I²C, and 5 V / 3.3 V / GND power)
- 4-layer, 1.6 mm FR-4 PCB; 2 × M2 mounting holes

Shared I²C bus on D4 (SDA) / D5 (SCL) with 4.7 kΩ pull-ups, broken out to a header.
On-board I²C devices: MCP23017 `0x20`, MAX17048 `0x36`, SSD1306 OLED `0x3C`.

#### Firmware Specifications
Splunkerboard ships with multiple firmwares plus all the specifications needed for custom firmware development (Arduino IDE / ESP-IDF / PlatformIO).

- Wardriving Tamagotchi (Wi-Fi & Bluetooth recon)
- War Toads multiplayer game (up to 4 players)
- Retro game console (Snake, Pong, etc.)
- (possibly soon) GhostESP support
- (possibly soon) Biscuit support
