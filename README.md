# Splunkerboard

**A handheld development and prototyping platform for the Seeed Studio XIAO ESP32 family.**

Splunkerboard puts the things most handheld projects need on one PCB: a 0.96" 128×64 OLED, a six-button D-pad, a LiPo connector with a power switch and, on Splunkerboard 1, a microSD slot, an I/O expander, a battery fuel gauge and breakout headers. Solder on a XIAO ESP32 module (C3, C5, C6, S3 …), plug in USB-C, and start writing firmware.

<p align="center">
  <img src="Splunker%201/Render-V1C-transparentbg.png" alt="Splunkerboard 1 (V1C) render" width="640">
</p>

The schematics, PCB layouts, fabrication files and firmware are all open.

*Designed by Papathought, Tallinn, Estonia (muru.global).*

---

## Contents

| Section | What's in it |
|---|---|
| [Hardware overview](docs/hardware/README.md) | Board variants, what's different between them, I²C map |
| [Splunkerboard 1 (V1C)](docs/hardware/splunkerboard-1.md) | Full variant: MCP23017, MAX17048, microSD, 4-layer PCB |
| [Splunkerboard Dev / DIY (V1B)](docs/hardware/splunkerboard-dev.md) | Compact 2-layer variant: buttons wired straight to the XIAO |
| [Expansion board](docs/hardware/expansion-board.md) | Prototyping add-on board with power rails and pass-through headers |
| [Manufacturing & assembly](docs/hardware/manufacturing.md) | Gerbers, BOM, assembly order, 3D models and case |
| [Firmware overview](docs/firmware/README.md) | Toolchain setup and which firmware runs on which board |
| [Firmware development guide](docs/firmware/development-guide.md) | Pin maps and working code for buttons, OLED, SD and fuel gauge |
| [Sentinel cold wallet](docs/firmware/sentinel-wallet.md) | Air-gapped multi-coin hardware wallet firmware |
| [Gamecard](docs/firmware/gamecard.md) | Retro game console with ESP-NOW multiplayer |
| [WarPet (Wormagotchi)](docs/firmware/warpet.md) | Wardriving virtual pet (passive Wi-Fi/BLE survey) |

---

## Board variants at a glance

| | **Splunkerboard Dev (V1B)** | **Splunkerboard 1 (V1C)** |
|---|---|---|
| Folder | [`Splunkerboard DIY/`](Splunkerboard%20DIY) | [`Splunker 1/`](Splunker%201) |
| PCB | 2-layer, 1.6 mm FR-4 | 4-layer, 1.6 mm FR-4 |
| Size | 47.1 × 79.4 mm | 47.1 × 79.4 mm |
| MCU | XIAO ESP32-C3 / C5 / C6 / S3 (dual footprint: C3 and C5 pad layouts) | same |
| Display | 0.96" SSD1306 OLED **module** (4-pin, I²C `0x3C`) | 0.96" SSD1306 OLED **bare panel** (30-pin FPC, I²C `0x3C`) |
| Buttons | 6, wired directly to XIAO GPIOs | 6, on an **MCP23017** I/O expander (I²C `0x20`) with interrupt |
| Storage | — | **microSD** (SPI) with card-detect |
| Battery gauge | — | **MAX17048** fuel gauge (I²C `0x36`) |
| Breakouts | 2.0 mm pads: D6, D7, D10, power, I²C | 2.54 mm headers: 16 expander I/O, D1/D3/D6/D7, I²C, power |
| Antenna | Optional u.FL → edge-SMA footprint | Uses the XIAO module's own antenna |
| Firmwares in this repo | All three run as-is | Need a port (see [compatibility](docs/firmware/README.md#board-compatibility)) |

## Firmware at a glance

| Firmware | Folder | Target | Summary |
|---|---|---|---|
| **Sentinel** (cold wallet) | [`Firmwares/Crypto Wallet`](Firmwares/Crypto%20Wallet) | XIAO ESP32-C5 | BIP39 seed, PIN-encrypted storage, offline signing for BTC, ETH and EVM chains, SOL, XLM, ADA, XRP, ATOM, TRX, TON |
| **Gamecard** | [`Firmwares/Gamecard/gamecard`](Firmwares/Gamecard/gamecard) | XIAO ESP32-C3 | 12 games, including 2–4 player Pong, Tron, Battleship and Uno over ESP-NOW |
| **WarPet** | [`Firmwares/Wormagotchi`](Firmwares/Wormagotchi) | XIAO ESP32-C5 | Virtual pet that levels up from passive dual-band Wi-Fi + BLE discoveries |

> **Naming:** the KiCad files use the project's working name **"Spunkleboard"**, and early Dev gerbers are named **"ESP32C5 Cold Wallet"**. These all refer to Splunkerboard.

## Tools used

| Tool | Used for |
|---|---|
| [KiCad](https://www.kicad.org/) 10 | Schematics, PCB layout, gerbers |
| [Blender](https://www.blender.org/) | Renders, 3D mock-ups, enclosure |
| Arduino IDE / arduino-cli (esp32 core 3.x) | Firmware |
| [Lopaka](https://lopaka.app/) | OLED UI mock-ups (WarPet) |
