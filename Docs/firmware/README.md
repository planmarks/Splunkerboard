# Firmware overview

← [Main README](../../README.md) · [Hardware docs](../hardware/README.md)

All Splunkerboard firmwares are **Arduino sketches** for the Espressif **arduino-esp32 core 3.x**. You can also build them with arduino-cli or PlatformIO. ESP-IDF works for custom firmware.

| Firmware | Folder | Main sketch | Target module | Docs |
|---|---|---|---|---|
| **Sentinel** cold wallet (v0.9B) | [`Firmwares/Crypto Wallet/`](../../Firmwares/Crypto%20Wallet) | `coldwallet.ino` | XIAO ESP32-C5 | [sentinel-wallet.md](sentinel-wallet.md) |
| **Gamecard** | [`Firmwares/Gamecard/gamecard/`](../../Firmwares/Gamecard/gamecard) | `gamecard.ino` | XIAO ESP32-C3 | [gamecard.md](gamecard.md) · [in-folder README](../../Firmwares/Gamecard/gamecard/README.md) |
| **WarPet** (Wormagotchi) | [`Firmwares/Wormagotchi/`](../../Firmwares/Wormagotchi) | `xiaoesp32c5_tamagotchi.ino` | XIAO ESP32-C5 | [warpet.md](warpet.md) · [in-folder README](../../Firmwares/Wormagotchi/README.md) |

Want to write your own? See the [firmware development guide](development-guide.md).

---

## Toolchain setup

1. Install the **Arduino IDE 2.x** (or `arduino-cli`).
2. Add the Espressif board index under *File → Preferences → Additional boards manager URLs*:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Under *Boards Manager*, install **esp32 by Espressif Systems**:
   - **≥ 3.1.0** for WarPet (C5, 5 GHz, promiscuous mode)
   - **≥ 3.3.5** for Sentinel
   - **3.x** for Gamecard (it also falls back to the 2.x ESP-NOW API)
4. Select the board: **Tools → Board → esp32 → `XIAO_ESP32C5`** or **`XIAO_ESP32C3`**.
5. Install the libraries listed below with the *Library Manager*.
6. Connect the XIAO over USB-C, select the port, and click **Upload**.

> **Arduino folder rule:** the sketch folder name has to match the main `.ino` file name. If the IDE asks to move the file into a new folder, let it, or rename the folder (for example `Crypto Wallet/` → `coldwallet/`).

### Libraries

| Library | Sentinel | Gamecard | WarPet |
|---|:-:|:-:|:-:|
| U8g2 | ✅ | | |
| Adafruit GFX Library | | ✅ | ✅ |
| Adafruit SSD1306 | | ✅ | ✅ |
| uBitcoin | ✅ | | |
| NimBLE-Arduino (v2.x) | ✅ | | ✅ |
| Bundled in the core (Wire, WiFi, ESP-NOW, Preferences, LittleFS, mbedTLS) | ✅ | ✅ | ✅ |
| Vendored in the sketch folder | qrcodegen, TweetNaCl, BLAKE2b | — | Lopaka sprites |

### Flashing tips

- If upload fails, hold the XIAO's **BOOT** button, tap **RESET**, release BOOT, and upload again.
- On the Dev board, **don't hold RIGHT/UP/LEFT while resetting** a C3. Those buttons sit on strapping pins (see [Dev hardware](../hardware/splunkerboard-dev.md#xiao-pin-assignment)).
- Serial monitor: **115200 baud** for all three firmwares.
- If the OLED stays blank, try address `0x3D`.

## Board compatibility

| Firmware | Splunkerboard Dev (V1B) | Splunkerboard 1 (V1C) |
|---|---|---|
| Sentinel | ✅ Native (C5) | ⚠️ Port needed: buttons |
| Gamecard | ✅ Native (C3). On the C5, change the hard-coded GPIO numbers. | ⚠️ Port needed: buttons |
| WarPet | ✅ Native (C5) | ⚠️ Port needed: buttons. SD logging and the battery gauge would be nice additions. |

All three firmwares read the six buttons **directly from XIAO pins D0, D1, D2, D3, D8 and D9** (the Dev board wiring). On **Splunkerboard 1** those pins carry the microSD bus and the MCP23017 interrupt, and the buttons sit on the **MCP23017 GPA0–GPA5** instead. The display wiring (I²C on D4/D5, `0x3C`) is the same on both boards, so only button input needs to change.

### Porting to Splunkerboard 1

Each firmware has a single input layer, so the port only touches one file:

| Firmware | File to change | What to replace |
|---|---|---|
| Sentinel | `buttons.cpp` (`Buttons::begin`/`poll`) and `config.h` | Replace `digitalRead(pin)` with a read of the MCP23017 `GPIOA` register (bit 0–5). |
| Gamecard | `input.ino` (`pollButtons`) and `gamecard.ino` (`BTN_PINS`, `Wire.begin(6, 7)`) | Read `GPIOA` once per frame and map the bits to `BTN_UP … BTN_B` |
| WarPet | `xiaoesp32c5_tamagotchi.ino` (`Button` struct, `pinMode` calls) and `config.h` | Same idea |

Bit mapping for the port (from the schematic):

| Logical button | MCP23017 bit | Gamecard id | Sentinel id | WarPet |
|---|---|---|---|---|
| Up | GPA0 | `BTN_UP` | `BTN_UP` | `bUp` |
| Down | GPA1 | `BTN_DOWN` | `BTN_DOWN` | `bDown` |
| OK | GPA2 | `BTN_A` | `BTN_SELECT` | `bOk` |
| Back | GPA3 | `BTN_B` | `BTN_BACK` | `bBack` |
| Left | GPA4 | `BTN_LEFT` | `BTN_LEFT` | `bLeft` |
| Right | GPA5 | `BTN_RIGHT` | `BTN_RIGHT` | `bRight` |

The [development guide](development-guide.md#splunkerboard-1-buttons-via-mcp23017) has a drop-in `readButtons()` helper (one I²C read returns all six buttons).

> **Gamecard on the C5:** Gamecard uses raw C3 GPIO numbers (`{2, 3, 8, 9, 4, 5}` and `Wire.begin(6, 7)`). To run it on a C5 Dev board, change them to `{D0, D1, D8, D9, D2, D3}` and `Wire.begin()`.

## Firmware ideas from the project notes

- War Toads, a multiplayer game for up to 4 players
- GhostESP support (planned)
- Biscuit support (planned)
- WarPet + GPS → Wigle uploads (plumbing already in place)
