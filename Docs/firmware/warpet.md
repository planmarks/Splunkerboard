# WarPet (Wormagotchi): wardriving virtual pet

A Tamagotchi-style creature that **grows by surveying the radio environment**. It earns XP for every *unique* Wi-Fi access point (2.4 **and 5 GHz**), BLE device and Wi-Fi client station it observes, and logs each discovery to on-device flash.

← [Firmware overview](README.md) · [Main README](../../README.md) · Full reference: [`Firmwares/Wormagotchi/README.md`](../../Firmwares/Wormagotchi/README.md)

| | |
|---|---|
| Folder | [`Firmwares/Wormagotchi/`](../../Firmwares/Wormagotchi) |
| Main sketch | `xiaoesp32c5_tamagotchi.ino` (+ `config.h`, `names.h`, `sprites.h`) |
| Target | XIAO ESP32-C5 (dual-band), arduino-esp32 **≥ 3.1.0** |
| Libraries | Adafruit SSD1306, Adafruit GFX, NimBLE-Arduino 2.x |
| Partition scheme | One with a filesystem, e.g. *Default 4MB with spiffs*. The capture log uses **LittleFS**. |
| Board | Splunkerboard Dev (V1B) |

> **Passive by design.** WarPet only listens: it reads beacons and BLE advertisements, and in monitor mode it counts 802.11 frames and records transmitter MACs. It does **not** deauthenticate, inject packets, join foreign networks or store payloads. Use it only where passive monitoring is legal.

## Gameplay

| Discovery | XP |
|---|---|
| New Wi-Fi AP (2.4 or 5 GHz) | 10 |
| New BLE device | 8 |
| New Wi-Fi client (monitor mode) | 12 |
| Every 25 frames observed | 1 |

There's no level cap. Reaching the next level takes `100 × 1.15^level` XP.

| Stage | Levels | Unlocks |
|---|---|---|
| Egg | 0–4 | — |
| Baby | 5–9 | — |
| Teen | 10–14 | **Packet monitor** (promiscuous frame counting, 2.4 GHz channel hopping) |
| Adult | 15+ | **Promiscuous harvest** (collects unique client MACs) |

**Stats (0–100):**

- **Hunger** works as satiation: discoveries feed the pet and idle time drains it.
- **Energy** comes back with discoveries, scaled by hunger. Monitor modes drain 1 % every 5 s, and the pet sleeps at 0.
- **Mood** is 25 % petting and 75 % discoveries and hunger. Below 25 it's sad.
- **Age** counts powered-on time and persists across reboots.

On first boot, the pet gets a random name generated from hardware entropy (e.g. `Byte-Gremlin`), stored in NVS.

## Controls

| Screen | ← / → | ↑ | ↓ | OK | Back |
|---|---|---|---|---|---|
| Home | Switch screen | Feed | Play | Pet | Home |
| Pet / Stats | Switch screen | — | — | Menu | Home |
| Menu | Switch screen | Up | Down | Select | Home |

**Menu items:** Feed · Play · Pet · Scan ON/OFF · Pkt Monitor (Teen+) · Promisc Harvest (Adult+) · Config (serial) · Save now · Reset pet (press OK twice).

## Data logging

Each unique capture is appended to `/captures.bin` (LittleFS ring buffer, `RECORD_CAP` = 6000 records, oldest overwritten first):

```
timestamp_s, type (AP2G | AP5G | BLE | CLIENT), mac, rssi, channel, ssid
```

The uniqueness sets (`/seen_ap.bin`, `/seen_ble.bin`, `/seen_cl.bin`) survive reboots, so the same device never earns XP twice. Use the `d` serial command to export the log as CSV.

## Serial console (115200 baud)

| Command | Action |
|---|---|
| `i` | Status |
| `w <ssid> <pass>` | Set home Wi-Fi (for future Wigle upload) |
| `k <apiname> <token>` | Set Wigle API credentials |
| `n <name>` | Rename the pet |
| `d` | Dump the capture log as CSV |
| `s` | Save now |
| `c` | Clear capture storage |
| `x` | Reset the pet (wipes progress and logs) |
| `h` | Help |

## Configuration (`config.h`)

| Group | Key settings |
|---|---|
| Pins | `PIN_BTN_UP…RIGHT` = D0, D1, D2, D3, D8, D9. `OLED_ADDR` `0x3C`. `OLED_ROTATION` 2 (180°). |
| Features | `ENABLE_5G`, `ENABLE_BLE`, `ENABLE_MONITOR`, `ENABLE_WIGLE_UPLOAD` (0, waiting on GPS support) |
| Scanning | `WIFI_SCAN_MS` 260 ms per channel, `BLE_SCAN_MS` 3000, `MON_HOP_MS` 300, channels 1–13 |
| Progression | XP per discovery type, `XP_LEVEL_BASE` 100, `XP_LEVEL_GROWTH` 1.15, stage levels 5/10/15 |
| Stats | Tick 10 s, hunger and energy rates, mood thresholds |
| Storage | `AUTOSAVE_MS` 60 s, `RECORD_CAP` 6000, file paths |

## UI assets

`sprites.h` holds the creature and HUD bitmaps (1 bpp, exported from [Lopaka](https://lopaka.app/) and adapted for Adafruit GFX). [`Visual Elements/`](../../Firmwares/Wormagotchi/Visual%20Elements) contains the original Lopaka code exports for every screen: the empty and stats HUD, plus two animation frames each for egg, baby, teen and adult.

## Splunkerboard 1 opportunities

Once the buttons are ported to the MCP23017 ([porting guide](README.md#porting-to-splunkerboard-1)), Splunkerboard 1 hardware makes these upgrades possible:

- Log captures to **microSD** instead of the 6000-record LittleFS ring
- Show a real battery level from the **MAX17048**
- Add a **GPS** module on the D6/D7 UART header to unlock Wigle uploads

## Notes

- Wi-Fi and BLE take turns on the radio (they don't scan at the same time), which keeps coexistence stable on the C5.
- Monitor mode hops 2.4 GHz channels only.
- Many phones randomise their MAC address, so client counts reflect addresses seen, not unique people.
- The in-folder README mentions an "Elder" stage. The current code tops out at **Adult** (level 15+).
