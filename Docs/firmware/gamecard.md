# Gamecard: pocket game console

A retro game console firmware with **8 single-player games** and **4 multiplayer games**. Multiplayer runs over **ESP-NOW**, so nearby Splunkerboards play together without a Wi-Fi network.

← [Firmware overview](README.md) · [Main README](../../README.md) · Full reference: [`Firmwares/Gamecard/gamecard/README.md`](../../Firmwares/Gamecard/gamecard/README.md)

| | |
|---|---|
| Folder | [`Firmwares/Gamecard/gamecard/`](../../Firmwares/Gamecard/gamecard) |
| Main sketch | `gamecard.ino` (the IDE pulls in all the other `.ino` files automatically) |
| Target | XIAO ESP32-C3, arduino-esp32 **3.x** (2.x ESP-NOW callback supported too) |
| Libraries | Adafruit GFX, Adafruit SSD1306 |
| Board | Splunkerboard Dev (V1B) |
| Display | SSD1306 `0x3C`, rotated 180° |

## Games

| Game | Players | Controls |
|---|---|---|
| Snake | 1 | D-pad steers |
| Tetris | 1 | ←/→ move, **A** rotate, ↓ soft drop, ↑ hard drop |
| 2048 | 1 | D-pad slides tiles |
| Runner | 1 | **A**/↑ jump (endless) |
| Breakout | 1 | ←/→ paddle |
| Invaders | 1 | ←/→ move, **A** shoot |
| Minesweeper | 1 | D-pad cursor, tap **A** reveal, hold **A** flag |
| Simon | 1 | Repeat the pattern with the D-pad |
| Pong | 1 (vs CPU) or 2 | ↑/↓ paddle |
| Tron | 2–4 | D-pad steers (no reversing) |
| Battleship | 2 | D-pad cursor, **A** fire. Each player's fleet stays on their own screen. |
| Uno | 2–4 | ←/→ pick a card or DRAW, **A** play. A wild card asks for a colour. |

**A = OK, B = Back.** Back quits any game to the menu, and A replays from a game-over screen.

## Multiplayer

1. On one unit, open the game and choose **Host Game**. It advertises a lobby under its device name.
2. On the others, choose **Join Game**, pick the lobby and press **A**.
3. The host presses **A** to start (Tron and Uno: once at least one player has joined).

| Layer | File | Used by | Model |
|---|---|---|---|
| 2-player transport | `netplay.ino` | Pong | Host simulates the ball and left paddle, client sends its paddle |
| ≤4-player transport | `netmp.ino` | Tron | Host broadcasts authoritative state every tick, clients unicast input |
| Turn-based messages | `netmp.ino` + `mplobby.ino` | Battleship, Uno | `NET_MMSG` with a 40-byte payload, a receive queue and timed resends (Uno re-broadcasts state every 700 ms) |

All units stay on the default Wi-Fi channel. A magic byte tags every packet, and receivers filter by game id and peer MAC, so nearby games don't interfere.

**Device name:** defaults to `GC-XXXX` (from the MAC). Change it in **Settings**: ←/→ moves the cursor, ↑/↓ changes the character, **A** saves (to NVS), **B** cancels. Names can be up to 10 characters.

## Pin usage

Gamecard hard-codes **ESP32-C3 GPIO numbers** in `gamecard.ino`:

```cpp
const uint8_t BTN_PINS[NUM_BTNS] = { 2, 3, 8, 9, 4, 5 };  // UP, DOWN, LEFT, RIGHT, A, B
...
Wire.begin(6, 7);                                         // SDA, SCL
```

| Button | C3 GPIO | XIAO pin |
|---|---|---|
| UP | 2 | D0 |
| DOWN | 3 | D1 |
| LEFT | 8 | D8 |
| RIGHT | 9 | D9 |
| A (OK) | 4 | D2 |
| B (Back) | 5 | D3 |

**On a XIAO ESP32-C5 / C6 / S3**, change these to the portable form:

```cpp
const uint8_t BTN_PINS[NUM_BTNS] = { D0, D1, D8, D9, D2, D3 };
...
Wire.begin();
```

**On Splunkerboard 1**, replace `pollButtons()` in `input.ino` with an MCP23017 read. See [porting](README.md#porting-to-splunkerboard-1) and the [MCP23017 driver](development-guide.md#splunkerboard-1-buttons-via-mcp23017).

## Adding a game

1. Write `myGameInit()` / `myGameUpdate()` in a new `.ino` file and declare them in `common.h`.
2. Add a launcher in `gamecard.ino`, `static void L_mine(){ startGame(myGameInit, myGameUpdate); }`, and a `{ "My Game", L_mine }` row in `GAMES[]`.
3. All `.ino` files compile into one translation unit, so give file-scope `static`s unique, game-prefixed names.
4. Return to the menu with `setState(ST_MENU)`.

Input helpers (from `input.ino`): `btnDown[i]` (held), `btnPressed[i]` (rising edge), `btnHeldRepeat(i, firstMs, repeatMs)` (auto-repeat).

## Roadmap

Planned games on the `netmp.ino` layer: a trivia/reaction buzzer and Connect 4.
