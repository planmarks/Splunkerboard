# Splunkerboard Dev / DIY (V1B)

The compact base variant: a XIAO, an OLED module, six buttons and a battery switch on a 2-layer board. It has few parts and all of them are easy to hand-solder, so it works well as a first soldering kit. All the firmwares in this repository were written for this wiring.

← [Hardware overview](README.md) · [Main README](../../README.md)

<p align="center">
  <img src="../../Splunker%201/Render-V1C-single-transparentbg.png" alt="Splunkerboard Dev render: OLED module, six buttons, SMA antenna connector and breakout headers" width="420">
</p>

| | |
|---|---|
| Source folder | [`Splunkerboard DIY/`](../../Splunkerboard%20DIY) |
| KiCad project | `Spunkleboard.kicad_pro` / `.kicad_sch` / `.kicad_pcb` |
| Gerbers | [`Splunkerboard DIY/Spunkleboard Gerber 2208/`](../../Splunkerboard%20DIY/Spunkleboard%20Gerber%202208) (+ `.zip`), generated 2026-08-22, project name *"ESP32C5 Cold Wallet"* |
| PCB | 2 layers, 1.6 mm FR-4 |
| Outline | 47.07 × 79.39 mm, 2 × Ø2.1 mm mounting holes |
| Silkscreen | "Spunkleboard V1C", "Designed by Papathought", "20.08.26 Tallinn, Estonia", "planmarks.eu / muru.global", *"The shortest worm sees the least sunlight"* |

---

## Features

- XIAO ESP32-C3 / C5 / C6 / S3 (dual C3 + C5 footprint)
- 0.96" SSD1306 OLED **module** (4-pin GND/VCC/SCL/SDA, part `DM-OLED096-636`), I²C `0x3C`
- 6 buttons wired **directly** to XIAO GPIOs (active-low, internal pull-ups)
- Slide power switch and a 2.0 mm LiPo header
- 2.0 mm breakout pads for every button line, the free GPIOs and power
- Optional **u.FL receptacle + edge-mount SMA** footprint for an external antenna

## XIAO pin assignment

| XIAO pin | Net | Function | C3 GPIO | C5 GPIO |
|---|---|---|---|---|
| **D0** | `D0-SW1` | Button **UP** (SW1) | 2 ⚠️ strapping | 1 |
| **D1** | `D1-SW2` | Button **DOWN** (SW2) | 3 | 0 |
| **D2** | `D2-SW3` | Button **OK** (SW3) | 4 | 25 |
| **D3** | `D3-SW4` | Button **BACK** (SW4) | 5 | 7 |
| **D4** | `D4-SDA` | I²C SDA → OLED | 6 | 23 |
| **D5** | `D5-SCL` | I²C SCL → OLED | 7 | 24 |
| **D6** | `D6` | Free (breakout), UART TX | 21 | — |
| **D7** | `D7` | Free (breakout), UART RX | 20 | — |
| **D8** | `D8-SW5` | Button **LEFT** (SW5) | 8 ⚠️ strapping | 8 |
| **D9** | `D9-SW6` | Button **RIGHT** (SW6) | 9 ⚠️ BOOT | 9 |
| **D10** | `D10` | Free (breakout) | 10 | — |

The C3 GPIO numbers follow the Seeed XIAO ESP32-C3 pinout, and the Gamecard sources use the same numbers for the buttons and I²C. The C5 numbers come from the WarPet `config.h`. A dash means no firmware in this repo uses that pin, so check the Seeed pinout. Use the Arduino `Dx` macros in your code so it stays portable between modules.

> ⚠️ **Boot caution (ESP32-C3):** D0, D8 and D9 are strapping pins, and D9 (GPIO9) is the BOOT pin. The buttons pull these pins to GND, so **holding RIGHT (and possibly UP or LEFT) during power-on or reset can put the chip into download mode.** Release all buttons while resetting. Splunkerboard 1 avoids this problem by moving the buttons to the MCP23017.

## Buttons

| Button | Ref | XIAO pin | Test point | 2.0 mm pad pair |
|---|---|---|---|---|
| Up | SW1 | D0 | `UP` | `D0` (signal + GND) |
| Down | SW2 | D1 | `DOWN` | `D1` |
| OK | SW3 | D2 | `OK` | `D2` |
| Back | SW4 | D3 | `BACK` | `D3` |
| Left | SW5 | D8 | `LEFT` | `D8` |
| Right | SW6 | D9 | `RIGHT` | `D9` |

Each button shorts its pin to GND. The board has no external resistors, so enable the internal pull-ups with `pinMode(pin, INPUT_PULLUP)`. Each button line also has a 2-pin 2.0 mm pad (signal + GND), so you can wire an external button or remote switch in parallel.

## Power

```
 LiPo (+) ── BT1 (BAT+) ── SW7 slide switch ── VBAT ──> XIAO BAT pad
 LiPo (−) ── GND
```

- **BT1**: 1×2 **2.0 mm** header, pin 1 = BAT+, pin 2 = GND
- **SW7**: C&K OS102011MA1Q slide switch, between the battery and the XIAO
- The XIAO's charger charges the battery over USB-C (switch ON)
- No fuel gauge. To estimate charge, measure battery voltage with an ADC divider you add yourself, or upgrade to Splunkerboard 1.

## Breakouts (2.0 mm pitch)

| Breakout | Pins |
|---|---|
| Power | GND, 3V3, VBUS (5 V) |
| GPIO | D6, D7, D10 |
| Buttons | D0, D1, D2, D3, D8, D9, each paired with GND |
| Test pads | SDA, SCL, 3V, GND |

## External antenna (optional)

The `IPEX4` footprint (MHF-I / u.FL receptacle) connects to the `SMA` footprint (Amphenol 132289 edge-mount SMA) through a short RF trace. Fit both parts, then run a u.FL-to-u.FL jumper from a XIAO with an antenna connector (e.g. the C5) to the on-board receptacle. You get an external SMA antenna port at the board edge. Leave both parts unpopulated if you don't need them.

## Display

The OLED is a 4-pin module (`U2`, `DM-OLED096-636`) soldered on: GND, VCC (3V3), SCL (D5), SDA (D4). The board has **no I²C pull-ups**. It relies on the pull-ups fitted on the OLED module, which nearly all 0.96" modules have. If you remove the OLED and use the I²C pads for other devices, add 4.7 kΩ pull-ups.

Most modules are `0x3C`, and some are `0x3D`. The firmwares mount the display rotated 180° (`OLED_ROTATION 2` in WarPet).

## Bill of materials

| Ref | Qty | Part | Notes |
|---|---|---|---|
| U1 / U3 | 1 | Seeed XIAO ESP32-C5 **or** C3 (C6/S3 also fit) | Two overlaid footprints. Fit only one module. |
| U2 | 1 | 0.96" SSD1306 I²C OLED module, 4-pin | `DM-OLED096-636` footprint |
| SW1–SW6 | 6 | XKB TS-1187A SMD tactile switch | |
| SW7 | 1 | C&K OS102011MA1Q slide switch | THT, right-angle |
| BT1 | 1 | 1×2 2.0 mm header / JST-PH style battery lead | |
| IPEX4 | 0–1 | u.FL (MHF-I) receptacle | optional |
| SMA | 0–1 | Amphenol 132289 edge-mount SMA | optional |
| — | — | 2.0 mm headers for the breakouts | optional |
| — | 1 | 3.7 V LiPo cell | optional |

## Known issues / review notes

| # | Item | Notes |
|---|---|---|
| 1 | Variant naming | The silkscreen says **"Spunkleboard V1C"**, but the product notes call this board **Splunkerboard Dev (V1B)**, and V1C is Splunkerboard 1. Pick one scheme and update the silkscreen. |
| 2 | Schematic out of sync | The schematic (last saved 2026-08-08) doesn't include the IPEX/SMA antenna parts or the breakout headers, which were added directly in the PCB. Update the schematic before the next revision. |
| 3 | Strapping-pin buttons | On the C3, UP, LEFT and RIGHT sit on strapping pins (see the boot caution above). |
| 4 | Gerber naming | The gerber files are named `ESP32C5 Cold Wallet-*`, a leftover from the project's origin as a cold-wallet board. |
