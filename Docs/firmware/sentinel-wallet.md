# Sentinel: air-gapped cold wallet firmware

**Sentinel** (firmware `V0.9B`, "Phase 6") turns a Splunkerboard with a **XIAO ESP32-C5** into an offline signing device. It generates or restores a BIP39 seed, stores it encrypted under a PIN, shows receive addresses as text and QR codes, and signs transactions that a watch-only wallet on a computer prepares. Before it signs anything, it shows the transaction on its own screen and waits for a long press to confirm.

← [Firmware overview](README.md) · [Main README](../../README.md)

| | |
|---|---|
| Folder | [`Firmwares/Crypto Wallet/`](../../Firmwares/Crypto%20Wallet) |
| Main sketch | `coldwallet.ino` |
| Target | Seeed XIAO ESP32-C5, arduino-esp32 **≥ 3.3.5** |
| Libraries | **U8g2**, **uBitcoin**, **NimBLE-Arduino** |
| Vendored | `qrcodegen.*` (QR codes), `tweetnacl.*` (ed25519), `blake2*` (BLAKE2b for Cardano) |
| Board | Splunkerboard Dev (V1B) wiring, buttons on D0/D1/D2/D3/D8/D9 |

> ⚠️ **Security status: experimental.** The project's own notes say that flash encryption and Secure Boot v2 ("Phase 7") are **not enabled yet**. The encrypted seed sits in ordinary flash, protected only by the PIN-derived key and the wipe counter. The TON address derivation is marked **unverified on hardware**. Use test amounts and testnets until the firmware has been reviewed.

---

## Features

- **Seed generation:** 24-word BIP39 from the ESP32-C5 hardware TRNG (SAR-ADC entropy source enabled so it's truly random even with radios off), through an mbedTLS CTR-DRBG
- **Seed restore:** 12 or 24 words, typed by letter prefix with BIP39 wordlist autocomplete, then checked against the BIP39 checksum
- **Optional BIP39 passphrase** (the "25th word"), stored encrypted with the seed
- **PIN protection:** 4–8 digits, PBKDF2-HMAC-SHA256 (60 000 iterations, random salt) → AES-256-GCM
- **Self-wipe** after **5** wrong PINs. The attempt counter is written to flash *before* each check, so power-cycling doesn't reset it.
- **Auto-lock** after 3 minutes idle. Keys are zeroized from RAM, BLE turns off and the OLED blanks.
- **16 assets:** receive addresses (text + QR) and on-device transaction review/signing
- **Transports:** USB serial (default) or optional **BLE Nordic UART Service**. BLE is off by default and only comes up after unlock.
- **Self-test:** at boot, derives the official BIP84 test vector and prints PASS/FAIL to serial
- Secrets are **never printed to serial**. The mnemonic only ever appears on the OLED.

## Supported assets

| Coin | Derivation path | Address | Signing | Review on device |
|---|---|---|---|---|
| **BTC** | BIP84 `m/84'/0'/0'/0/i` (testnet `1'`) | `bc1q…` / `tb1q…` | PSBT (BIP174) | Every output (address, amount, change flag) + fee |
| **ETH** | BIP44 `m/44'/60'/0'/0/i` | EIP-55 `0x…` | EIP-1559 (device builds the RLP) | Recipient, amount, fee; ERC-20 transfers decoded |
| **BNB** (BSC), **AVAX**, **BASE**, **POL**, **CRO** | same as ETH | same as ETH | EIP-1559, chain set by `chainId` | same as ETH |
| **USDT**, **USDC** (ERC-20) | same as ETH | same as ETH | EIP-1559 + ERC-20 | Token transfer decoded |
| **SOL** | SLIP-10 `m/44'/501'/i'/0'` (Phantom) | base58 | ed25519 over legacy message | SystemProgram transfer, otherwise a blind-sign warning |
| **XLM** | SEP-0005 `m/44'/148'/i'` | `G…` | ed25519 over the signature base | Native payment, otherwise blind (tx hash shown) |
| **ADA** | CIP-1852 `m/1852'/1815'/0'/{0,2}/i` (Icarus) | `addr1…` / `addr_test1…` | Ed25519-BIP32 over the BLAKE2b-256 body hash | **Blind** (tx hash shown) |
| **XRP** | BIP44 `m/44'/144'/0'/0/i` | `r…` | secp256k1 over SHA-512-half | Summary |
| **ATOM** | BIP44 `m/44'/118'/0'/0/i` | `cosmos1…` | SIGN_MODE_DIRECT | **Blind** (SHA-256 digest prefix shown) |
| **TRX** | BIP44 `m/44'/195'/0'/0/i` | `T…` | secp256k1 recoverable | **Blind** (txID prefix shown) |
| **TON** | SLIP-10 `m/44'/607'/0'` (Ledger-style) | v4R2 `EQ…` | ed25519 over the 32-byte hash | **Blind** (hash shown) |

Notes:

- The BIP39 passphrase **doesn't affect Cardano** addresses (Icarus derives from raw entropy, the same as Yoroi and Eternl).
- **TON** addresses won't match Tonkeeper's native mnemonic accounts. They match tools that use the BIP39 + `m/44'/607'` path.
- All EVM chains share one address. Only the `chainId` in the transaction differs.

## Controls

| Button | Role |
|---|---|
| **Up / Down** | Move the cursor, change a digit or letter, page through screens |
| **Left / Right** | On the **Coin** menu item, step forward/back through the coin list |
| **OK** (short) | Select or commit |
| **OK** (hold ≥ 700 ms) | Confirm: submit PIN, accept word, **approve signature**, confirm wipe |
| **Back** | Back, cancel, delete last digit or letter |

## Menus

```
Main menu                    Settings
├── Coin: <BTC>  (←/→)       ├── Generate Seed
├── Receive                  ├── Restore Wallet
├── Sign Tx                  ├── Net: TESTNET / MAINNET
├── Settings ──────────────► ├── BLE: OFF / ON
└── About                    ├── Export xpub      (BTC only)
                             ├── Wipe wallet      (hold OK)
                             └── Back
```

The network defaults to **TESTNET**. Switch to MAINNET in Settings.

## First-time setup

1. Flash the firmware and open the serial monitor at **115200 baud** (optional). Check that it prints `bip84 self-test … PASS`.
2. **Settings → Generate Seed** (or **Restore Wallet**).
3. Write down the 24 words, which are shown page by page on the OLED.
4. Optionally add a **passphrase**: hold OK to add one, Back to skip.
5. Set a **PIN** (4–8 digits) and enter it twice. Up/Down change the digit, OK commits it, hold OK to finish.
6. After a reboot, the device asks for the PIN before it does anything else.

## Receiving

**Receive** shows address index 0. Up/Down step through indices 0–999, and OK shows the address as a QR code. Every shown address is also printed to serial (and over BLE when connected) as:

```
addr #<index> <COIN>: <address>
```

For a watch-only Bitcoin wallet (Sparrow, Electrum, etc.), use **Settings → Export xpub**. It shows the xpub as a QR code and prints:

```
XPUB>
<xpub>
FINGERPRINT>
<8 hex chars>
DESCRIPTOR>
wpkh([<fp>/84h/0h/0h]<xpub>/<0;1>/*)
```

## Signing protocol

1. Select the coin, choose **Sign Tx**, and send **one line** over serial (or BLE NUS while the main menu is open).
2. The device parses the line and shows the review pages. Step through them with Up/Down.
3. On the last page, **hold OK** to sign (or press Back to reject).
4. The device prints a result label and the signed payload on the next line. Over BLE, the payload is also sent back.

| Coin | Input line | Output label | Output payload |
|---|---|---|---|
| BTC | `<base64 PSBT>` | `PSBT-SIGNED>` | Signed PSBT, base64 |
| ETH / BNB / USDT / USDC / AVAX / BASE / POL / CRO | `ETH\|acct\|chainId\|nonce\|maxPrioFee\|maxFee\|gasLimit\|to\|value\|data` (prefix = coin ticker) | `ETH-SIGNED>` (or `BNB-SIGNED>` …) | Signed raw tx |
| SOL | `SOL\|acct\|messageHex` | `SOL-SIGNED>` | base58 signature |
| XLM | `XLM\|acct\|signatureBaseHex` | `XLM-SIGNED>` | 64-byte signature, hex |
| ADA | `ADA\|index\|txBodyHex` | `ADA-SIGNED>` | `vkeyHex(64) ‖ sigHex(128)` |
| XRP | `XRP\|acct\|txBodyHex` | `XRP-SIG>` | DER signature, hex |
| ATOM | `ATOM\|acct\|signDocHex` | `ATOM-SIG>` | 64-byte `r‖s`, hex |
| TRX | `TRX\|acct\|rawDataHex` | `TRX-SIG>` | 65-byte `r‖s‖v`, hex |
| TON | `TON\|acct\|msgHashHex` | `TON-SIG>` | 64-byte ed25519 signature, hex |

EVM numeric fields are big-endian hex, `acct` is decimal, and `to`/`data` are hex without `0x` (`data` may be empty).

Example (BTC over USB serial):

```
PSBT> paste base64 PSBT (one line, end with newline):
cHNidP8BAHECAAAAAa...                 ← you paste
PSBT-SIGNED>
cHNidP8BAHECAAAAAa...                 ← device returns
```

## BLE transport

- **Settings → BLE: ON**. The device advertises as `Sentinel-XXXX` (suffix taken from the MAC).
- Nordic UART Service:
  - Service `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
  - RX (write, host → device) `6E400002-…`
  - TX (notify, device → host) `6E400003-…`
- Any generic BLE UART app (nRF Connect, Serial Bluetooth Terminal) works. Send the same one-line payloads, terminated by a newline.
- BLE is only a transport. Every transaction still needs on-device review and a long press. The radio is off while the wallet is locked.

## Source layout

| File | Purpose |
|---|---|
| `coldwallet.ino` | UI state machine, menus, flows (unlock, generate, restore, receive, sign), auto-lock |
| `config.h` | Pins, timings, PIN/KDF parameters, BLE UUIDs, coin IDs |
| `display.*` | U8g2 wrapper: menus, messages, QR rendering |
| `buttons.*` | Debounce, long-press, auto-repeat |
| `wallet.*` | TRNG entropy, BIP39, BIP84 receive addresses, xpub, self-test |
| `store.*` | AES-256-GCM encrypted NVS storage, PIN tries, settings |
| `signer.*` | BTC PSBT parsing, review summary, signing (uBitcoin) |
| `ble.*` | NimBLE Nordic UART Service transport |
| `eth.*`, `bnb.*` | EVM addresses + EIP-1559 signing, ERC-20 decoding |
| `sol.*`, `xlm.*`, `ton.*` | ed25519 chains |
| `ada.*` | Cardano Icarus / BIP32-Ed25519, Shelley base address |
| `xrp.*`, `cosmos.*`, `tron.*` | Other secp256k1 chains |
| `slip10.*`, `ed25519.*` | SLIP-0010 derivation, ed25519 on TweetNaCl (replaces rweather Crypto, which crashed on the C5) |
| `qrcodegen.*`, `tweetnacl.*`, `blake2*` | Vendored third-party code |

## Configuration (`config.h`)

| Constant | Default | Meaning |
|---|---|---|
| `AUTO_LOCK_MS` | 3 min | Idle time before re-locking |
| `PIN_MIN_DIGITS` / `PIN_MAX_DIGITS` | 4 / 8 | PIN length |
| `PIN_MAX_TRIES` | 5 | Wrong PINs before self-wipe |
| `PBKDF2_ITERS` | 60000 | KDF cost (affects unlock time) |
| `BTN_LONGPRESS_MS` | 700 | Hold-to-confirm duration |
| `OLED_I2C_ADDR7` | `0x3C` | Display address |
| `PIN_BTN_*` | D0, D1, D2, D3, D8, D9 | Button pins (Dev board) |

## Known limitations

- **No flash encryption or Secure Boot** yet (planned "Phase 7"). Anyone who can read the flash can brute-force the PIN offline.
- The loop task stack is raised to **24 KB** (`SET_LOOP_TASK_STACK_SIZE`) for the ed25519 math. Keep it there if you modify the code.
- True CPU light sleep while locked was removed because it crashed the C5. The device uses an OLED-off polling idle instead.
- **Display orientation:** Sentinel uses `U8G2_R0`, while Gamecard and WarPet rotate the panel 180°. If the image is upside-down on your board, change `U8G2_R0` to `U8G2_R2` in `display.h`.
- Splunkerboard 1 (MCP23017 buttons) isn't supported yet. See [porting](README.md#porting-to-splunkerboard-1).
- ADA, ATOM, TRX and TON signing is **blind**. Always compare the hash on screen with what your host software shows.
