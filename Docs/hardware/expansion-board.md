# Splunkerboard 1 Expansion (Prototyping) Board

An add-on board with the same outline as Splunkerboard 1. It picks up the main board's 2.54 mm breakout headers and adds a perfboard area with power rails, so you can build add-on circuits (GPS, LoRa, sensors, LEDs, buzzers …) that stack onto the handheld.

← [Hardware overview](README.md) · [Main README](../../README.md)

| | |
|---|---|
| Source folder | [`Splunker 1 Prototyping Board/`](../../Splunker%201%20Prototyping%20Board) |
| PCB | `Spunkleboard.kicad_pcb` (4 layers, 1.6 mm). Silkscreen: **"Spunkleboard 1 Expansion Board"**, "Designed by Papathought", "17.09.26 Tallinn, Estonia" |
| Gerbers | [`Prototyping Board/`](../../Splunker%201%20Prototyping%20Board/Prototyping%20Board) and `Prototyping Board.zip` (generated 2026-09-20) |
| Outline | 47.07 × 79.39 mm (same as the main boards), 2 × M2 holes |

> This folder began as a copy of the main project. Only `Spunkleboard.kicad_pcb` and the `Prototyping Board/` gerbers describe the expansion board. The schematic in this folder is a leftover copy from the main board. The duplicate `Spunkleboard Gerber 2208` set is excluded by `.gitignore`.

## Layout

| Area | Description |
|---|---|
| **Pass-through headers** | Two copies of each Splunkerboard 1 breakout: expander port A (GPA0–GPA7), port B (GPB0–GPB7), MCU GPIO (D1, D3, D6, D7), I²C (SCL/SDA), power (VBUS/3V3/GND). One copy mates with the main board, and the second gives you a solder point for every signal. |
| **Power rails** | Three 11-hole rails: **5V** (VBUS), **3V** (3V3) and **GND** |
| **I²C rails** | A 4-hole **SDA** rail and a 4-hole **SCL** rail for several I²C devices |
| **Perfboard area** | A grid of unconnected 2.54 mm plated holes (11-hole and 8-hole rows) for your own circuit |

## Signals available

| Signal | Source on main board | Notes |
|---|---|---|
| GPA0–GPA5 | MCP23017 | Button lines (read-only use recommended) |
| GPA6, GPA7, GPB0–GPB7 | MCP23017 | 10 free expander I/O, 3.3 V, ~25 mA max per pin |
| D1 | XIAO | Shared with microSD card-detect (pulled up) |
| D3 | XIAO | Free GPIO / ADC |
| D6 / D7 | XIAO | UART TX / RX (GPS, LoRa, etc.) |
| SDA / SCL | XIAO D4/D5 | Shared I²C bus, already pulled up on the main board |
| VBUS | USB | 5 V **only while USB is connected** |
| 3V3 | XIAO LDO | Shared with the main board, so keep add-on current modest |
| GND | | |

## Usage tips

- For battery-powered add-ons that need more than 3.3 V, use VBUS only when USB is connected. Otherwise add your own boost converter.
- Avoid I²C addresses `0x20`, `0x36` and `0x3C` (already used on the main board).
- Read and write the extra expander pins with the same MCP23017 driver you use for the buttons. See the [development guide](../firmware/development-guide.md#splunkerboard-1-buttons-via-mcp23017).

## Known issues

- The port-B header silkscreen reads **`BPN2`** instead of **`GPB2`** (same typo as on the main board).
