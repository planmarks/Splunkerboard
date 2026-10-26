# Splunkerboard 1 (V1C)

The full prototyping variant. Compared with the Dev board, it adds a microSD slot, an MCP23017 I/O expander for the buttons, a MAX17048 LiPo fuel gauge and 2.54 mm breakout headers. It also moves to a 4-layer PCB.

← [Hardware overview](README.md) · [Main README](../../README.md)

| | |
|---|---|
| Source folder | [`Splunker 1/`](../../Splunker%201) |
| KiCad project | `Spunkleboard.kicad_pro` / `.kicad_sch` / `.kicad_pcb` |
| Schematic title block | *Splunkerboard*, rev **V1**, 2026-09-22, muru.global, "by Papathought" |
| Gerbers | [`Splunker 1/Splunkerboard V1 Gerber/`](../../Splunker%201/Splunkerboard%20V1%20Gerber) (generated 2026-09-21) |
| PCB | 4 layers (F.Cu, In1.Cu, In2.Cu, B.Cu), 1.6 mm FR-4 |
| Outline | 47.07 × 79.39 mm, 2 × Ø2.1 mm mounting holes |
| Silkscreen | "17.09.26 Tallinn, Estonia", "muru.global", *"The shortest worm sees the least sunlight"* |

---

## Features

- XIAO ESP32-C3 / C5 / C6 / S3 (dual C3 + C5 footprint)
- 0.96" SSD1306 128 × 64 OLED panel on I²C (`0x3C`), driven from its internal charge pump
- 6 buttons on an **MCP23017** (I²C `0x20`) with **INTA → D0** for interrupt-driven input and wake-on-press
- **microSD** (CUI MSD-11-A, push-push) on SPI with card-detect
- **MAX17048** fuel gauge (I²C `0x36`): battery % and cell voltage
- Slide power switch and a 2.0 mm LiPo header
- 2.54 mm headers: expander port A (8), port B (8), D1/D3/D6/D7 (4), I²C (2), power 5 V/3.3 V/GND (3)

## XIAO pin assignment

| XIAO pin | Net | Function | On-board pull-up |
|---|---|---|---|
| **D0** | `D0-INTA` | MCP23017 **INTA** (button interrupt) | — |
| **D1** | `D1` | microSD **card-detect**, also on the GPIO header | 4.7 kΩ (R11) |
| **D2** | `D2-SDCS` | microSD **CS** | 4.7 kΩ (R10) |
| **D3** | `D3` | Free, GPIO header | — |
| **D4** | `D4-SDA` | I²C **SDA** | 4.7 kΩ (R3) |
| **D5** | `D5-SCL` | I²C **SCL** | 4.7 kΩ (R2) |
| **D6** | `D6` | Free, GPIO header (UART TX) | — |
| **D7** | `D7` | Free, GPIO header (UART RX) | — |
| **D8** | `D8-SDCLK` | microSD **SCK** | 4.7 kΩ (R5) |
| **D9** | `D9-SDSO` | microSD **MISO** (card DO) | 4.7 kΩ (R7) |
| **D10** | `D10-SDSI` | microSD **MOSI** (card DI) | 4.7 kΩ (R6) |
| 3V3 | `+3V3` | Logic supply for all on-board parts | |
| 5V | `VBUS` | USB 5 V, on the power header only | |
| BAT | `PWR` | Battery input, through the slide switch | |

> The buttons are **not** on XIAO pins. D0, D8 and D9 are ESP32-C3 strapping pins, and on this board nothing holds them low during reset, so the board can't boot into download mode by accident.

## Buttons: MCP23017

| Button | Ref | Expander pin | Net | Test point |
|---|---|---|---|---|
| Up | SW2 | **GPA0** | `SW2-GPA0` | TP2 `SW-UP` |
| Down | SW3 | **GPA1** | `SW3-GPA1` | TP3 `SW-DWN` |
| OK | SW4 | **GPA2** | `SW4-GPA2` | TP4 `SW-OK` |
| Back | SW5 | **GPA3** | `SW5-GPA3` | TP5 `SW-BCK` |
| Left | SW6 | **GPA4** | `SW6-GPA4` | TP6 `SW-LFT` |
| Right | SW7 | **GPA5** | `SW7-GPA5` | TP7 `SW-RHT` |

- Each button shorts its line to **GND**. Enable the MCP23017's internal 100 kΩ pull-ups (`GPPUA`), so a pressed button reads **0**.
- **A0/A1/A2 are tied to GND**, so the address is `0x20`.
- **RESET** is pulled to 3V3 through 10 kΩ (R1).
- **INTA → D0**. **INTB is not connected.** To get interrupts from port B pins, set `IOCON.MIRROR = 1` so both ports drive INTA.
- Decoupling: 0.1 µF (C1, C8) and 10 µF (C7) on 3V3.
- Package: QFN-28 6 × 6 mm (`MCP23017-E/ML`). Solder it with a hot plate or hot air.

Firmware examples are in the [development guide](../firmware/development-guide.md#splunkerboard-1-buttons-via-mcp23017).

## microSD (J26: CUI MSD-11-A)

| Card pin | Signal | Net |
|---|---|---|
| P1 (DAT2) | — | pulled up, 4.7 kΩ (R9) |
| P2 (CD/DAT3) | **CS** | `D2-SDCS` |
| P3 (CMD) | **MOSI** | `D10-SDSI` |
| P4 (VDD) | 3.3 V | `+3V3` |
| P5 (CLK) | **SCK** | `D8-SDCLK` |
| P6 (VSS) | GND | |
| P7 (DAT0) | **MISO** | `D9-SDSO` |
| P8 (DAT1) | — | pulled up, 4.7 kΩ (R8) |
| Detect switch | **Card detect** | `D1` (4.7 kΩ pull-up) |
| WP | not connected | |

The slot runs in **SPI mode**. All lines, including unused DAT1 and DAT2, have pull-ups, as the SD spec recommends.

## Power & battery monitoring

```
 LiPo (+) ── BT1 ──┬───────────── VBAT ──> MAX17048 CELL + VDD   (C9 0.1 µF)
                   │
                   └── SW1 (slide) ── PWR ──> XIAO BAT pad ──> XIAO charger / 3V3 LDO
 LiPo (−) ── GND
```

| Item | Details |
|---|---|
| Battery connector | BT1, 1×2 **2.0 mm** pitch header: pin 1 = VBAT (+), pin 2 = GND |
| Switch | SW1 SPDT slide (OS102011MA1Q). Common = `PWR` (XIAO BAT), one throw = `VBAT`, the other is unconnected. |
| Charging | Through the XIAO module's USB-C charger. **The charger can only reach the battery when the switch is ON.** |
| Fuel gauge | U3 **MAX17048G+T10** (TDFN-8 2 × 2 mm), CELL and VDD on `VBAT`, SDA/SCL on the shared bus, address `0x36` |
| Gauge ALRT | Not connected (poll over I²C) |

> The MAX17048 sits **before** the power switch, so it stays powered and keeps learning the cell's state of charge while the board is off. Its quiescent draw is in the tens of µA (lower in hibernate). Disconnect the battery for long-term storage.

## Display (DS1)

- Bare 0.96" 128 × 64 SSD1306 panel (`OLED-128O064D`, 30-pin FPC) in **I²C mode**, address `0x3C`
- Charge-pump capacitors C2 and C3 (2.2 µF), VCOMH capacitor C4 (1 µF), VCC decoupling C5 (0.1 µF) and C6 (2 µF), IREF resistor R4 (390 kΩ)
- Your firmware must enable the internal charge pump. Adafruit SSD1306 does this with `SSD1306_SWITCHCAPVCC`, and U8g2's SSD1306 constructors do it by default.

## Expansion headers (2.54 mm)

| Header | Pins (in order) |
|---|---|
| Expander port A | GPA0, GPA1, GPA2, GPA3, GPA4, GPA5, GPA6, GPA7 (GPA0–5 are the buttons) |
| Expander port B | GPB0 … GPB7 |
| MCU GPIO | D1, D3, D6, D7 |
| I²C | SCL, SDA |
| Power | VBUS (5 V), 3V3, GND |

Free I/O: **10 expander pins** (GPA6, GPA7, GPB0–7) and **4 XIAO pins** (D1 is shared with card-detect). The [expansion board](expansion-board.md) plugs onto these headers.

## Bill of materials

| Ref | Qty | Value / part | Footprint |
|---|---|---|---|
| U1 | 1 | Microchip **MCP23017-E/ML** | QFN-28 6×6 mm (0.65 mm pitch) |
| U2 | 1 | Seeed **XIAO ESP32** (C3/C5/C6/S3) | XIAO SMD (C5 + C3 overlay) |
| U3 | 1 | Analog Devices **MAX17048G+T10** | TDFN-8 2×2 mm |
| DS1 | 1 | 0.96" SSD1306 OLED panel, 128×64, 30-pin | `OLED-128O064D` |
| J26 | 1 | CUI **MSD-11-A** microSD, push-push | CUI_MSD-11-A |
| SW1 | 1 | C&K **OS102011MA1Q** slide switch | THT right-angle |
| SW2–SW7 | 6 | XKB **TS-1187A** tactile switch | SMD 5.1×5.1 mm |
| BT1 | 1 | 1×2 pin header, 2.0 mm | PinHeader_1x02_P2.00mm |
| R1 | 1 | 10 kΩ | 0805 |
| R2, R3, R5–R11 | 9 | 4.7 kΩ | 0805 |
| R4 | 1 | 390 kΩ | 0805 |
| C1, C5, C8, C9 | 4 | 0.1 µF | 0805 |
| C2, C3 | 2 | 2.2 µF | 0805 |
| C4 | 1 | 1 µF | 0805 |
| C6 | 1 | 2 µF (2.2 µF works) | 0805 |
| C7 | 1 | 10 µF | 0805 |
| — | 5 | 2.54 mm pin headers: 1×8 (×2), 1×4, 1×3, 1×2 | THT |
| — | 1 | 3.7 V single-cell LiPo with 2.0 mm (PH-style) connector | — |

## Known issues / review notes

| # | Item | Notes |
|---|---|---|
| 1 | **No firmware support yet** | All three firmwares in `Firmwares/` read buttons straight from XIAO GPIOs (Dev wiring). On V1C those pins are the SD bus and INTA, so the buttons won't respond. See [porting](../firmware/README.md#porting-to-splunkerboard-1). |
| 2 | Silkscreen typo | The port-B header label reads **`BPN2`**. It should read **`GPB2`**. The same typo is on the expansion board. |
| 3 | Duplicate reference designators | The PCB has two XIAO footprints whose references collide with other parts: **U1** (MCP23017 and XIAO-C5) and **U3** (MAX17048 and XIAO-C3). Rename them (e.g. `U2A`/`U2B`) before generating BOM or pick-and-place files. |
| 4 | MAX17048 **CTG** pin | Left unconnected. The MAX17048 datasheet says to connect CTG to GND. Check this before the next revision. |
| 5 | D1 shared | D1 is both microSD card-detect (with a 4.7 kΩ pull-up) and a header pin. Don't drive it from an add-on if you use card-detect. |
| 6 | Charging with switch off | The charger reaches the battery through SW1, so a battery doesn't charge while the switch is OFF. Document this for users. |
