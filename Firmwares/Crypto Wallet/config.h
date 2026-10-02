#pragma once
//
// Sentinel — hardware configuration for the Seeed XIAO ESP32-C5
//
// Keep ALL board-specific pin and geometry constants here so the rest of the
// firmware stays hardware-agnostic.

// ---- Firmware identity ------------------------------------------------------
#define FW_NAME    "Sentinel"
#define FW_VERSION "V0.9B"

// ---- Serial -----------------------------------------------------------------
#define SERIAL_BAUD 115200

// ---- OLED (SSD1306, I2C) ----------------------------------------------------
// The XIAO form factor exposes the default I2C bus on SDA=D4, SCL=D5, so
// Wire.begin() needs no arguments. U8g2 uses the 8-bit form of the address.
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_I2C_ADDR7  0x3C            // 7-bit; most 0.96" modules use 0x3C
#define OLED_I2C_ADDR8  (OLED_I2C_ADDR7 << 1)

// ---- Buttons ----------------------------------------------------------------
// Wired between GPIO and GND; internal pull-ups make them active-LOW.
// NOTE: if any of these is a strapping pin on the C5, move it to a free GPIO
// and update the mapping here (see Seeed pin-multiplexing wiki).
#define PIN_BTN_UP      D0
#define PIN_BTN_DOWN    D1
#define PIN_BTN_SELECT  D2   // "OK"
#define PIN_BTN_BACK    D3   // "Back/Del"
#define PIN_BTN_LEFT    D8   // Left  — advances the coin picker (GPIO8, not a strapping pin)
#define PIN_BTN_RIGHT   D9   // Right — steps the coin picker back (GPIO9, not a strapping pin)

// ---- Button timing (ms) -----------------------------------------------------
#define BTN_DEBOUNCE_MS    25
#define BTN_LONGPRESS_MS   700
#define BTN_REPEAT_MS      180   // auto-repeat interval while held (Up/Down)

// ---- Auto-lock --------------------------------------------------------------
// After this long with no button interaction, the device re-locks (wipes keys
// from RAM, turns BLE off) and returns to the PIN screen.
#define AUTO_LOCK_MS       (3UL * 60UL * 1000UL)   // 3 minutes

// ---- PIN & encrypted storage (Phase 3) --------------------------------------
#define PIN_MIN_DIGITS     4
#define PIN_MAX_DIGITS     8
#define PIN_MAX_TRIES      5     // wrong attempts before the wallet self-wipes
#define PBKDF2_ITERS       60000 // KDF cost; tune for the C5 unlock latency
#define NVS_NAMESPACE      "cw"  // NVS namespace for wallet blobs

// ---- BLE (Phase 6, optional, default OFF) -----------------------------------
// Nordic UART Service (NUS) — a de-facto "serial over BLE" profile supported by
// generic BLE terminal apps, so the PSBT flow works over BLE with no custom app.
#define BLE_DEVICE_NAME       "Sentinel"
#define BLE_NUS_SERVICE_UUID  "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_NUS_RX_UUID       "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // client -> device (write)
#define BLE_NUS_TX_UUID       "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // device -> client (notify)

// Where a signed PSBT should be returned. Defined here (not in the .ino) so the
// type is known before the Arduino IDE's auto-generated function prototypes.
enum SignSource { SRC_SERIAL, SRC_BLE };

// Selected coin (persisted). BTC = native segwit BIP84; ETH = BIP44 m/44'/60';
// SOL = ed25519 SLIP-0010 m/44'/501'; XLM = ed25519 SLIP-0010 m/44'/148';
// ADA = Cardano Icarus/BIP32-Ed25519 m/1852'/1815'.
#define COIN_BTC   0
#define COIN_ETH   1
#define COIN_SOL   2
#define COIN_XLM   3
#define COIN_ADA   4
#define COIN_BNB   5   // BNB Smart Chain — same secp256k1/EVM address as ETH
#define COIN_XRP   6   // XRP Ledger — secp256k1 m/44'/144', XRP base58check
#define COIN_USDT  7   // Tether ERC-20 — same ETH address, tagged separately
#define COIN_USDC  8   // USD Coin ERC-20 — same ETH address, tagged separately
#define COIN_AVAX  9   // Avalanche C-Chain — EVM, same ETH address (chainId 43114)
#define COIN_BASE  10  // Base L2 — EVM, same ETH address, gas in ETH (chainId 8453)
#define COIN_POL   11  // Polygon (POL) — EVM, same ETH address (chainId 137)
#define COIN_CRO   12  // Cronos — EVM, same ETH address (chainId 25)
#define COIN_ATOM  13  // Cosmos Hub — secp256k1 m/44'/118', bech32 "cosmos", blind-sign
#define COIN_TRX   14  // TRON — secp256k1 m/44'/195', base58check "T...", blind-sign
#define COIN_TON   15  // Toncoin — SLIP-10 ed25519 m/44'/607', v4R2 address, blind-sign
#define COIN_COUNT 16
