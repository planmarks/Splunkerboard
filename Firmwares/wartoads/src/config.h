#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

// ---------------------------------------------------------------------------
// WARTOADS — hardware + game constants
// Fat-toad artillery, inspired by Team17 Worms.
// Custom handheld: Seeed XIAO ESP32C3 + 0.96" SSD1306 OLED + 6 buttons
// ---------------------------------------------------------------------------

#define WM_VERSION_STR "1.3.0-pace"

// Handheld PCB (XIAO ESP32C3):
//   D0 UP, D1 DOWN, D2 OK, D3 BACK, D8 LEFT, D9 RIGHT
//   OLED I2C on D4/D5. Buttons are active-LOW with internal pull-ups.
// D9 is also the module BOOT pin — do not hold RIGHT while resetting.

#define OLED_W          128
#define OLED_H          64
#define OLED_ADDR_PRIMARY  0x3C
#define OLED_ADDR_SECONDARY 0x3D
#define OLED_ROTATE_180 1

#define PIN_SDA         6     // D4
#define PIN_SCL         7     // D5

#define PIN_BTN_AIM_UP  2     // D0  UP
#define PIN_BTN_AIM_DN  3     // D1  DOWN
#define PIN_BTN_FIRE    4     // D2  OK  (START / FIRE / READY)
#define PIN_BTN_WEAPON  5     // D3  BACK (weapon menu / pause)
#define PIN_BTN_LEFT    8     // D8  LEFT
#define PIN_BTN_RIGHT   9     // D9  RIGHT (strapping/BOOT)

#define BTN_COUNT       6
#define BTN_LEFT        0
#define BTN_RIGHT       1
#define BTN_AIM_UP      2
#define BTN_AIM_DN      3
#define BTN_FIRE        4
#define BTN_WEAPON      5

#define MAX_PLAYERS     4
#define MAX_PROJ        8
#define MAX_MINES       4
#define MAX_SENTRY      4
#define MAX_PARTICLES   56
#define MAX_FOUND       3
#define RX_QUEUE        16
#define PKT_MAX         250

#define HUD_H           16
#define WATER_H_DEFAULT 8
#define WORLD_W         256
#define WORLD_H         96
#define WORLD_TOP       2
#define PLAY_TOP        WORLD_TOP
#define SKY_MIN_Y       (-40)
#define FB_BYTES        (OLED_W * OLED_H / 8)
#define TERRAIN_BYTES   (WORLD_W * WORLD_H / 8)
#define MAP_CHUNK_LEN   128
#define MAP_CHUNK_N     (TERRAIN_BYTES / MAP_CHUNK_LEN)
#define ZOOM_MAP        0
#define ZOOM_MIN        0
#define ZOOM_MAX        2
// 0 overview (~0.57x), 1 1x, 2 1.5x
#define JUMP_VEL        (-3)
#define JUMP_FWD        1
#define JUMP_COUNT      2
#define CLIMB_MAX       8
#define WALK_CLIMB      3
#define WALK_DROP       6
#define AIM_MS_PER_DEG  14
#define MOVE_MS_PER_PX  49
#define AIR_MS_PER_PX   28
#define JET_MS_PER_PX   24
#define JET_STRAFE_MS_PER_PX 32
#define TARGET_MS_PER_PX 36
#define WALK_FRAME_MS   82
#define WORM_H          6
#define TORCH_MS        2000
#define TORCH_R         2
#define TORCH_CANCEL_MS 120
#define AMMO_TORCH      2
#define BAT_MS          380
#define AMMO_BAT        2
#define AMMO_JET        50
#define JET_FUEL_MS     100
#define AMMO_MINE       1
#define MINE_ARM_MS     3000
#define AMMO_DYN        1
#define DYN_FUSE_MS     5000
#define DYN_R           8
#define DMG_DYN         60
#define LIVES_MIN       1
#define LIVES_MAX       4
#define AMMO_HOME       1
#define AMMO_DRACO      2
#define DRACO_N         15
#define DRACO_SHOT_MS   55
#define DRACO_R         1
#define DMG_DRACO       3
#define AMMO_SENTRY     1
#define SENTRY_HP       30
#define SENTRY_SHOTS    30
#define SENTRY_RANGE    42
#define SENTRY_SHOT_MS  120
#define SENTRY_ARM_MS   500
#define SENTRY_TURN_MS  1000
#define SENTRY_FUSE_MS  1300
#define SENTRY_TURN_SHOTS 30
#define DMG_SENTRY      1
#define AMMO_PIPE       1
#define PIPE_STICK_MS   3000
#define PIPE_R          12
#define DMG_PIPE        65
#define DMG_PIPE_MIN    10
#define DEATH_BURST_MS  2000
#define DEATH_KILL_MS   700
#define DEATH_BLAST_R   4
#define DEATH_BLAST_DMG 20
#define JET_CANCEL_MS   120
#define DMG_MINE        45
#define DMG_HOME        45
#define HOMING_LOCK_MS  1000
#define JUMP_TAP_MS     180
#define JUMP_DOUBLE_MS  340
#define COYOTE_MS       140
#define WPN_OPEN_MS     400
#define PAUSE_HOLD_MS   800
#define WPN_COOLDOWN_MS 850
#define JET_HOVER_MS    320
#define MENU_REPEAT_DELAY_MS 320
#define MENU_REPEAT_MS  200

#define HP_MAX          100
#define DMG_BAZOOKA     45
#define DMG_GRENADE     50
#define DMG_AIRSTRIKE   50
#define DMG_MOLLI       30
#define DMG_TORCH       30
#define DMG_BAT         40
#define TURN_MS         15000
#define TURN_OPT_N      4
inline uint16_t turnMsFor(uint8_t opt) {
  static const uint16_t v[TURN_OPT_N] = {10000, 15000, 20000, 30000};
  if (opt >= TURN_OPT_N) opt = 1;
  return v[opt];
}
#define LOBBY_ROWS      2
#define CHARGE_MS       1300
#define GRENADE_FUSE_MS 2200
#define MOLLI_WINDUP_MS 300
#define MOLLI_MS        420
#define AMMO_MOLLI      3
#define HEARTBEAT_MS    600
#define PEER_TIMEOUT_MS 25000
#define GRACE_MS        15000
#define HOST_LOST_MS    4000
#define COUNTDOWN_MS    2500
#define INTRO_MS        3400
#define EXPLODE_ANIM_MS 560
#define FALL_STEP_MS    28
#define MSG_MS          1100
#define DROWN_MSG_DELAY_MS 2000
#define TURN_BANNER_MS  800
#define TURN_END_MS     1100
#define TURN_ARROW_MS   2200
#define DMG_POP_MS      1800
#define DROWN_POP_MS    2400
#define DAMAGE_HANG_MS  900
#define FALL_MAX_MS     8000
#define SUDDEN_AFTER_ROUNDS 8
#define WATER_RISE      2
#define WATER_MAX_Y     52

#define BAZOOKA_R       7
#define GRENADE_R       6
#define AIRSTRIKE_R     4
#define AIRSTRIKE_N     5

#define AMMO_GRENADE    2
#define AMMO_AIR        1
#define AMMO_TELE       1

#define PHYS_DT_MS      16
#define PHYS_SUBSTEP    4

// 8.8 fixed. Readable arcs, not moon hang. Draco/sentry are fast needles.
#define PROJ_SPD_BASE   140
#define PROJ_SPD_GAIN   7
#define PROJ_PIPE_BASE  100
#define PROJ_PIPE_GAIN  5
#define PROJ_DRACO_SPD  620
#define PROJ_SENTRY_SPD 540
#define PROJ_HOME_CAP   280
#define PROJ_GRAV_BAZOOKA  14
#define PROJ_GRAV_GRENADE  16
#define PROJ_GRAV_PIPE     18
#define PROJ_GRAV_HOME     4
#define PROJ_GRAV_NEEDLE   1
#define PROJ_GRAV_BOMB     10
#define PROJ_GRAV_SKY      22
#define PROJ_WIND_BAZOOKA  5
#define PROJ_WIND_HOME     1
#define PROJ_WIND_NEEDLE   0
#define PROJ_WIND_BOMB     1
#define PROJ_BOUNCE_REST   18

#define PROTO_MAGIC0    'W'
#define PROTO_MAGIC1    'M'
#define PROTO_VERSION   16

#define PLAYER_NONE     0xFF

#define WM_DEBUG        1

// USB OLED mirror for desktop preview / MP4. Off until the laptop sends WMON.
#ifndef WM_MIRROR
#define WM_MIRROR       1
#endif

enum GameMode : uint8_t {
  MODE_TITLE = 0,
  MODE_BTN_TEST,
  MODE_DISCOVERY,
  MODE_LOBBY,
  MODE_COUNTDOWN,
  MODE_PLAY,
  MODE_GAMEOVER,
  MODE_REMATCH,
  MODE_PAUSE,
  MODE_HOST_LOST,
  MODE_GROUP_FULL,
  MODE_MP_MENU,
  MODE_SEEK,
  MODE_SETUP,
  MODE_BTN_CFG,
  MODE_MAIN_MENU,
  MODE_INTRO,
  MODE_CREDITS
};

#define TITLE_ROWS      5
#define MP_MENU_ROWS    2
#define SETUP_PRAC_ROWS 4
#define SETUP_HOST_ROWS 6

enum PlayPhase : uint8_t {
  PHASE_TURN_START = 0,
  PHASE_CONTROL,
  PHASE_WEAPON_MENU,
  PHASE_TARGET,       // airstrike / teleport X cursor
  PHASE_CHARGING,
  PHASE_BLOWTORCH,
  PHASE_MOLLI,
  PHASE_BAT,
  PHASE_JETPACK,
  PHASE_DRACO,
  PHASE_PROJECTILE,
  PHASE_EXPLOSION,
  PHASE_FALLING,
  PHASE_TURN_END,
  PHASE_SD_BANNER
};

enum Weapon : uint8_t {
  WPN_BAZOOKA = 0,
  WPN_GRENADE,
  WPN_AIRSTRIKE,
  WPN_TELEPORT,
  WPN_BLOWTORCH,
  WPN_MOLLI,
  WPN_BAT,
  WPN_JETPACK,
  WPN_MINE,
  WPN_HOMING,
  WPN_DRACO,
  WPN_SENTRY,
  WPN_PIPE,
  WPN_DYNAMITE,
  WPN_COUNT
};

#define WPN_MENU_ROWS 7
#define WPN_MENU_COLS 2

// Menu slots: inf first, movement last.
inline uint8_t wpnMenuAt(uint8_t slot) {
  static const uint8_t o[WPN_COUNT] = {
      WPN_BAZOOKA, WPN_MOLLI, WPN_GRENADE, WPN_PIPE, WPN_DRACO, WPN_AIRSTRIKE, WPN_BLOWTORCH,
      WPN_BAT,     WPN_HOMING, WPN_MINE,   WPN_DYNAMITE, WPN_SENTRY, WPN_TELEPORT, WPN_JETPACK};
  if (slot >= WPN_COUNT) return WPN_BAZOOKA;
  return o[slot];
}
inline uint8_t wpnMenuSlot(uint8_t w) {
  for (uint8_t i = 0; i < WPN_COUNT; i++)
    if (wpnMenuAt(i) == w) return i;
  return 0;
}

enum MapKind : uint8_t {
  MAP_CAVERN = 0,
  MAP_ISLAND,
  MAP_ISLANDS,
  MAP_COUNT
};

enum ProjType : uint8_t {
  PRJ_BAZOOKA = 0,
  PRJ_GRENADE,
  PRJ_BOMB,
  PRJ_HOME,
  PRJ_DRACO,
  PRJ_POD,
  PRJ_SENTRY,
  PRJ_PIPE,
  PRJ_PLANE
};

enum EventKind : uint8_t {
  EVT_DROWN = 1,
  EVT_DISCONNECT = 2,
  EVT_WIN = 3,
  EVT_SUDDEN = 4,
  EVT_HOST_LOST = 5,
  EVT_CRATER = 6,
  EVT_KILL = 7
};

inline int16_t wrap360(int a) {
  while (a < 0) a += 360;
  while (a >= 360) a -= 360;
  return (int16_t)a;
}

inline int16_t wm_clamp(int16_t v, int16_t a, int16_t b) {
  if (v < a) return a;
  if (v > b) return b;
  return v;
}

inline uint8_t packY(int16_t y) {
  int v = (int)y + 64;
  if (v < 0) v = 0;
  if (v > 255) v = 255;
  return (uint8_t)v;
}
inline int16_t unpackY(uint8_t y) { return (int16_t)((int)y - 64); }

inline int imin(int a, int b) { return a < b ? a : b; }
inline int imax(int a, int b) { return a > b ? a : b; }

inline uint8_t crc8(const uint8_t* d, int n) {
  uint8_t c = 0xFF;
  for (int i = 0; i < n; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++) {
      c = (c & 1) ? (uint8_t)((c >> 1) ^ 0x8C) : (uint8_t)(c >> 1);
    }
  }
  return c;
}

inline uint16_t fnv16(const uint8_t* d, int n) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < n; i++) {
    h ^= d[i];
    h *= 16777619u;
  }
  return (uint16_t)(h ^ (h >> 16));
}

struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed = 1) : s(seed ? seed : 1) {}
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  int16_t range(int16_t a, int16_t bExclusive) {
    uint32_t span = (uint32_t)(bExclusive - a);
    if (span == 0) return a;
    return (int16_t)(a + (int16_t)(next() % span));
  }
};

struct BufW {
  uint8_t* p;
  uint8_t* e;
  bool overflow;
  BufW(uint8_t* b, int n) : p(b), e(b + n), overflow(false) {}
  bool u8(uint8_t v) {
    if (p >= e) { overflow = true; return false; }
    *p++ = v;
    return true;
  }
  bool u16(uint16_t v) { return u8((uint8_t)v) && u8((uint8_t)(v >> 8)); }
  bool u32(uint32_t v) { return u16((uint16_t)v) && u16((uint16_t)(v >> 16)); }
  bool i16(int16_t v) { return u16((uint16_t)v); }
  bool i8(int8_t v) { return u8((uint8_t)v); }
  bool raw(const uint8_t* d, int n) {
    if (p + n > e) { overflow = true; return false; }
    memcpy(p, d, n);
    p += n;
    return true;
  }
  int used(const uint8_t* start) const { return (int)(p - start); }
};

struct BufR {
  const uint8_t* p;
  const uint8_t* e;
  bool err;
  BufR(const uint8_t* b, int n) : p(b), e(b + n), err(false) {}
  uint8_t u8() {
    if (p >= e) { err = true; return 0; }
    return *p++;
  }
  uint16_t u16() {
    uint8_t a = u8();
    uint8_t b = u8();
    return (uint16_t)(a | ((uint16_t)b << 8));
  }
  uint32_t u32() {
    uint16_t a = u16();
    uint16_t b = u16();
    return a | ((uint32_t)b << 16);
  }
  int16_t i16() { return (int16_t)u16(); }
  int8_t i8() { return (int8_t)u8(); }
  bool raw(uint8_t* d, int n) {
    if (p + n > e) { err = true; return false; }
    memcpy(d, p, n);
    p += n;
    return true;
  }
  int left() const { return (int)(e - p); }
};
