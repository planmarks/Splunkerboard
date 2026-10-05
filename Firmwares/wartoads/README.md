# WORMs MICRO

Tiny 2–4 player wireless artillery game for identical **Seeed XIAO ESP32C3** handhelds (0.96" SSD1306 OLED, 6 buttons, LiPo). One firmware binary on every board. No router, phone, or internet.

Last worm alive wins.

The sketch folder has to be named `wartoads`, same as `wartoads.ino`. Clone or download this repo so the folder on disk is `wartoads`.

## Hardware

PCB mapping (active LOW, internal pull-up):

| Function | XIAO pin | GPIO | Notes |
|---|---|---|---|
| OLED SDA | D4 | 6 | I2C |
| OLED SCL | D5 | 7 | I2C |
| UP (aim) | D0 | 2 | |
| DOWN (aim) | D1 | 3 | |
| OK (start/fire/ready) | D2 | 4 | |
| BACK (weapon/pause) | D3 | 5 | Long-press pause |
| LEFT | D8 | 8 | Strapping pin; used after boot |
| RIGHT | D9 | 9 | Also module BOOT — don't hold RIGHT while resetting |

- **OLED:** SSD1306 128×64, I2C `0x3C` (fallback `0x3D`), rotated 180°
- **Sound:** none
- **Battery gauge:** none (no confirmed ADC divider)

### Controls

| Button | Gameplay | Menus / system |
|---|---|---|
| UP / DOWN | Aim up/down on the facing side | Weapon list; lobby: map vs time |
| LEFT / RIGHT | Walk. Turns the worm immediately. **Both** = zoom | Lobby: change map/time (host) |
| OK | Charge shot; release to fire. Hold with **TORCH** to dig | START / READY / unpause / pick weapon |
| BACK | **Tap** = jump. **Hold ~0.4s** = weapon menu. **Hold ~1.1s** = pause | Title: button test. Lobby: exit |

Title / lobby:

- Short **OK** → start (find players). In lobby: ready / unready
- Hold **OK** ~1.2s on title → 2-worm **practice**
- **BACK** on title (tap) → button test. In lobby / finding players: **exit to title**
- **L/R** walk and face; **U/D** aim up/down on that side
- In lobby, **host** uses **U/D** to pick MAP or TIME, **L/R** to change: **CAVERN** / **ISLAND** / **ISLES**, and **10 / 15 / 20 / 30s** turns
- Hold **BACK ~1.1s** in play → pause. **U/D** pick **RESUME** or **QUIT**, **OK** confirms (quit returns every board to title).

## Build

Arduino-ESP32 core **3.3.x**, board **XIAO_ESP32C3**. No extra libraries (OLED is a local SSD1306 driver).

```powershell
# once
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32

# compile
.\build.ps1
```

Or:

```powershell
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32C3 --output-dir .\build
```

Output (flashable):

- `build/wartoads.ino.bin` — application
- `build/wartoads.ino.merged.bin` — full image from address `0x0` (bootloader + partitions + app)

## Flash all 2–4 boards

Same `.bin` on every device.

1. Plug board 1 via USB-C.
2. If no COM port appears, hold **BOOT** (D9 / RIGHT) while tapping **RESET**, then release BOOT.
3. Flash:

```powershell
.\flash.ps1 COM5
```

Or:

```powershell
arduino-cli upload -p COM5 --fqbn esp32:esp32:XIAO_ESP32C3 --input-dir .\build
```

4. Unplug, plug the next board, repeat with its COM port (`arduino-cli board list`).
5. Power from LiPo or USB. After boot: **OK** on each unit → lobby → all READY → match starts.

Merged-image alternative (esptool, address 0x0):

```powershell
python -m esptool --chip esp32c3 --port COM5 --baud 921600 write_flash 0x0 .\build\wartoads.ino.merged.bin
```

## How a match works

1. Lowest MAC address becomes **HOST** (automatic).
2. Player IDs **P1–P4** are that same MAC sort order.
3. Host owns terrain, HP, wind, physics, deaths, winner.
4. Clients send button intentions; host broadcasts snapshots (~12–25 Hz) plus explosion/crater events.
5. Terrain CRC mismatch → client requests 24×128-byte chunks.

Weapons: **Bazooka** (unlimited, max 45), **Grenade** ×2 (max 50), **Airstrike** ×1 (max 50/bomb), **Teleport** ×1, **Blowtorch** ×1 (2s fuel, 30 dmg), **Molli** (unlimited, 30), **Bat** ×2 (40 + strong knockback). Maps are **256×96** (camera follows) with trees, bushes, rocks, and girders stamped into destructible terrain. 100 HP. Wind is a compact `>2` / `<1` reading. After 8 rounds, **sudden death** raises the water.

## Architecture

```
wartoads.ino             setup/loop
src/config.h             pins, constants, packet helpers
src/display.*            SSD1306 128×64 driver + 5×7 font
src/buttons.*            debounce + hold-repeat
src/terrain.*            1-bit bitmap, generate, crater, CRC
src/protocol.h           binary header + CRC8
src/net.*                ESP-NOW, RX queue (callback is copy-only)
src/game.*               host election, lobby, sim, weapons
src/render.*             OLED scenes
src/mirror.*             USB framebuffer dump, idle until a host asks
```

ESP-NOW channel **1**, broadcast discovery, locked peer list after `GAME_START`.

## Serial debug (921600)

MAC, OLED status, free heap at boot, mode/phase/turn.
