#include "render.h"
#include "display.h"
#include "buttons.h"
#include "terrain.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <Arduino.h>

static Particle parts[MAX_PARTICLES];

void spawnParticles(int x, int y, int n) {
  Rng r(millis() ^ (x << 8) ^ y);
  for (int i = 0; i < MAX_PARTICLES && n > 0; i++) {
    if (parts[i].on) continue;
    int spread = (n > 8) ? 5 : 2;
    parts[i].on = true;
    parts[i].x = (int16_t)(x + r.range(-1, 2));
    parts[i].y = (int16_t)(y + r.range(-1, 2));
    parts[i].vx = (int16_t)r.range(-spread, spread + 1);
    parts[i].vy = (int16_t)r.range(-spread - 2, 3);
    parts[i].life = (uint8_t)r.range(n > 8 ? 16 : 10, n > 8 ? 32 : 18);
    parts[i].splash = 0;
    n--;
  }
}

void spawnSplash(int x, int y) {
  Rng r(millis() ^ (x * 17) ^ (y << 3) ^ 0x5A);
  int n = 16;
  for (int i = 0; i < MAX_PARTICLES && n > 0; i++) {
    if (parts[i].on) continue;
    parts[i].on = true;
    parts[i].splash = 1;
    parts[i].x = (int16_t)(x + r.range(-5, 6));
    parts[i].y = (int16_t)(y - r.range(0, 3));
    parts[i].vx = (int16_t)r.range(-5, 6);
    parts[i].vy = (int16_t)r.range(-6, -2);
    parts[i].life = (uint8_t)r.range(28, 48);
    n--;
  }
}

void spawnJetPuff(int x, int y, int dir) {
  (void)dir;
  int n = 4;
  uint32_t now = millis();
  for (int i = 0; i < MAX_PARTICLES && n > 0; i++) {
    if (parts[i].on) continue;
    parts[i].on = true;
    parts[i].splash = 2;
    parts[i].x = (int16_t)(x + (int)((now / 9 + (uint32_t)n * 3) % 5) - 2);
    parts[i].y = (int16_t)(y + (int)(n & 1));
    parts[i].vx = (int16_t)((int)((now / 13 + n * 5) % 5) - 2);
    parts[i].vy = (int16_t)(2 + (int)((now / 7 + n) % 3));
    parts[i].life = (uint8_t)(12 + n * 4);
    n--;
  }
}

static void spawnWhoosh(int x, int y) {
  int n = 5;
  uint32_t now = millis();
  for (int i = 0; i < MAX_PARTICLES && n > 0; i++) {
    if (parts[i].on) continue;
    parts[i].on = true;
    parts[i].splash = 3;
    int side = (n == 2) ? 0 : ((n & 1) ? 1 : -1);
    parts[i].x = (int16_t)(x + side);
    parts[i].y = (int16_t)(y + (int)(n & 1));
    parts[i].vx = (int16_t)(side * (1 + (int)((now / 11 + n) % 2)));
    parts[i].vy = (int16_t)(1 + (int)((now / 7 + n) % 2));
    parts[i].life = (uint8_t)(8 + n * 3);
    n--;
  }
}

void spawnSparks(int x, int y, int n) {
  Rng r(millis() ^ (x * 31) ^ (y << 5) ^ 0xA5);
  for (int i = 0; i < MAX_PARTICLES && n > 0; i++) {
    if (parts[i].on) continue;
    parts[i].on = true;
    parts[i].splash = 4;
    parts[i].x = (int16_t)(x + r.range(-1, 2));
    parts[i].y = (int16_t)(y + r.range(-2, 1));
    parts[i].vx = (int16_t)r.range(-3, 4);
    parts[i].vy = (int16_t)r.range(-5, 0);
    parts[i].life = (uint8_t)r.range(8, 16);
    n--;
  }
}

void spawnMuzzle(int x, int y) {
  int n = 6;
  uint32_t now = millis();
  for (int i = 0; i < MAX_PARTICLES && n > 0; i++) {
    if (parts[i].on) continue;
    parts[i].on = true;
    parts[i].splash = 5;
    int ox = (int)((now / 7 + n * 3) % 5) - 2;
    int oy = (int)((now / 5 + n * 2) % 3) - 1;
    parts[i].x = (int16_t)(x + ox);
    parts[i].y = (int16_t)(y + oy);
    parts[i].vx = (int16_t)ox;
    parts[i].vy = (int16_t)(oy - 1);
    parts[i].life = (uint8_t)(6 + n);
    n--;
  }
  spawnWhoosh(x, y);
}

void spawnExplosionBurst(int x, int y, int r) {
  int debris = (r >= 10) ? 28 : 18;
  int sparks = (r >= 10) ? 14 : 8;
  spawnParticles(x, y, debris);
  spawnSparks(x, y, sparks);
  spawnSparks(x + 1, y - 1, sparks / 2);
}

void spawnBlood(int x, int y, int dir) {
  if (dir == 0) dir = 1;
  Rng r(millis() ^ (x * 19) ^ (y << 4) ^ 0xB1);
  int n = 10;
  for (int i = 0; i < MAX_PARTICLES && n > 0; i++) {
    if (parts[i].on) continue;
    parts[i].on = true;
    parts[i].splash = 6;
    parts[i].x = (int16_t)(x + r.range(-1, 2));
    parts[i].y = (int16_t)(y + r.range(-2, 2));
    parts[i].vx = (int16_t)(dir * r.range(1, 5) + r.range(-1, 2));
    parts[i].vy = (int16_t)r.range(-4, 2);
    parts[i].life = (uint8_t)r.range(10, 22);
    n--;
  }
}

static uint8_t sKick[MAX_PLAYERS];
static uint32_t sPlaceUntil[MAX_PLAYERS];
static uint8_t sPlaceKind[MAX_PLAYERS];
static uint32_t sThrowUntil[MAX_PLAYERS];

void noteFireKick(uint8_t id) {
  if (id < MAX_PLAYERS) sKick[id] = 8;
}

void notePlaceAnim(uint8_t id, uint8_t kind) {
  if (id >= MAX_PLAYERS) return;
  sPlaceUntil[id] = millis() + 320;
  sPlaceKind[id] = kind;
}

void noteThrowAnim(uint8_t id) {
  if (id < MAX_PLAYERS) sThrowUntil[id] = millis() + 280;
}

static uint8_t sPartSlow;

void tickParticles() {
  sPartSlow = (uint8_t)(sPartSlow + 1);
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (!parts[i].on) continue;
    parts[i].x = (int16_t)(parts[i].x + (parts[i].vx / 2));
    if (parts[i].vx == 1 || parts[i].vx == -1) parts[i].x = (int16_t)(parts[i].x + parts[i].vx);
    parts[i].y = (int16_t)(parts[i].y + (parts[i].vy / 2));
    if (parts[i].vy == 1 || parts[i].vy == -1) parts[i].y = (int16_t)(parts[i].y + parts[i].vy);
    bool grav = false;
    if (parts[i].splash == 1) grav = ((sPartSlow % 3) == 0);
    else if (parts[i].splash == 0) grav = ((sPartSlow & 1) != 0);
    else if (parts[i].splash == 4 && ((sPartSlow % 5) == 0))
      parts[i].vy = (int16_t)(parts[i].vy + 1);
    else if (parts[i].splash == 6) grav = ((sPartSlow % 3) == 0);
    if (grav) parts[i].vy = (int16_t)(parts[i].vy + 1);
    if (parts[i].life == 0) parts[i].on = false;
    else parts[i].life--;
  }
}

static int sCamX = 0, sCamY = 0, sZoom = 1;
static int sZNum = 1, sZDen = 1;
static bool sCamInit = false;

static void setViewZoom(int z) {
  if (z < ZOOM_MIN) z = ZOOM_MIN;
  if (z > ZOOM_MAX) z = ZOOM_MAX;
  sZoom = z;
  static const uint8_t num[3] = {4, 1, 3};
  static const uint8_t den[3] = {7, 1, 2};
  sZNum = num[z];
  sZDen = den[z];
  if (sZNum < 1) sZNum = 1;
  if (sZDen < 1) sZDen = 1;
}

static int viewW() { return OLED_W * sZDen / sZNum; }
static int viewH() { return (OLED_H - HUD_H) * sZDen / sZNum; }
static int zoomPx() { return (sZNum + sZDen - 1) / sZDen; }

static int chaseCam(int cur, int tgt, int div) {
  int d = tgt - cur;
  if (d == 0) return cur;
  int s = d / div;
  if (s == 0) s = d > 0 ? 1 : -1;
  return cur + s;
}

static void drawBar(int x, int y, int w, int fill, int maxv) {
  displayHLine(x, y, w, true);
  displayHLine(x, y + 3, w, true);
  displayVLine(x, y, 4, true);
  displayVLine(x + w - 1, y, 4, true);
  int inner = w - 2;
  int n = maxv <= 0 ? 0 : inner * fill / maxv;
  if (n < 0) n = 0;
  if (n > inner) n = inner;
  displayFillRect(x + 1, y + 1, n, 2, true);
}

static int viewZoom(const Match& m) {
  int z = m.zoom;
  if (z < ZOOM_MIN) z = ZOOM_MIN;
  if (z > ZOOM_MAX) z = ZOOM_MAX;
  return z;
}

static bool mapView() { return sZoom == 0; }

static bool camTrackShot(const Proj& pr) {
  if (!pr.active) return false;
  if (pr.type == PRJ_SENTRY) return false;
  if (pr.type == PRJ_PIPE && pr.vx == 0 && pr.vy == 0) return false;
  return true;
}

static void worldToScreen(int wx, int wy, int* sx, int* sy) {
  *sx = (wx - sCamX) * sZNum / sZDen;
  *sy = HUD_H + (wy - sCamY) * sZNum / sZDen;
}

static void updateCamera(const Match& m) {
  setViewZoom(viewZoom(m));
  int vw = viewW();
  int vh = viewH();
  if (vw < 16) vw = 16;
  if (vh < 12) vh = 12;
  int cx = WORLD_W / 2;
  int cy = WORLD_H / 2;
  bool followShot = false;
  bool deathCam = false;
  bool followKnock = false;
  uint32_t now = millis();
  int deathId = -1;
  if (m.camFocusUntil > now && m.camFocus < m.nPlayers) deathId = (int)m.camFocus;
  else {
    for (int i = 0; i < m.nPlayers; i++) {
      if (m.p[i].alive && m.p[i].dying) deathId = i;
    }
  }
  if (deathId >= 0) {
    deathCam = true;
    if (m.camFocusUntil > now && m.camFocus == (uint8_t)deathId) {
      cx = m.camFocusX;
      cy = m.camFocusY;
    } else {
      cx = m.p[deathId].x;
      cy = m.p[deathId].y - 3;
      if (cy < WORLD_TOP + 6) cy = WORLD_TOP + 6;
    }
  }
  int ox = cx, oy = cy;
  if (m.live && m.selfId < m.nPlayers) {
    ox = m.p[m.selfId].x;
    oy = m.p[m.selfId].y - 3;
  } else if (m.turn < m.nPlayers) {
    ox = m.p[m.turn].x;
    oy = m.p[m.turn].y - 3;
  }
  if (!deathCam) {
    int bestD = -1;
    for (int i = 0; i < MAX_PROJ; i++) {
      const Proj& pr = m.proj[i];
      if (!camTrackShot(pr)) continue;
      if (m.live && pr.owner != m.selfId) continue;
      int px = pr.x >> 8;
      int py = pr.y >> 8;
      int dx = px - ox;
      int dy = py - oy;
      int d = dx * dx + dy * dy;
      if (d > bestD) {
        bestD = d;
        followShot = true;
        cx = px;
        cy = py;
      }
    }
  }
  static int sKnockId = -1;
  static uint32_t sKnockHoldUntil = 0;
  int knockId = -1;
  int knockSpd = 0;
  bool knockPhase = !m.live &&
                    (m.phase == PHASE_EXPLOSION || m.phase == PHASE_FALLING ||
                     m.phase == PHASE_PROJECTILE);
  if (!knockPhase) {
    sKnockId = -1;
  } else {
    for (int i = 0; i < m.nPlayers; i++) {
      if (!m.p[i].alive || m.p[i].dying || m.p[i].bursting) continue;
      int vx = m.p[i].velX;
      int vy = m.p[i].velY;
      if (vx < 0) vx = -vx;
      if (vy < 0) vy = -vy;
      int spd = vx + vy;
      if (m.p[i].knockFrames) spd += 2;
      if (spd < 2) continue;
      if (spd > knockSpd || (spd == knockSpd && i != (int)m.turn)) {
        knockSpd = spd;
        knockId = i;
      }
    }
    if (knockId >= 0) {
      sKnockId = knockId;
      sKnockHoldUntil = now + 450;
    } else if (sKnockId >= 0 && sKnockId < m.nPlayers && m.p[sKnockId].alive &&
               !m.p[sKnockId].dying && now < sKnockHoldUntil) {
      knockId = sKnockId;
    } else {
      sKnockId = -1;
      knockId = -1;
    }
  }
  if (!deathCam && !followShot) {
    if (knockId >= 0) {
      followKnock = true;
      cx = m.p[knockId].x;
      cy = m.p[knockId].y - 3;
    } else if (m.phase == PHASE_EXPLOSION && !m.live) {
      cx = m.explodeX;
      cy = m.explodeY;
    } else if (m.phase == PHASE_TARGET) {
      uint8_t pid = m.live ? m.selfId : m.turn;
      if (pid < m.nPlayers && m.p[pid].weapon == WPN_HOMING &&
          m.homeTarget < m.nPlayers && m.p[m.homeTarget].alive) {
        cx = m.p[m.homeTarget].x;
        cy = m.p[m.homeTarget].y - 3;
      } else {
        cx = m.cursorX;
        int sy = terrainStandY(m.terrain, m.cursorX);
        cy = sy >= 0 ? sy - 4 : WORLD_H / 2;
      }
    } else if (m.live && m.selfId < m.nPlayers && m.p[m.selfId].alive) {
      cx = m.p[m.selfId].x;
      cy = m.p[m.selfId].y - 3;
    } else if (m.turn < m.nPlayers && m.p[m.turn].alive) {
      cx = m.p[m.turn].x;
      cy = m.p[m.turn].y - 3;
    } else {
      for (int i = 0; i < m.nPlayers; i++) {
        if (m.p[i].alive) {
          cx = m.p[i].x;
          cy = m.p[i].y - 3;
          break;
        }
      }
    }
  }
  int tx = cx - vw / 2;
  int ty = cy - vh / 2;
  if (vw >= WORLD_W) tx = 0;
  else {
    if (tx < 0) tx = 0;
    if (tx > WORLD_W - vw) tx = WORLD_W - vw;
  }
  bool skyFollow = m.mapKind != MAP_CAVERN;
  if (ty < SKY_MIN_Y && skyFollow) ty = SKY_MIN_Y;
  if (ty < 0 && !skyFollow) ty = 0;
  if (vh >= WORLD_H) {
    if (!skyFollow || ty > 0) ty = 0;
  } else if (ty > WORLD_H - vh) {
    ty = WORLD_H - vh;
  }
  if (!sCamInit) {
    sCamX = tx;
    sCamY = ty;
    sCamInit = true;
  } else {
    static bool returning;
    static bool actionCam;
    if (deathCam) returning = true;
    int dx = tx - sCamX;
    int dy = ty - sCamY;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    bool jump = !returning && !actionCam && (dx > vw || dy > vh);
    if (deathCam || followShot || followKnock) actionCam = true;
    int div = returning ? 3 : ((followShot || followKnock || actionCam) ? 2 : ((skyFollow && ty < 8) ? 2 : 4));
    if (jump) {
      sCamX = tx;
      sCamY = ty;
    } else {
      sCamX = chaseCam(sCamX, tx, div);
      sCamY = chaseCam(sCamY, ty, div);
    }
    if (returning && !deathCam && dx <= 2 && dy <= 2) returning = false;
    if (actionCam && !deathCam && !followShot && !followKnock && dx <= 2 && dy <= 2)
      actionCam = false;
  }
  if (sZDen > sZNum) {
    int camSX = (sCamX * sZNum) / sZDen;
    int camSY = (sCamY * sZNum) / sZDen;
    sCamX = (camSX * sZDen) / sZNum;
    sCamY = (camSY * sZDen) / sZNum;
  }
}

static void worldSet(int wx, int wy, bool on) {
  int sx0 = (wx - sCamX) * sZNum / sZDen;
  int sy0 = HUD_H + (wy - sCamY) * sZNum / sZDen;
  int sx1 = (wx + 1 - sCamX) * sZNum / sZDen;
  int sy1 = HUD_H + (wy + 1 - sCamY) * sZNum / sZDen;
  int w = sx1 - sx0;
  int h = sy1 - sy0;
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  if (w == 1 && h == 1) displaySet(sx0, sy0, on);
  else displayFillRect(sx0, sy0, w, h, on);
}

static void worldLine(int x0, int y0, int x1, int y1) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    worldSet(x0, y0, true);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

static void drawAimTip(int wx, int wy) {
  worldSet(wx, wy, true);
  worldSet(wx - 1, wy, true);
  worldSet(wx + 1, wy, true);
  worldSet(wx, wy - 1, true);
  worldSet(wx, wy + 1, true);
  if (mapView()) {
    int sx, sy;
    worldToScreen(wx, wy, &sx, &sy);
    displaySet(sx, sy, true);
    displaySet(sx - 1, sy, true);
    displaySet(sx + 1, sy, true);
    displaySet(sx, sy - 1, true);
    displaySet(sx, sy + 1, true);
  }
}

// 7x6 fat toad. Bit 0 = left. Facing-right art; flipped at blit time.
// 0 idle, 1 stride A, 2 pass, 3 stride B, 4 jump, 5 idle squat
static const uint8_t WORM_FR[6][6] = {
  { 0x3E, 0x7F, 0x5D, 0x7F, 0x3E, 0x22 },
  { 0x3E, 0x7F, 0x5D, 0x7F, 0x3E, 0x41 },
  { 0x00, 0x3E, 0x7F, 0x5D, 0x3E, 0x14 },
  { 0x3E, 0x7F, 0x5D, 0x7F, 0x3E, 0x21 },
  { 0x3E, 0x7F, 0x5D, 0x3E, 0x1C, 0x22 },
  { 0x00, 0x3E, 0x7F, 0x5D, 0x7F, 0x36 },
};

static uint8_t sWalkFr[MAX_PLAYERS];
static uint32_t sWalkMs[MAX_PLAYERS];
static int16_t sWalkX[MAX_PLAYERS];
static uint32_t sLastStep[MAX_PLAYERS];
static uint8_t sWalkOn[MAX_PLAYERS];

static uint8_t wormFrame(const Player& pl, uint8_t i, bool grounded) {
  if (mapView()) return 0;
  uint32_t now = millis();
  if (!sWalkOn[i]) {
    sWalkX[i] = pl.x;
    sWalkOn[i] = 1;
  }
  if (pl.x != sWalkX[i]) {
    sLastStep[i] = now;
    sWalkX[i] = pl.x;
  }
  if (!grounded) {
    sWalkFr[i] = 4;
    sWalkMs[i] = now;
    return 4;
  }
  bool walking = (now - sLastStep[i]) < (uint32_t)(MOVE_MS_PER_PX + 50);
  if (walking) {
    if (now - sWalkMs[i] >= WALK_FRAME_MS) {
      sWalkMs[i] = now;
      sWalkFr[i] = (uint8_t)((sWalkFr[i] + 1) & 3);
    }
    return sWalkFr[i];
  }
  sWalkFr[i] = ((now / 480) & 1) ? 5 : 0;
  return sWalkFr[i];
}

static void drawWormMap(int x, int y, uint8_t id, int facing) {
  int sx, sy;
  worldToScreen(x, y, &sx, &sy);
  const uint8_t rows[4] = { 0x0E, 0x1F, 0x1B, 0x0A };
  const int w = 5, h = 4, ox = 2;
  for (int r = 0; r < h; r++) {
    for (int c = 0; c < w; c++) {
      if (!(rows[r] & (1 << c))) continue;
      int col = facing > 0 ? c : (w - 1 - c);
      displaySet(sx + col - ox, sy - (h - 1) + r, true);
    }
  }
  if (id == 1) displaySet(sx, sy - h, true);
  else if (id == 2) {
    displaySet(sx - 1, sy - h, true);
    displaySet(sx + 1, sy - h, true);
  } else if (id == 3) {
    int ear = facing > 0 ? 2 : -2;
    displaySet(sx + ear, sy - 2, true);
  }
}

static void drawWorm(int x, int y, uint8_t id, int facing, bool blink, bool recoil, uint8_t hp,
                     bool punch, uint8_t frame) {
  if (mapView()) {
    drawWormMap(x, y, id, facing);
    return;
  }
  int bx = x + (recoil ? -facing : 0);
  int by = y;
  if (frame > 5) frame = 0;
  const uint8_t* rows = WORM_FR[frame];
  const int w = 7, h = 6, ox = 3;
  auto pxAt = [&](int r, int c) {
    int col = facing > 0 ? c : (w - 1 - c);
    return bx + col - ox;
  };
  auto pyAt = [&](int r) { return by - (h - 1) + r; };
  for (int r = 0; r < h; r++) {
    uint8_t bits = rows[r];
    for (int c = 0; c < w; c++) {
      if (!(bits & (1 << c))) continue;
      int px = pxAt(r, c), py = pyAt(r);
      worldSet(px - 1, py, false);
      worldSet(px + 1, py, false);
      worldSet(px, py - 1, false);
      worldSet(px, py + 1, false);
    }
  }
  for (int r = 0; r < h; r++) {
    uint8_t bits = rows[r];
    for (int c = 0; c < w; c++) {
      if (!(bits & (1 << c))) continue;
      worldSet(pxAt(r, c), pyAt(r), true);
    }
  }
  int hatY = by - h;
  if (id == 1) {
    worldSet(bx, hatY, false);
    worldSet(bx, hatY - 1, false);
    worldSet(bx, hatY, true);
    worldSet(bx, hatY - 1, true);
  } else if (id == 2) {
    worldSet(bx - 1, hatY, false);
    worldSet(bx, hatY, false);
    worldSet(bx + 1, hatY, false);
    worldSet(bx - 1, hatY, true);
    worldSet(bx, hatY, true);
    worldSet(bx + 1, hatY, true);
  } else if (id == 3) {
    int ear = facing > 0 ? 2 : -2;
    worldSet(bx - ear, by - 4, false);
    worldSet(bx + ear, by - 4, false);
    worldSet(bx - ear, by - 4, true);
    worldSet(bx + ear, by - 4, true);
  }
  if (punch) {
    int dir = facing > 0 ? 1 : -1;
    int hx = bx + dir * 3;
    int hy = by - 3;
    worldSet(bx + dir, by - 3, true);
    worldSet(bx + dir * 2, by - 3, true);
    worldSet(hx, hy, true);
    worldSet(hx + dir, hy, true);
    worldSet(hx, hy - 1, true);
    worldSet(hx + dir, hy - 1, true);
  }
  if (mapView()) return;
  int sx, sy;
  worldToScreen(bx, by, &sx, &sy);
  int barW = 11;
  int barX = sx - 5;
  int barY = sy - 14 - zoomPx();
  if (barY < HUD_H) barY = HUD_H;
  displayFillRect(barX, barY, barW, 4, false);
  displayHLine(barX, barY, barW, true);
  displayHLine(barX, barY + 3, barW, true);
  displayVLine(barX, barY, 4, true);
  displayVLine(barX + barW - 1, barY, 4, true);
  int inner = barW - 2;
  int n = HP_MAX <= 0 ? 0 : inner * hp / HP_MAX;
  if (n > 0) displayFillRect(barX + 1, barY + 1, n, 2, true);
  if (blink) displaySet(sx + 1, barY - 1, true);
}

static void drawBatSprite(int bx, int by, int facing, bool swing, uint32_t age) {
  auto iround = [](float v) { return (int)(v >= 0.f ? v + 0.5f : v - 0.5f); };
  int dir = facing >= 0 ? 1 : -1;
  float deg;
  if (swing) {
    float t = (float)age / (float)BAT_MS;
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    float u;
    if (t < 0.10f) u = t / 0.10f * 0.04f;
    else if (t < 0.38f) u = 0.04f + (t - 0.10f) / 0.28f * 0.90f;
    else u = 0.94f + (t - 0.38f) / 0.62f * 0.06f;
    deg = 170.f - u * 185.f;
  } else {
    bool bob = ((millis() / 220) & 1) != 0;
    deg = 165.f + (bob ? 8.f : 0.f);
  }
  float rad = deg * 3.14159265f / 180.f;
  float c = cosf(rad);
  float s = sinf(rad);
  if (dir < 0) c = -c;
  int x0 = bx - dir;
  int y0 = by - 4;
  int len = swing ? 11 : 8;
  int x1 = x0 + iround(c * (float)len);
  int y1 = y0 - iround(s * (float)len);
  worldLine(x0, y0, x1, y1);
  worldLine(x0, y0 - 1, x1, y1 - 1);
  int hx = x1 + iround(c);
  int hy = y1 - iround(s);
  worldSet(hx, hy, true);
  worldSet(hx + dir, hy, true);
  worldSet(hx, hy - 1, true);
  worldSet(hx + dir, hy - 1, true);
  worldSet(x1, y1, true);
  if (swing && age > BAT_MS / 10 && age < (BAT_MS * 2) / 5) {
    worldSet(hx + dir, hy - 1, true);
    worldSet(hx, hy - 2, true);
    worldSet(x1 + dir, y1, true);
    int tx = x1 + iround(c * 2.f);
    int ty = y1 - iround(s * 2.f);
    worldSet(tx, ty, true);
  }
}

static void drawGunSprite(int bx, int by, int facing, int16_t aim, bool charge, bool fire) {
  auto iround = [](float v) { return (int)(v >= 0.f ? v + 0.5f : v - 0.5f); };
  float rad = aim * 3.14159265f / 180.f;
  int wob = 0;
  if (charge) wob = ((millis() / 45) & 1) ? 1 : 0;
  int x0 = bx + (facing > 0 ? 1 : -1);
  int y0 = by - 4 + (charge ? wob : 0);
  int len = charge ? 8 : 6;
  int x1 = x0 + iround(cosf(rad) * (float)len);
  int y1 = y0 - iround(sinf(rad) * (float)len);
  worldLine(x0, y0, x1, y1);
  int ox = iround(-sinf(rad));
  int oy = iround(-cosf(rad));
  if (ox == 0 && oy == 0) oy = 1;
  worldLine(x0 + ox, y0 + oy, x1 + ox, y1 + oy);
  worldSet(x1, y1, true);
  worldSet(x1 + ox, y1 + oy, true);
  if (charge) {
    bool flick = ((millis() / 70) & 1) != 0;
    worldSet(x1 + iround(cosf(rad)), y1 - iround(sinf(rad)), flick);
    worldSet(x0 - facing, y0 + 1, true);
    static uint32_t chargePuff;
    if (millis() - chargePuff > 100) {
      chargePuff = millis();
      spawnSparks(x1, y1, 2);
    }
  }
  if (fire) {
    worldSet(x1, y1, true);
    worldSet(x1 + 1, y1, true);
    worldSet(x1 - 1, y1, true);
    worldSet(x1, y1 - 1, true);
    worldSet(x1, y1 + 1, true);
    worldSet(x1 + iround(cosf(rad) * 2.f), y1 - iround(sinf(rad) * 2.f), true);
  }
}

static void drawAkSprite(int bx, int by, int facing, int16_t aim, bool fire) {
  auto iround = [](float v) { return (int)(v >= 0.f ? v + 0.5f : v - 0.5f); };
  float rad = aim * 3.14159265f / 180.f;
  int dir = facing > 0 ? 1 : -1;
  int kick = fire ? -dir : 0;
  int x0 = bx + dir + kick;
  int y0 = by - 4;
  int x1 = x0 + iround(cosf(rad) * 9.f);
  int y1 = y0 - iround(sinf(rad) * 9.f);
  worldLine(x0, y0, x1, y1);
  int ox = iround(-sinf(rad));
  int oy = iround(-cosf(rad));
  if (ox == 0 && oy == 0) oy = 1;
  worldLine(x0 + ox, y0 + oy, x1 + ox, y1 + oy);
  worldSet(x0 - dir, y0, true);
  worldSet(x0 - dir * 2, y0 + 1, true);
  worldSet(x0 - dir * 2, y0, true);
  int mx = x0 + iround(cosf(rad) * 2.f);
  int my = y0 - iround(sinf(rad) * 2.f);
  worldSet(mx, my + 1, true);
  worldSet(mx, my + 2, true);
  worldSet(mx + dir, my + 2, true);
  worldSet(mx + dir, my + 3, true);
  worldSet(x1, y1, true);
  if (fire) {
    bool flick = ((millis() / 30) & 1) != 0;
    worldSet(x1 + iround(cosf(rad)), y1 - iround(sinf(rad)), true);
    worldSet(x1 + iround(cosf(rad) * 2.f), y1 - iround(sinf(rad) * 2.f), flick);
    worldSet(x1, y1 - 1, flick);
    worldSet(x1, y1 + 1, !flick);
  }
}

static void drawPipeHeld(int bx, int by, int facing, int16_t aim, bool throwing, uint32_t age) {
  auto iround = [](float v) { return (int)(v >= 0.f ? v + 0.5f : v - 0.5f); };
  int dir = facing > 0 ? 1 : -1;
  float deg = (float)aim;
  if (throwing) {
    float t = (float)age / 280.f;
    if (t > 1.f) t = 1.f;
    float u = t < 0.3f ? t / 0.3f * 0.15f : 0.15f + (t - 0.3f) / 0.7f * 0.85f;
    deg = (float)aim + 50.f - u * 70.f;
  }
  float rad = deg * 3.14159265f / 180.f;
  float c = cosf(rad);
  float s = sinf(rad);
  int x0 = bx + dir;
  int y0 = by - 2;
  int x1 = x0 + iround(c * 6.f);
  int y1 = y0 - iround(s * 6.f);
  worldLine(x0, y0, x1, y1);
  int ox = iround(-s);
  int oy = iround(-c);
  if (ox == 0 && oy == 0) oy = 1;
  worldSet(x1, y1, true);
  worldSet(x1 + ox, y1 + oy, true);
  worldSet(x1 + dir, y1, true);
  worldSet(bx + dir, by - 3, true);
  worldSet(bx + dir, by - 1, true);
  if (throwing && age < 160) {
    worldSet(x1 + iround(c), y1 - iround(s), true);
    worldSet(x1 + dir, y1 - 1, true);
  }
}

static void drawCarryCharge(int bx, int by, int facing, bool dynamite, bool placing) {
  int dir = facing > 0 ? 1 : -1;
  int panic = ((millis() / 55) & 1) ? 1 : 0;
  int hx = bx + dir * (placing ? 2 : 3);
  int hy = by - (placing ? 1 : 2);
  if (!placing) {
    hx += ((millis() / 80) & 1) ? 0 : -dir;
    hy += panic;
  }
  worldSet(bx + dir, by - 3, true);
  worldSet(bx + dir, by - 2, true);
  worldSet(bx + dir * 2, by - 2, true);
  worldSet(bx - dir, by - 3, true);
  worldSet(bx, by - 2, true);
  worldSet(hx, hy, true);
  worldSet(hx + dir, hy, true);
  worldSet(hx, hy - 1, true);
  worldSet(hx + dir, hy - 1, true);
  worldSet(hx - 1, hy, true);
  if (dynamite) {
    worldSet(hx, hy - 2, true);
    worldSet(hx + dir, hy - 2, true);
    bool spark = ((millis() / 70) & 1) != 0;
    worldSet(hx, hy - 3, spark);
    worldSet(hx + dir, hy - 3, !spark);
    worldSet(hx, hy - 4, spark);
  } else {
    worldSet(hx + 1, hy, true);
    worldSet(hx + dir - 1, hy - 1, true);
  }
  if (placing) {
    worldSet(hx, hy + 1, true);
    worldSet(bx - dir, by, true);
  }
}

static void drawMissile(int px, int py, int vx, int vy) {
  int avx = vx < 0 ? -vx : vx;
  int avy = vy < 0 ? -vy : vy;
  int tx = 0, ty = 0;
  if (avx * 2 >= avy * 3) tx = vx >= 0 ? 1 : -1;
  else if (avy * 2 >= avx * 3) ty = vy >= 0 ? 1 : -1;
  else {
    tx = vx >= 0 ? 1 : -1;
    ty = vy >= 0 ? 1 : -1;
  }
  if (tx == 0 && ty == 0) tx = 1;
  int fx = -ty;
  int fy = tx;
  worldSet(px + tx * 2, py + ty * 2, true);
  worldSet(px + tx, py + ty, true);
  worldSet(px, py, true);
  worldSet(px - tx, py - ty, true);
  worldSet(px - tx * 2, py - ty * 2, true);
  worldSet(px - tx + fx, py - ty + fy, true);
  worldSet(px - tx - fx, py - ty - fy, true);
  worldSet(px - tx * 2 + fx, py - ty * 2 + fy, true);
  worldSet(px - tx * 2 - fx, py - ty * 2 - fy, true);
  if (((millis() / 40) & 1) != 0)
    worldSet(px - tx * 3, py - ty * 3, true);
}

static void drawTorchHeld(int bx, int by, int facing, int16_t aim, bool digging) {
  auto iround = [](float v) { return (int)(v >= 0.f ? v + 0.5f : v - 0.5f); };
  float rad = aim * 3.14159265f / 180.f;
  float c = cosf(rad);
  float s = sinf(rad);
  int dir = facing > 0 ? 1 : -1;
  int x0 = bx + dir;
  int y0 = by - 4;
  int x1 = x0 + iround(c * 5.f);
  int y1 = y0 - iround(s * 5.f);
  worldLine(x0, y0, x1, y1);
  worldSet(bx + dir, by - 3, true);
  worldSet(x0, y0 + 1, true);
  worldSet(x1, y1, true);
  int nx = x1 + iround(c);
  int ny = y1 - iround(s);
  worldSet(nx, ny, true);
  uint32_t t = millis();
  bool f1 = ((t / 40) & 1) != 0;
  bool f2 = ((t / 55) & 1) != 0;
  bool f3 = ((t / 28) & 1) != 0;
  worldSet(nx + iround(c), ny - iround(s), f1);
  worldSet(nx + iround(c * 2.f), ny - iround(s * 2.f), f2);
  if (digging) {
    worldSet(nx + iround(c * 3.f), ny - iround(s * 3.f), f3);
    worldSet(nx + iround(c * 2.f) + iround(-s), ny - iround(s * 2.f) - iround(c), f3);
    worldSet(nx + iround(c * 2.f) - iround(-s), ny - iround(s * 2.f) + iround(c), f1);
    worldSet(nx + iround(c * 4.f), ny - iround(s * 4.f), f3);
  }
}

static void drawWind(int x, int y, int8_t wind, bool right) {
  char s[10];
  memset(s, ' ', 8);
  s[8] = 0;
  int mag = wind < 0 ? -wind : wind;
  if (mag > 4) mag = 4;
  bool flash = ((millis() / 180) & 1) != 0;
  if (wind == 0) {
    memcpy(s + 2, "WIND", 4);
  } else if (wind > 0) {
    memcpy(s, "WIND", 4);
    if (flash) {
      for (int i = 0; i < mag; i++) s[4 + i] = '>';
    }
  } else {
    memcpy(s + 4, "WIND", 4);
    if (flash) {
      int start = 4 - mag;
      for (int i = 0; i < mag; i++) s[start + i] = '<';
    }
  }
  if (right) x -= displayTextWidth(s);
  if (x < 56) x = 56;
  displayText(x, y, s, true);
}

static void drawHpChip(int i, int x, int y, const Match& m, bool right) {
  uint8_t hp = (i < m.nPlayers && m.p[i].alive) ? m.p[i].hp : 0;
  char buf[10];
  snprintf(buf, sizeof(buf), "%u:%u", (unsigned)(i + 1), (unsigned)hp);
  int tw = displayTextWidth(buf);
  int barW = 18;
  int total = tw + 3 + barW;
  if (right) x -= total;
  displayText(x, y, buf, true);
  drawBar(x + tw + 2, y + 1, barW, hp, HP_MAX);
  if (m.live && i < m.nPlayers) {
    uint8_t lv = m.p[i].alive ? m.p[i].lives : 0;
    int pipX = x + tw + 2;
    for (int k = 0; k < lv && k < LIVES_MAX; k++)
      displayFillRect(pipX + k * 4, y + 6, 3, 1, true);
  }
  if (m.live ? (i == m.selfId) : (i == m.turn))
    displayFillRect(x - 1, y + 7, total + 1, 1, true);
}

static bool terrainRim(const Match& m, int x, int y) {
  if (y < 0 || y >= m.waterY) return false;
  if (x < 0 || x >= WORLD_W) return false;
  if (!terrainSolid(m.terrain, x, y)) return false;
  bool edge = !terrainSolid(m.terrain, x - 1, y) || !terrainSolid(m.terrain, x + 1, y) ||
              !terrainSolid(m.terrain, x, y - 1) || !terrainSolid(m.terrain, x, y + 1);
  bool crust = !terrainSolid(m.terrain, x, y - 1) || !terrainSolid(m.terrain, x, y - 2);
  return edge || crust;
}

static bool terrainLit(const Match& m, int x, int y, int phase) {
  if (y < 0) return false;
  if (y >= m.waterY) return ((x + phase + (y & 1) * 2) & 3) == 0;
  if (!terrainSolid(m.terrain, x, y)) return false;
  if (terrainRim(m, x, y)) return true;
  return ((x + y * 3) & 3) == 0;
}

static void drawTerrain() {
  const Match& m = gameMatch();
  uint32_t t = millis();
  int phase = (t / 180) & 1;
  const int playH = OLED_H - HUD_H;
  const Terrain& ter = m.terrain;
  const int waterY = m.waterY;
  auto solid = [&](int x, int y) -> bool {
    if ((unsigned)x >= (unsigned)WORLD_W) return true;
    if ((unsigned)y >= (unsigned)WORLD_H) return false;
    return (ter.bits[x + (y >> 3) * WORLD_W] & (1 << (y & 7))) != 0;
  };
  auto rim = [&](int x, int y) -> bool {
    if (y < 0 || y >= waterY) return false;
    if ((unsigned)x >= (unsigned)WORLD_W) return false;
    if (!solid(x, y)) return false;
    return !solid(x - 1, y) || !solid(x + 1, y) || !solid(x, y - 1) || !solid(x, y + 1) ||
           !solid(x, y - 2);
  };
  if (sZDen > sZNum) {
    int camSX = (sCamX * sZNum) / sZDen;
    int camSY = (sCamY * sZNum) / sZDen;
    for (int sy = 0; sy < playH; sy++) {
      for (int sx = 0; sx < OLED_W; sx++) {
        int x = sCamX + (sx * sZDen) / sZNum;
        int y = sCamY + (sy * sZDen) / sZNum;
        bool on = false;
        if (x >= 0 && x < WORLD_W) {
          if (y >= waterY) {
            on = (((sx + camSX) + (sy + camSY) * 3) & 3) == 0;
          } else if (solid(x, y)) {
            if (!solid(x - 1, y) || !solid(x + 1, y) || !solid(x, y - 1) || !solid(x, y + 1) ||
                !solid(x, y - 2))
              on = true;
            else
              on = (((sx + camSX) + (sy + camSY) * 3) & 3) == 0;
          } else if (rim(x + 1, y) || rim(x, y + 1) || rim(x + 1, y + 1)) {
            on = true;
          }
        }
        if (on) displaySet(sx, HUD_H + sy, true);
      }
    }
    return;
  }
  for (int sy = 0; sy < playH; sy++) {
    for (int sx = 0; sx < OLED_W; sx++) {
      int x = sCamX + (sx * sZDen) / sZNum;
      int y = sCamY + (sy * sZDen) / sZNum;
      bool on = false;
      if (x >= 0 && x < WORLD_W) on = terrainLit(m, x, y, phase);
      if (on) displaySet(sx, HUD_H + sy, true);
    }
  }
}

static void drawPlayfield() {
  const Match& m = gameMatch();
  updateCamera(m);
  drawTerrain();
  bool blink = ((millis() / 280) & 1) != 0;
  for (int i = 0; i < m.nPlayers; i++) {
    const Player& pl = m.p[i];
    if (!pl.alive || pl.bursting) continue;
    if (pl.hp == 0 && pl.y + 1 >= m.waterY) continue;
    PlayPhase ph = m.live ? (PlayPhase)pl.act : ((i == (int)m.turn) ? m.phase : PHASE_CONTROL);
    uint32_t poseAt = m.live ? pl.actAt : m.phaseAt;
    uint32_t poseAge = millis() - poseAt;
    bool rec = sKick[i] > 0;
    if (sKick[i]) sKick[i]--;
    bool molli = (ph == PHASE_MOLLI);
    bool windup = molli && poseAge < MOLLI_WINDUP_MS && !m.cheat;
    bool punch = molli && !windup;
    bool batSwing = (ph == PHASE_BAT);
    bool placing = millis() < sPlaceUntil[i];
    bool throwing = millis() < sThrowUntil[i];
    uint32_t throwAge = throwing ? (280 - (sThrowUntil[i] - millis())) : 0;
    bool dyingBlink = pl.dying && ((millis() / 120) & 1);
    bool grounded = terrainGrounded(m.terrain, pl.x, pl.y);
    uint8_t fr = wormFrame(pl, (uint8_t)i, grounded);
    if (windup) {
      rec = true;
      fr = 5;
    }
    if (ph == PHASE_CHARGING && (pl.weapon == WPN_BAZOOKA || pl.weapon == WPN_HOMING)) {
      fr = 5;
      rec = rec || ((millis() / 50) & 1) != 0;
    }
    if (batSwing) fr = (poseAge < BAT_MS / 3) ? 5 : 4;
    if (placing) fr = 5;
    if (throwing) fr = (throwAge < 90) ? 5 : 4;
    if (punch) fr = 4;
    bool carryMine = pl.weapon == WPN_MINE && ph != PHASE_WEAPON_MENU && !pl.dying;
    bool carryDyn = pl.weapon == WPN_DYNAMITE && ph != PHASE_WEAPON_MENU && !pl.dying;
    if ((carryMine || carryDyn) && !placing) {
      fr = 5;
      rec = rec || ((millis() / 70) & 1) != 0;
    }
    drawWorm(pl.x, pl.y, (uint8_t)i, pl.facing,
             (blink && ((m.live && i == (int)m.selfId) || (!m.live && m.turn == i))) || dyingBlink,
             rec, pl.hp, punch, fr);
    int gx = pl.x + (rec ? -pl.facing : 0);
    int gy = pl.y;
    bool holdBat = (pl.weapon == WPN_BAT && ph != PHASE_WEAPON_MENU && !pl.dying && !placing);
    if (holdBat || batSwing) drawBatSprite(gx, gy, pl.facing, batSwing, poseAge);
    bool gunWpn = (pl.weapon == WPN_BAZOOKA || pl.weapon == WPN_HOMING);
    bool gunPose = gunWpn && (ph == PHASE_CONTROL || ph == PHASE_CHARGING || ph == PHASE_JETPACK);
    bool gunCharge = gunWpn && ph == PHASE_CHARGING;
    bool gunFire = gunWpn && rec && (ph == PHASE_PROJECTILE || ph == PHASE_CONTROL ||
                                    ph == PHASE_JETPACK || ph == PHASE_EXPLOSION);
    if ((gunPose || gunFire))
      drawGunSprite(gx, gy, pl.facing, pl.aim, gunCharge, gunFire);
    bool akOn = (pl.weapon == WPN_DRACO && ph != PHASE_WEAPON_MENU && !pl.dying) ||
                (ph == PHASE_DRACO);
    if (akOn)
      drawAkSprite(gx, gy, pl.facing, pl.aim, ph == PHASE_DRACO);
    bool pipeHold = pl.weapon == WPN_PIPE && ph != PHASE_WEAPON_MENU && !pl.dying &&
                    ph != PHASE_PROJECTILE;
    if (throwing)
      drawPipeHeld(gx, gy, pl.facing, pl.aim, true, throwAge);
    else if (pipeHold)
      drawPipeHeld(gx, gy, pl.facing, pl.aim, false, 0);
    if ((carryMine || carryDyn || placing)) {
      bool dyn = placing ? (sPlaceKind[i] != 0) : carryDyn;
      drawCarryCharge(gx, gy, pl.facing, dyn, placing);
    }
    bool torchHold = pl.weapon == WPN_BLOWTORCH && ph != PHASE_WEAPON_MENU && !pl.dying;
    if (torchHold || ph == PHASE_BLOWTORCH)
      drawTorchHeld(gx, gy, pl.facing, pl.aim, ph == PHASE_BLOWTORCH);
    if (windup && !mapView()) {
      bool dust = ((millis() / 50) & 1) != 0;
      worldSet(gx - pl.facing * 2, gy + 1, dust);
      worldSet(gx - pl.facing, gy + 1, true);
      worldSet(gx + pl.facing, gy, true);
    }
    if (punch && !mapView()) {
      int dir = pl.facing > 0 ? 1 : -1;
      worldSet(gx - dir, gy, true);
      worldSet(gx + dir * 2, gy - 1, true);
      worldSet(gx + dir * 3, gy - 1, true);
      worldSet(gx + dir * 4, gy - 2, true);
      worldSet(gx + dir * 4, gy - 3, ((millis() / 40) & 1) != 0);
    }
    if (mapView()) {
      int sx, sy;
      worldToScreen(pl.x, pl.y - 6, &sx, &sy);
      char tag[3] = {'P', (char)('1' + i), 0};
      int tw = 10;
      int tx = sx - tw / 2;
      int ty = sy - 12;
      if (ty < HUD_H) ty = HUD_H;
      if (tx < 0) tx = 0;
      if (tx > OLED_W - tw) tx = OLED_W - tw;
      displayFillRect(tx - 1, ty - 1, tw + 2, 9, false);
      displayChar(tx, ty, tag[0], true);
      displayChar(tx + 5, ty, tag[1], true);
    }
    bool jetOn = m.live ? (pl.act == PHASE_JETPACK)
                        : (m.phase == PHASE_JETPACK && (int)m.turn == i);
    uint8_t held = pl.held;
    if (!m.live && i == m.turn) held = m.heldButtons;
    bool jetThrust = jetOn && (held & (1 << BTN_AIM_UP)) != 0;
    bool jetDive = jetOn && !jetThrust && (held & (1 << BTN_AIM_DN)) != 0;
    bool jetFly = jetOn && (!grounded || jetThrust || jetDive);
    if (jetOn) {
      int back = pl.facing > 0 ? -1 : 1;
      worldSet(gx + back, gy - 4, true);
      worldSet(gx + back * 2, gy - 4, true);
      worldSet(gx + back * 3, gy - 3, true);
      worldSet(gx + back * 4, gy - 3, true);
      worldSet(gx + back * 3, gy - 2, true);
      worldSet(gx + back * 4, gy - 2, true);
      worldSet(gx + back * 3, gy - 1, true);
      worldSet(gx + back * 4, gy - 1, true);
      worldSet(gx + back * 2, gy - 3, true);
      worldSet(gx + back * 3, gy, true);
      worldSet(gx + back * 4, gy, true);
    }
    if (jetFly) {
      int back = pl.facing > 0 ? -1 : 1;
      int ax = gx + back * 3;
      int ay = gy + 1;
      uint32_t t = millis();
      bool flick = ((t / 35) & 1) != 0;
      bool flick2 = ((t / 55) & 1) != 0;
      worldSet(ax, ay, true);
      worldSet(ax + back, ay, true);
      worldSet(ax, ay + 1, flick);
      worldSet(ax + back, ay + 1, flick2);
      worldSet(ax, ay + 2, true);
      worldSet(ax + back, ay + 2, flick);
      worldSet(ax, ay + 3, flick2);
      if (jetThrust) {
        worldSet(ax, ay + 4, true);
        worldSet(ax + back, ay + 3, flick);
        worldSet(ax, ay + 5, flick);
      }
      static uint32_t jetPuffAt[MAX_PLAYERS];
      uint32_t gap = jetThrust ? 22u : 40u;
      if (t - jetPuffAt[i] >= gap) {
        jetPuffAt[i] = t;
        spawnJetPuff(ax, ay + 1, 0);
        if (jetThrust) spawnJetPuff(ax, ay + 2, 0);
      }
    }
    if (!jetOn && !grounded && pl.jumpsLeft == 0) {
      uint32_t t = millis();
      bool flick = ((t / 40) & 1) != 0;
      bool flick2 = ((t / 70) & 1) != 0;
      int ax = pl.x;
      int ay = pl.y + 1;
      worldSet(ax, ay, flick);
      worldSet(ax - 1, ay + 1, true);
      worldSet(ax + 1, ay + 1, true);
      worldSet(ax - 2, ay + 2, flick);
      worldSet(ax + 2, ay + 2, flick2);
      worldSet(ax, ay + 2, !flick);
      worldSet(ax - 1, ay + 3, flick2);
      worldSet(ax + 1, ay + 3, flick);
      static uint32_t whooshAt[MAX_PLAYERS];
      if (t - whooshAt[i] >= 48u) {
        whooshAt[i] = t;
        spawnWhoosh(ax, ay + 1);
      }
    }
    if (!mapView() && m.turn == i && millis() < m.turnArrowUntil && ((millis() / 180) & 1)) {
      int ax, ay;
      worldToScreen(pl.x, pl.y - 12, &ax, &ay);
      ax -= 2;
      if (ay < HUD_H) ay = HUD_H;
      displayChar(ax, ay, 'v', true);
    }
    if ((m.phase == PHASE_TARGET || m.homeLocked) &&
        ((m.live && m.selfId < m.nPlayers && m.p[m.selfId].weapon == WPN_HOMING) ||
         (!m.live && m.turn < m.nPlayers && m.p[m.turn].weapon == WPN_HOMING)) &&
        i == m.homeTarget) {
      bool blink = m.homeLocked ? (((millis() / 160) & 1) != 0) : true;
      if (blink) {
        int sx, sy;
        worldToScreen(pl.x, pl.y - 4, &sx, &sy);
        int pulse = ((millis() / 140) & 1) ? 0 : 1;
        int L = sx - 8 - pulse, R = sx + 8 + pulse;
        int T = sy - 10 - pulse, B = sy + 6 + pulse;
        displayHLine(L, T, 4, true);
        displayVLine(L, T, 4, true);
        displayHLine(R - 3, T, 4, true);
        displayVLine(R, T, 4, true);
        displayHLine(L, B, 4, true);
        displayVLine(L, B - 3, 4, true);
        displayHLine(R - 3, B, 4, true);
        displayVLine(R, B - 3, 4, true);
        displaySet(sx, T - 1, true);
      }
    }
  }
  for (int i = 0; i < m.nPlayers; i++) {
    if (m.dmgUntil[i] <= millis() || m.dmgAmt[i] == 0) continue;
    int sx, sy;
    worldToScreen(m.dmgX[i], m.dmgY[i], &sx, &sy);
    sy -= 2;
    if (sy < HUD_H) sy = HUD_H;
    if (sx < 0) sx = 0;
    char t[6];
    snprintf(t, sizeof(t), "%d", (int)m.dmgAmt[i]);
    if (m.dmgAmt[i] == (int8_t)(-100)) snprintf(t, sizeof(t), "-100");
    int tw = displayTextWidth(t);
    displayFillRect(sx - 2, sy - 1, tw + 3, 8, false);
    displayText(sx - 1, sy, t, true);
  }
  if ((m.phase == PHASE_CONTROL || m.phase == PHASE_CHARGING || m.phase == PHASE_BLOWTORCH ||
       m.phase == PHASE_DRACO || m.phase == PHASE_JETPACK || m.live) &&
      m.nPlayers > 0) {
    for (int i = 0; i < m.nPlayers; i++) {
      const Player& apl = m.p[i];
      if (!apl.alive || apl.dying || apl.bursting) continue;
      if (apl.hp == 0) continue;
      if (apl.y + 1 >= m.waterY) continue;
      if (!m.live && i != m.turn) continue;
      PlayPhase ph = m.live ? (PlayPhase)apl.act : m.phase;
      if (ph != PHASE_CONTROL && ph != PHASE_CHARGING && ph != PHASE_BLOWTORCH &&
          ph != PHASE_DRACO && ph != PHASE_JETPACK)
        continue;
      if (apl.weapon == WPN_BAT || apl.weapon == WPN_PIPE || apl.weapon == WPN_MINE ||
          apl.weapon == WPN_DYNAMITE || apl.weapon == WPN_JETPACK)
        continue;
      float rad = apl.aim * 3.14159265f / 180.f;
      int x0 = apl.x;
      int y0 = apl.y - 4;
      int x1 = x0 + (int)(cosf(rad) * 10.f);
      int y1 = y0 - (int)(sinf(rad) * 10.f);
      if (apl.weapon != WPN_BAZOOKA && apl.weapon != WPN_HOMING && apl.weapon != WPN_DRACO &&
          apl.weapon != WPN_BLOWTORCH) {
        worldLine(x0, y0, x1, y1);
      }
      drawAimTip(x1, y1);
    }
  }
  if (m.phase == PHASE_TARGET) {
    bool homing = false;
    if (m.live && m.selfId < m.nPlayers) homing = m.p[m.selfId].weapon == WPN_HOMING;
    else if (m.turn < m.nPlayers) homing = m.p[m.turn].weapon == WPN_HOMING;
    if (!homing) {
      int sx, sy;
      worldToScreen(m.cursorX, 0, &sx, &sy);
      displayVLine(sx, HUD_H, OLED_H - HUD_H, true);
      int stand = terrainStandY(m.terrain, m.cursorX);
      if (stand < 0) stand = m.waterY;
      worldToScreen(m.cursorX, stand, &sx, &sy);
      displayHLine(sx - 4, sy, 9, true);
      displaySet(sx - 2, sy - 2, true);
      displaySet(sx + 2, sy - 2, true);
    }
  }
  for (int i = 0; i < MAX_MINES; i++) {
    if (!m.mines[i].on) continue;
    int mx = m.mines[i].x;
    int my = m.mines[i].y;
    bool armed = millis() >= m.mines[i].armAt;
    if (m.mines[i].kind == 1) {
      bool spark = ((millis() / 80) & 1) != 0;
      worldSet(mx, my, true);
      worldSet(mx - 1, my, true);
      worldSet(mx + 1, my, true);
      worldSet(mx, my - 1, true);
      worldSet(mx - 1, my - 1, true);
      worldSet(mx + 1, my - 1, true);
      worldSet(mx, my - 2, true);
      worldSet(mx - 1, my - 2, true);
      worldSet(mx + 1, my - 2, true);
      worldSet(mx, my - 3, true);
      worldSet(mx - 1, my - 3, true);
      worldSet(mx + 1, my - 3, true);
      worldSet(mx, my - 4, true);
      if (spark) {
        worldSet(mx + 1, my - 5, true);
        worldSet(mx - 1, my - 5, true);
        worldSet(mx, my - 6, true);
      } else {
        worldSet(mx, my - 5, true);
      }
      continue;
    }
    if (!armed && ((millis() / 200) & 1) == 0) continue;
    worldSet(mx, my, true);
    worldSet(mx - 1, my, true);
    worldSet(mx + 1, my, true);
    worldSet(mx, my - 1, true);
    if (armed) worldSet(mx, my - 2, true);
  }
  static int16_t sGunDeg[MAX_SENTRY];
  static uint8_t sGunOn[MAX_SENTRY];
  auto wrap360 = [](int a) {
    while (a < 0) a += 360;
    while (a >= 360) a -= 360;
    return a;
  };
  auto degToT = [](int d, int curT) {
    while (d < 0) d += 360;
    while (d >= 360) d -= 360;
    if (d > 250 && d < 290) return (curT > 160) ? 320 : 0;
    if (d >= 290) return d - 290;
    return d + 70;
  };
  auto tToDeg = [](int t) {
    if (t < 0) t = 0;
    if (t > 320) t = 320;
    if (t <= 69) return 290 + t;
    return t - 70;
  };
  auto stepGun = [&](int cur, int want, int maxStep) {
    int t = degToT(cur, 160);
    int wt = degToT(want, t);
    int dt = wt - t;
    if (dt > maxStep) dt = maxStep;
    if (dt < -maxStep) dt = -maxStep;
    return tToDeg(t + dt);
  };
  auto iround = [](float v) { return (int)(v >= 0.f ? v + 0.5f : v - 0.5f); };
  for (int i = 0; i < MAX_SENTRY; i++) {
    if (!m.sentries[i].on) {
      sGunOn[i] = 0;
      continue;
    }
    int sx = m.sentries[i].x;
    int sy = m.sentries[i].y;
    int want = wrap360(m.sentries[i].aim);
    if (!sGunOn[i]) {
      sGunDeg[i] = (int16_t)want;
      sGunOn[i] = 1;
    } else {
      sGunDeg[i] = (int16_t)stepGun(sGunDeg[i], want, 6);
    }
    int deg = sGunDeg[i];
    bool squat = ((millis() / 480 + (uint32_t)i * 90) & 1) != 0;
    auto ink = [](int px, int py) { worldSet(px, py, true); };
    auto fillH = [&](int x0, int x1, int y) {
      if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
      for (int x = x0; x <= x1; x++) ink(x, y);
    };
    if (squat) {
      fillH(sx - 4, sx - 2, sy);
      fillH(sx + 2, sx + 4, sy);
      fillH(sx - 3, sx + 3, sy + 1);
    } else {
      fillH(sx - 4, sx - 2, sy - 1);
      fillH(sx + 2, sx + 4, sy - 1);
      fillH(sx - 4, sx - 2, sy);
      fillH(sx + 2, sx + 4, sy);
      fillH(sx - 3, sx + 3, sy + 1);
    }
    int drop = squat ? 1 : 0;
    int by = sy - 2 + drop;
    fillH(sx - 3, sx + 3, by);
    fillH(sx - 2, sx + 2, by - 1);
    fillH(sx - 2, sx + 2, by - 2);
    int px = sx;
    int py = by - 2;
    fillH(px - 1, px + 1, py);
    fillH(px - 1, px + 1, py - 1);
    float rad = (float)deg * 3.14159265f / 180.f;
    float c = cosf(rad);
    float s = sinf(rad);
    int x1 = px + iround(c * 6.f);
    int y1 = py - iround(s * 6.f);
    int ox = iround(-s);
    int oy = iround(-c);
    if (ox == 0 && oy == 0) {
      if (fabsf(c) >= fabsf(s)) oy = 1;
      else ox = 1;
    }
    worldLine(px, py, x1, y1);
    worldLine(px + ox, py + oy, x1 + ox, y1 + oy);
    ink(x1, y1);
    ink(x1 + ox, y1 + oy);
    if (((millis() / 180) & 1) != 0) {
      int mx = px + iround(c * 7.f);
      int my = py - iround(s * 7.f);
      ink(mx, my);
      ink(mx + ox, my + oy);
    }
  }
  for (int i = 0; i < MAX_PROJ; i++) {
    if (!m.proj[i].active) continue;
    int px = m.proj[i].x >> 8;
    int py = m.proj[i].y >> 8;
    if (m.proj[i].type == PRJ_PLANE) {
      bool prop = ((millis() / 50) & 1) != 0;
      int dir = m.proj[i].vx >= 0 ? 1 : -1;
      worldSet(px, py, true);
      worldSet(px + dir, py, true);
      worldSet(px + dir * 2, py, true);
      worldSet(px + dir * 3, py, true);
      worldSet(px + dir * 4, py, true);
      worldSet(px + dir * 2, py - 1, true);
      worldSet(px + dir * 2, py + 1, true);
      worldSet(px + dir, py - 1, true);
      worldSet(px + dir, py + 1, true);
      worldSet(px + dir * 3, py - 2, true);
      worldSet(px + dir * 3, py + 2, true);
      worldSet(px + dir * 2, py - 2, true);
      worldSet(px + dir * 2, py + 2, true);
      worldSet(px, py - 1, true);
      worldSet(px - dir, py, prop);
      worldSet(px - dir, py - 1, !prop);
      worldSet(px - dir, py + 1, prop);
      static uint32_t planePuff;
      if (millis() - planePuff > 70) {
        planePuff = millis();
        spawnJetPuff(px - dir * 3, py + 1, -dir);
      }
      continue;
    }
    if (m.proj[i].type == PRJ_PIPE) {
      bool stuck = (m.proj[i].vx == 0 && m.proj[i].vy == 0);
      uint32_t blinkMs = stuck ? 90 : 160;
      bool blink = ((millis() / blinkMs) & 1) != 0;
      if (stuck && m.proj[i].fuseMs < 1000) blink = ((millis() / 50) & 1) != 0;
      worldSet(px, py, true);
      worldSet(px + 1, py, true);
      worldSet(px - 1, py, true);
      worldSet(px, py - 1, true);
      worldSet(px + 1, py - 1, true);
      worldSet(px - 1, py - 1, true);
      worldSet(px, py - 2, true);
      worldSet(px + 1, py - 2, true);
      worldSet(px, py - 3, true);
      worldSet(px + 1, py - 3, true);
      worldSet(px, py - 4, true);
      if (blink) {
        worldSet(px + 1, py - 4, true);
        worldSet(px, py - 5, true);
        worldSet(px, py + 1, true);
      }
      if (stuck && m.proj[i].fuseMs) {
        int n = ((int)m.proj[i].fuseMs + 999) / 1000;
        if (n > 3) n = 3;
        if (n < 1) n = 1;
        int ax = (px - sCamX) * sZNum / sZDen + 4;
        int ay = HUD_H + (py - 12 - sCamY) * sZNum / sZDen;
        if (ay < HUD_H) ay = HUD_H;
        displayFillRect(ax - 1, ay - 1, 8, 9, false);
        displayChar(ax, ay, (char)('0' + n), true);
      }
      continue;
    }
    if (m.proj[i].type == PRJ_HOME || m.proj[i].type == PRJ_BAZOOKA) {
      drawMissile(px, py, m.proj[i].vx, m.proj[i].vy);
      continue;
    }
    worldSet(px, py, true);
    worldSet(px + 1, py, true);
    worldSet(px, py + 1, true);
    if (m.proj[i].type == PRJ_GRENADE || m.proj[i].type == PRJ_POD) {
      worldSet(px - 1, py, true);
      worldSet(px, py - 1, true);
      worldSet(px + 1, py - 1, true);
      worldSet(px - 1, py + 1, true);
    } else if (m.proj[i].type == PRJ_DRACO || m.proj[i].type == PRJ_SENTRY) {
      worldSet(px, py, true);
    } else {
      int vx = m.proj[i].vx;
      int vy = m.proj[i].vy;
      int ax = 0, ay = 0;
      int avx = vx < 0 ? -vx : vx;
      int avy = vy < 0 ? -vy : vy;
      if (avx >= avy) ax = vx >= 0 ? 1 : -1;
      else ay = vy >= 0 ? 1 : -1;
      worldSet(px + ax, py + ay, true);
      worldSet(px - ax, py - ay, true);
      worldSet(px - ax * 2, py - ay * 2, false);
    }
  }
  if (millis() < m.explodeUntil && m.explodeR) {
    uint32_t left = m.explodeUntil - millis();
    uint32_t anim = (m.explodeR >= 10) ? (EXPLODE_ANIM_MS * 2 + 80) : EXPLODE_ANIM_MS;
    int frame = (int)((anim - left) * 8 / anim);
    if (frame < 0) frame = 0;
    if (frame > 7) frame = 7;
    int r = 2 + frame * (int)m.explodeR / 5;
    int cx = m.explodeX, cy = m.explodeY;
    if (frame <= 1) {
      worldSet(cx, cy, true);
      worldSet(cx - 1, cy, true);
      worldSet(cx + 1, cy, true);
      worldSet(cx, cy - 1, true);
      worldSet(cx, cy + 1, true);
      worldSet(cx - 2, cy, frame == 0);
      worldSet(cx + 2, cy, frame == 0);
      worldSet(cx, cy - 2, frame == 0);
      worldSet(cx, cy + 2, frame == 0);
      worldSet(cx - 1, cy - 1, true);
      worldSet(cx + 1, cy - 1, true);
      worldSet(cx - 1, cy + 1, true);
      worldSet(cx + 1, cy + 1, true);
    }
    for (int dy = -r; dy <= r; dy++) {
      for (int dx = -r; dx <= r; dx++) {
        int d = dx * dx + dy * dy;
        if (d >= (r - 1) * (r - 1) && d <= r * r)
          worldSet(cx + dx, cy + dy, true);
        int r2 = r - 2;
        if (r2 > 2 && d >= (r2 - 1) * (r2 - 1) && d <= r2 * r2 && (frame & 1))
          worldSet(cx + dx, cy + dy, true);
        int r3 = r / 2;
        if (r3 > 1 && frame < 5 && d >= (r3 - 1) * (r3 - 1) && d <= r3 * r3 &&
            ((dx + dy + frame) & 1))
          worldSet(cx + dx, cy + dy, true);
      }
    }
    int spoke = r + 3;
    worldSet(cx, cy - spoke, true);
    worldSet(cx, cy + spoke, true);
    worldSet(cx - spoke, cy, true);
    worldSet(cx + spoke, cy, true);
    worldSet(cx, cy - spoke - 1, true);
    worldSet(cx, cy + spoke + 1, true);
    worldSet(cx - spoke - 1, cy, true);
    worldSet(cx + spoke + 1, cy, true);
    if (frame & 1) {
      worldSet(cx - spoke + 1, cy - spoke + 1, true);
      worldSet(cx + spoke - 1, cy - spoke + 1, true);
      worldSet(cx - spoke + 1, cy + spoke - 1, true);
      worldSet(cx + spoke - 1, cy + spoke - 1, true);
    }
    if (frame < 4) {
      int bit = 1 + (frame & 3);
      worldSet(cx - r - bit, cy - 2, true);
      worldSet(cx + r + bit, cy - 1, true);
      worldSet(cx - 2, cy - r - bit, true);
      worldSet(cx + 1, cy + r + bit, true);
      worldSet(cx - r, cy + 3, (frame & 1) != 0);
      worldSet(cx + r, cy - 3, (frame & 1) == 0);
    }
  }
  for (int i = 0; i < MAX_PARTICLES; i++) {
    if (!parts[i].on) continue;
    int px = parts[i].x, py = parts[i].y;
    worldSet(px, py, true);
    if (parts[i].splash == 4 || parts[i].splash == 5 || parts[i].splash == 6) {
      if (parts[i].life & 1) worldSet(px, py - 1, true);
      if (parts[i].splash == 5) {
        worldSet(px + 1, py, true);
        worldSet(px - 1, py, true);
      }
      if (parts[i].splash == 6) {
        worldSet(px + (parts[i].vx > 0 ? 1 : -1), py, true);
        worldSet(px, py + 1, (parts[i].life & 1) != 0);
      }
    } else if (parts[i].splash == 0 && parts[i].life > 18) {
      worldSet(px + (parts[i].vx > 0 ? 1 : (parts[i].vx < 0 ? -1 : 0)), py, true);
    }
  }
}

static void drawHeaderBar(const char* title) {
  displayFillRect(0, 0, OLED_W, 10, true);
  displayCenterText(2, title, false);
}

static void drawFooterBar(const char* left) {
  displayFillRect(0, 54, OLED_W, 10, true);
  const char* s = (left && left[0]) ? left : "DON'T FORGET TO FUMBLE!";
  int n = 0;
  for (const char* p = s; *p; p++) n++;
  int pitch = (n * 6 <= OLED_W) ? 6 : 5;
  int x = (OLED_W - n * pitch) / 2;
  if (x < 0) x = 0;
  while (*s) {
    displayChar(x, 56, *s++, false);
    x += pitch;
  }
}

static void drawHudPlay() {
  const Match& m = gameMatch();
  displayFillRect(0, 0, OLED_W, HUD_H, false);
  displayHLine(0, HUD_H - 1, OLED_W, true);
  if (m.turn >= m.nPlayers && !m.live) return;
  char buf[20];
  uint8_t who = m.live ? m.selfId : m.turn;
  bool mine = m.practice || m.live || who == m.selfId;
  if (who < m.nPlayers) {
    snprintf(buf, sizeof(buf), "P%u", (unsigned)(who + 1));
    if (mine) {
      displayFillRect(0, 0, 16, 8, true);
      displayText(2, 1, buf, false);
    } else {
      displayText(2, 1, buf, true);
    }
  }
  if (m.live) {
    displayText(20, 1, "LIVE", true);
  } else {
    int sec = (m.turnLeft + 999) / 1000;
    snprintf(buf, sizeof(buf), "%2d", sec);
    displayText(20, 1, buf, true);
  }
  if (m.cheat) displayText(44, 1, "CHT", true);
  drawWind(OLED_W - 1, 1, m.wind, true);

  int n = m.nPlayers > 0 ? m.nPlayers : 1;
  const int chip = 40;
  for (int i = 0; i < n; i++) {
    int x;
    if (n == 1) x = 1;
    else if (i == n - 1) x = OLED_W - 1;
    else x = 1 + i * (OLED_W - 2 - chip) / (n - 1);
    bool right = (i == n - 1 && n > 1);
    drawHpChip(i, x, 8, m, right);
  }
}

static void drawWeaponMenu() {
  const Match& m = gameMatch();
  const char* names[WPN_COUNT] = {"BAZOOK", "GRENAD", "AIR", "TELE", "TORCH",
                                  "MOLLI", "BAT", "JET", "MINE", "HOME", "DRACO", "SENTRY", "PIPE",
                                  "DYNAM"};
  displayFillRect(0, 0, OLED_W, OLED_H, false);
  drawHeaderBar("WEAPON");
  if (m.turn >= m.nPlayers) return;
  const Player& pl = m.p[m.turn];
  for (int slot = 0; slot < WPN_COUNT; slot++) {
    uint8_t wpn = wpnMenuAt((uint8_t)slot);
    int col = slot / WPN_MENU_ROWS;
    int row = slot % WPN_MENU_ROWS;
    int x = col * 64;
    int y = 11 + row * 7;
    bool sel = (slot == m.menuSel);
    if (sel) displayFillRect(x, y - 1, 64, 8, true);
    displayText(x + 1, y, names[wpn], !sel);
    char a[8] = "";
    bool inf = m.cheat || wpn == WPN_BAZOOKA;
    if (wpn == WPN_AIRSTRIKE && m.mapKind == MAP_CAVERN) memcpy(a, "--", 3);
    else if (inf) memcpy(a, "inf", 4);
    else if (wpn == WPN_MOLLI) snprintf(a, sizeof(a), "x%u", (unsigned)pl.ammoMo);
    else if (wpn == WPN_GRENADE) snprintf(a, sizeof(a), "x%u", (unsigned)pl.ammoG);
    else if (wpn == WPN_AIRSTRIKE) snprintf(a, sizeof(a), "x%u", (unsigned)pl.ammoA);
    else if (wpn == WPN_TELEPORT) snprintf(a, sizeof(a), "x%u", (unsigned)pl.ammoT);
    else if (wpn == WPN_BLOWTORCH) snprintf(a, sizeof(a), "x%u", (unsigned)pl.ammoB);
    else if (wpn == WPN_BAT) snprintf(a, sizeof(a), "x%u", (unsigned)pl.ammoBat);
    else if (wpn == WPN_JETPACK) snprintf(a, sizeof(a), "%u", (unsigned)pl.ammoJ);
    else if (wpn == WPN_MINE) snprintf(a, sizeof(a), "x%u", (unsigned)pl.ammoM);
    else if (wpn == WPN_DYNAMITE) snprintf(a, sizeof(a), "x%u", (unsigned)pl.ammoDyn);
    else if (wpn == WPN_HOMING) snprintf(a, sizeof(a), "x%u", (unsigned)pl.ammoH);
    else if (wpn == WPN_DRACO) snprintf(a, sizeof(a), "x%u", (unsigned)pl.ammoD);
    else if (wpn == WPN_SENTRY) snprintf(a, sizeof(a), "x%u", (unsigned)pl.ammoS);
    else if (wpn == WPN_PIPE) snprintf(a, sizeof(a), "x%u", (unsigned)pl.ammoP);
    displayText(x + 46, y, a, !sel);
  }
}

static void drawChargeAndMove() {
  const Match& m = gameMatch();
  uint8_t pid = m.live ? m.selfId : m.turn;
  if (!(m.practice || m.live || m.turn == m.selfId)) return;
  if (pid >= m.nPlayers) return;
  const Player& pl = m.p[pid];
  if (m.phase == PHASE_CHARGING) {
    displayFillRect(0, OLED_H - 8, OLED_W, 8, false);
    displayText(1, OLED_H - 8, "POW", true);
    bool jetHud = pl.jetting || pl.act == PHASE_JETPACK;
    if (jetHud) {
      drawBar(22, OLED_H - 7, 40, m.charge, 100);
      displayText(66, OLED_H - 8, "JET", true);
      drawBar(86, OLED_H - 7, 40, pl.ammoJ, AMMO_JET);
    } else {
      drawBar(24, OLED_H - 7, 56, m.charge, 100);
    }
  }
  if (m.phase == PHASE_BLOWTORCH) {
    displayFillRect(0, OLED_H - 8, 90, 8, false);
    displayText(1, OLED_H - 8, "TORCH", true);
    uint32_t used = millis() - m.phaseAt;
    int left = used >= TORCH_MS ? 0 : (int)(TORCH_MS - used);
    drawBar(36, OLED_H - 7, 50, left, TORCH_MS);
  }
  if (m.phase == PHASE_MOLLI) {
    displayFillRect(0, OLED_H - 8, 90, 8, false);
    displayText(1, OLED_H - 8, "MOLLI", true);
    uint32_t used = millis() - m.phaseAt;
    if (used < MOLLI_WINDUP_MS && !m.cheat) {
      drawBar(36, OLED_H - 7, 50, (int)used, MOLLI_WINDUP_MS);
    }
  }
  if (m.phase == PHASE_BAT) {
    displayFillRect(0, OLED_H - 8, 70, 8, false);
    displayText(1, OLED_H - 8, "BAT", true);
  }
  if (m.phase == PHASE_JETPACK) {
    const char* wn[] = {"BAZ", "GRN", "AIR", "TEL", "TRC", "MOL", "BAT", "JET", "MIN", "HOM", "DRC", "SNT", "PIP", "DYN"};
    uint8_t w = pl.weapon < WPN_COUNT ? pl.weapon : 0;
    displayFillRect(0, OLED_H - 8, OLED_W, 8, false);
    if (w != WPN_JETPACK) {
      displayText(1, OLED_H - 8, wn[w], true);
      displayText(28, OLED_H - 8, "JET", true);
      drawBar(48, OLED_H - 7, 78, pl.ammoJ, AMMO_JET);
    } else {
      displayText(1, OLED_H - 8, "JET", true);
      drawBar(24, OLED_H - 7, 56, pl.ammoJ, AMMO_JET);
    }
  }
  if (m.phase == PHASE_CONTROL || m.phase == PHASE_BLOWTORCH || m.phase == PHASE_DRACO) {
    const char* wn[] = {"BAZ", "GRN", "AIR", "TEL", "TRC", "MOL", "BAT", "JET", "MIN", "HOM", "DRC", "SNT", "PIP", "DYN"};
    uint8_t w = pl.weapon < WPN_COUNT ? pl.weapon : 0;
    displayFillRect(0, OLED_H - 8, 24, 8, false);
    displayText(1, OLED_H - 8, wn[w], true);
  }
  uint32_t nowHud = millis();
  for (int i = 0; i < MAX_MINES; i++) {
    if (!m.mines[i].on || m.mines[i].kind != 1) continue;
    if (m.mines[i].owner != pid) continue;
    if (nowHud >= m.mines[i].armAt) continue;
    uint32_t left = m.mines[i].armAt - nowHud;
    int sec = (int)((left + 999) / 1000);
    char fb[8];
    snprintf(fb, sizeof(fb), "FUSE %d", sec);
    displayFillRect(26, OLED_H - 8, 50, 8, false);
    displayText(26, OLED_H - 8, fb, true);
    break;
  }
  if (m.phase == PHASE_TARGET) {
    const char* t = (pl.weapon == WPN_TELEPORT) ? "TELE X"
                    : (pl.weapon == WPN_HOMING) ? "LOCK"
                    : "STRIKE";
    displayFillRect(0, OLED_H - 8, 70, 8, false);
    displayText(1, OLED_H - 8, t, true);
    if (pl.weapon == WPN_HOMING && m.homeTarget < m.nPlayers) {
      char b[8];
      snprintf(b, sizeof(b), "P%u", (unsigned)(m.homeTarget + 1));
      displayText(100, OLED_H - 8, b, true);
    }
  } else if (m.homeLocked && pl.weapon == WPN_HOMING && m.phase == PHASE_CONTROL &&
             m.homeTarget < m.nPlayers) {
    char b[8];
    snprintf(b, sizeof(b), "P%u", (unsigned)(m.homeTarget + 1));
    displayText(100, OLED_H - 8, b, true);
  }
}

static void banner(const char* a, const char* b) {
  displayFillRect(10, 16, 108, 32, false);
  displayFillRect(10, 16, 108, 10, true);
  displayCenterText(18, a, false);
  displayHLine(10, 47, 108, true);
  displayVLine(10, 16, 32, true);
  displayVLine(117, 16, 32, true);
  if (b && b[0]) displayCenterText(32, b, true);
}

void renderFrame() {
  const Match& m = gameMatch();
  displayClear();

  switch (m.mode) {
    case MODE_INTRO: {
      displayCenterText(4, "WARTOADS", true);
      displayCenterText(14, "A HANDHELD TRIBUTE", true);
      displayCenterText(24, "TO TEAM17 WORMS", true);
      displayCenterText(34, "BY @MURU.GLOBAL", true);
      displayCenterText(46, WM_VERSION_STR, true);
      if ((millis() / 400) & 1) displayCenterText(56, "OK", true);
      break;
    }
    case MODE_TITLE: {
      displayCenterText(14, "WARTOADS", true);
      if ((millis() / 420) & 1) {
        displayFillRect(2, 32, 124, 12, true);
        displayCenterText(35, "PRESS OK TO START", false);
      }
      break;
    }
    case MODE_MAIN_MENU: {
      drawHeaderBar("WARTOADS");
      const char* items[TITLE_ROWS] = {
        "PRACTICE", "MULTIPLAYER", "BUTTON TEST", "BUTTON CONFIG", "CREDITS"
      };
      for (int i = 0; i < TITLE_ROWS; i++) {
        int y = 11 + i * 8;
        bool sel = m.titleSel == i;
        if (sel) displayFillRect(0, y - 1, OLED_W, 10, true);
        displayText(8, y, items[i], !sel);
      }
      drawFooterBar("");
      break;
    }
    case MODE_CREDITS: {
      drawHeaderBar("CREDITS");
      displayCenterText(14, "WARTOADS", true);
      displayCenterText(24, "HANDHELD TRIBUTE", true);
      displayCenterText(34, "INSPIRED BY", true);
      displayCenterText(44, "TEAM17 WORMS", true);
      displayCenterText(54, "BY @MURU.GLOBAL", true);
      break;
    }
    case MODE_SETUP: {
      drawHeaderBar(m.setupFromRematch ? "NEXT MATCH" : (m.setupHost ? "HOST GAME" : "PRACTICE"));
      const char* maps[MAP_COUNT] = {"CAVERN", "ISLAND", "ISLES"};
      uint8_t mk = m.mapKind < MAP_COUNT ? m.mapKind : MAP_ISLAND;
      uint8_t to = m.turnOpt < TURN_OPT_N ? m.turnOpt : 1;
      uint8_t rows = m.setupHost ? SETUP_HOST_ROWS : SETUP_PRAC_ROWS;
      char line[22];
      int gap = (rows <= 4) ? 11 : 8;
      int y0 = (rows <= 4) ? 12 : 11;
      for (uint8_t i = 0; i < rows; i++) {
        int y = y0 + i * gap;
        bool sel = m.lobbySel == i;
        bool start = (i == rows - 1);
        if (sel) displayFillRect(0, y - 1, OLED_W, (gap >= 10) ? 11 : 9, true);
        if (i == 0) snprintf(line, sizeof(line), "MAP  %s", maps[mk]);
        else if (i == 1) {
          if (m.live && m.setupHost) snprintf(line, sizeof(line), "TIME --");
          else snprintf(line, sizeof(line), "TIME %us", (unsigned)(turnMsFor(to) / 1000));
        } else if (m.setupHost && i == 2) {
          snprintf(line, sizeof(line), "PACE %s", m.live ? "LIVE" : "TURNS");
        } else if (m.setupHost && i == 3) {
          uint8_t lv = m.livesOpt;
          if (lv < LIVES_MIN) lv = LIVES_MIN;
          if (lv > LIVES_MAX) lv = LIVES_MAX;
          if (m.live) snprintf(line, sizeof(line), "LIVES %u", (unsigned)lv);
          else snprintf(line, sizeof(line), "LIVES --");
        } else if ((!m.setupHost && i == 2) || (m.setupHost && i == 4)) {
          snprintf(line, sizeof(line), "CHEAT %s", m.cheat ? "ON" : "OFF");
        } else {
          snprintf(line, sizeof(line), "%s", m.setupFromRematch ? "DONE" : "START");
        }
        displayText(start ? 40 : 10, y, line, !sel);
        if (sel && !start) {
          displayChar(2, y, '<', false);
          displayChar(120, y, '>', false);
        }
      }
      break;
    }
    case MODE_BTN_CFG: {
      drawHeaderBar("BUTTON CFG");
      if (m.cfgStep == 0xFF) {
        displayCenterText(18, "PRESS EACH KEY", true);
        displayCenterText(30, "IN ORDER", true);
        drawFooterBar("");
      } else if (m.cfgStep >= BTN_COUNT) {
        displayCenterText(26, "SAVED", true);
        drawFooterBar("");
      } else {
        const char* names[BTN_COUNT] = {"UP", "DOWN", "LEFT", "RIGHT", "OK", "BACK"};
        char line[20];
        snprintf(line, sizeof(line), "PRESS %s", names[m.cfgStep]);
        displayCenterText(26, line, true);
        drawFooterBar("");
      }
      break;
    }
    case MODE_BTN_TEST: {
      drawHeaderBar("BTN TEST");
      const char* names[6] = {"L", "R", "UP", "DN", "OK", "BACK"};
      for (int i = 0; i < 6; i++) {
        int x = 8 + (i % 3) * 40;
        int y = 16 + (i / 3) * 18;
        displayText(x, y, names[i], true);
        if (btnDown(i)) displayFillRect(x, y + 9, 22, 5, true);
        else displayHLine(x, y + 11, 22, true);
      }
      drawFooterBar("");
      break;
    }
    case MODE_MP_MENU: {
      drawHeaderBar("MULTIPLAYER");
      const char* items[MP_MENU_ROWS] = {"START LOBBY", "FIND LOBBY"};
      for (int i = 0; i < MP_MENU_ROWS; i++) {
        int y = 18 + i * 14;
        bool sel = m.mpSel == i;
        if (sel) displayFillRect(0, y - 1, OLED_W, 12, true);
        displayText(10, y, items[i], !sel);
      }
      drawFooterBar("");
      break;
    }
    case MODE_SEEK: {
      drawHeaderBar("FIND LOBBY");
      if (m.joining) {
        displayCenterText(26, "JOINING...", true);
      } else if (m.nFound == 0) {
        displayCenterText(26, "SEEKING...", true);
      } else {
        const char* maps[MAP_COUNT] = {"CAVERN", "ISLAND", "ISLES"};
        uint8_t nShow = m.nFound < MAX_FOUND ? m.nFound : MAX_FOUND;
        for (int i = 0; i < nShow; i++) {
          int y = 14 + i * 12;
          bool sel = m.seekSel == i;
          if (sel) displayFillRect(0, y - 1, OLED_W, 10, true);
          uint8_t mk = m.found[i].mapKind < MAP_COUNT ? m.found[i].mapKind : MAP_ISLAND;
          char line[22];
          snprintf(line, sizeof(line), "%u/4 %s %s",
                   (unsigned)m.found[i].nPlayers, maps[mk],
                   m.found[i].live ? "LIVE" : "TURN");
          displayText(6, y, line, !sel);
        }
      }
      drawFooterBar("");
      break;
    }
    case MODE_DISCOVERY:
      drawHeaderBar("FINDING");
      displayCenterText(28, "PLAYERS...", true);
      drawFooterBar("");
      break;
    case MODE_LOBBY: {
      char hdr[16];
      snprintf(hdr, sizeof(hdr), "LOBBY %u/4", (unsigned)m.nPlayers);
      drawHeaderBar(hdr);
      for (int i = 0; i < 4; i++) {
        int x = (i & 1) ? 66 : 2;
        int y = 12 + (i / 2) * 10;
        char line[14];
        if (i < m.nPlayers && m.p[i].present) {
          snprintf(line, sizeof(line), "P%u %s%s", (unsigned)(i + 1),
                   m.p[i].ready ? "READY" : "WAIT",
                   (i == m.selfId) ? "*" : "");
        } else {
          snprintf(line, sizeof(line), "P%u  -", (unsigned)(i + 1));
        }
        displayText(x, y, line, true);
      }
      if (m.lobbyQuitAsk) {
        displayFillRect(16, 10, 96, 44, false);
        displayFillRect(16, 10, 96, 10, true);
        displayCenterText(12, "LEAVE LOBBY?", false);
        displayHLine(16, 53, 96, true);
        bool stay = m.pauseSel != 0;
        if (!stay) displayFillRect(18, 24, 92, 10, true);
        displayText(28, 26, "QUIT LOBBY", stay);
        if (stay) displayFillRect(18, 36, 92, 10, true);
        displayText(28, 38, "STAY", !stay);
        drawFooterBar("OK SELECT");
        break;
      }
      if (m.isHost) {
        bool readySel = m.lobbySel == 0;
        bool startSel = m.lobbySel == 1;
        if (readySel) displayFillRect(0, 33, OLED_W, 10, true);
        displayText(10, 34, "READY UP", !readySel);
        if (startSel) displayFillRect(0, 43, OLED_W, 10, true);
        displayText(10, 44, "START GAME", !startSel);
      } else {
        bool meReady = m.selfId < m.nPlayers && m.p[m.selfId].ready;
        if (meReady) displayFillRect(0, 36, OLED_W, 12, true);
        displayCenterText(38, meReady ? "YOU ARE READY" : "READY UP", !meReady);
      }
      const char* foot = "PRESS OK TO READY UP";
      if (m.lobbyWarnUntil > millis() && ((millis() / 220) & 1)) {
        foot = (m.lobbyWarn == 1) ? "NEED 2 PLAYERS" : "ALL MUST READY";
      }
      drawFooterBar(foot);
      break;
    }
    case MODE_GROUP_FULL:
      banner("LOBBY FULL", "OK RETRY");
      break;
    case MODE_COUNTDOWN: {
      drawPlayfield();
      int left = (int)(COUNTDOWN_MS - (millis() - m.phaseAt));
      if (left > 0) {
        int n = 1 + left / 800;
        if (n < 1) n = 1;
        if (n > 3) n = 3;
        char c[2] = {(char)('0' + n), 0};
        banner(c, "GET READY");
      } else {
        drawHudPlay();
        drawChargeAndMove();
      }
      break;
    }
    case MODE_PLAY:
    case MODE_PAUSE:
      drawPlayfield();
      drawHudPlay();
      if (m.phase == PHASE_WEAPON_MENU &&
          !(m.selfId < m.nPlayers && m.p[m.selfId].dying))
        drawWeaponMenu();
      else drawChargeAndMove();
      if (m.phase == PHASE_SD_BANNER) banner("SUDDEN", "DEATH");
      if (!m.live && m.nPlayers > 0 &&
          (m.phase == PHASE_TURN_START || millis() < m.turnBannerUntil) &&
          m.msg.until <= millis()) {
        bool waitDeath = false;
        for (int i = 0; i < m.nPlayers; i++)
          if (m.p[i].alive && m.p[i].dying) waitDeath = true;
        if (!waitDeath) {
        uint8_t who = (millis() < m.turnBannerUntil) ? m.turnBannerId : m.turn;
        if (who >= m.nPlayers) who = m.turn;
        if (who < m.nPlayers) {
          char a[12];
          snprintf(a, sizeof(a), "P%u", (unsigned)(who + 1));
          bool mine = m.practice || who == m.selfId;
          banner(a, mine ? "YOUR TURN" : "TURN");
        }
        }
      }
      if (m.mode == MODE_PAUSE) {
        displayFillRect(20, 12, 88, 42, false);
        displayFillRect(20, 12, 88, 10, true);
        displayCenterText(14, "PAUSED", false);
        displayVLine(20, 12, 42, true);
        displayVLine(107, 12, 42, true);
        displayHLine(20, 53, 88, true);
        bool q = m.pauseSel != 0;
        if (!q) displayFillRect(22, 26, 84, 9, true);
        displayText(30, 27, "RESUME", q);
        if (q) displayFillRect(22, 36, 84, 9, true);
        displayText(30, 37, "QUIT", !q);
      }
      break;
    case MODE_GAMEOVER: {
      drawPlayfield();
      displayFillRect(0, 0, OLED_W, HUD_H, true);
      char a[16];
      if (m.winner == m.selfId && m.winner != PLAYER_NONE)
        snprintf(a, sizeof(a), "YOU WIN!");
      else if (m.winner == PLAYER_NONE)
        snprintf(a, sizeof(a), "DRAW");
      else
        snprintf(a, sizeof(a), "P%u WINS", (unsigned)(m.winner + 1));
      displayCenterText(4, a, false);
      break;
    }
    case MODE_REMATCH: {
      drawPlayfield();
      displayFillRect(0, 0, OLED_W, OLED_H, false);
      drawHeaderBar("REMATCH");
      if (m.practice) {
        const char* rows[3] = {"REMATCH", "CHANGE MAP", "QUIT TO MENU"};
        for (int i = 0; i < 3; i++) {
          bool sel = m.lobbySel == i;
          int y = 16 + i * 12;
          if (sel) displayFillRect(0, y, OLED_W, 12, true);
          displayCenterText(y + 2, rows[i], !sel);
        }
        drawFooterBar("OK SELECT  BACK EXITS");
      } else {
        for (int i = 0; i < m.nPlayers && i < 4; i++) {
          char line[18];
          snprintf(line, sizeof(line), "P%u  %s", (unsigned)(i + 1),
                   m.p[i].ready ? "READY" : "WAIT");
          displayText(6, 12 + i * 8, line, true);
        }
        if (m.isHost) {
          int y = 12 + m.nPlayers * 8 + 2;
          if (y > 36) y = 36;
          bool readySel = m.lobbySel == 0;
          bool mapSel = m.lobbySel == 1;
          if (readySel) displayFillRect(0, y, OLED_W, 10, true);
          displayText(10, y + 1, "READY UP", !readySel);
          if (mapSel) displayFillRect(0, y + 10, OLED_W, 10, true);
          displayText(10, y + 11, "CHANGE MAP", !mapSel);
        }
        drawFooterBar(m.isHost ? "OK SELECT  BACK EXITS" : "OK TOGGLE  BACK EXITS");
      }
      break;
    }
    case MODE_HOST_LOST:
      banner("HOST LOST", "MATCH ENDED");
      break;
    default:
      break;
  }

  if (m.msg.until > millis()) {
    if (m.msg.subtle) {
      const char* t = m.msg.line1;
      int tw = displayTextWidth(t);
      int x = OLED_W - tw - 3;
      int y = HUD_H;
      displayFillRect(x - 2, y - 1, tw + 4, 9, true);
      displayText(x, y, t, false);
    } else {
      banner(m.msg.line1, m.msg.line2);
    }
  }
  displayPresent();
}
