#include "game.h"
#include "buttons.h"
#include "display.h"
#include "mirror.h"
#include "net.h"
#include "protocol.h"
#include "render.h"
#include <Arduino.h>
#include <esp_system.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

Match g;
static uint32_t lastMs;
static uint32_t physAcc;
static Rng rng(1);
static uint8_t sGesture; // 0 none, 1 jump, 2 weapon menu
static uint8_t sPendingAct;

static uint8_t sChunkOut = 0xFF;
static uint32_t sLastChunkTx;
static uint32_t sLastStartTx;
static uint32_t sJetFuelAcc[MAX_PLAYERS];
static uint32_t sBusyAcc[MAX_PLAYERS];
static int16_t sTorchAccX[MAX_PLAYERS], sTorchAccY[MAX_PLAYERS];
static uint8_t sDracoLeft;
static uint32_t sDracoAcc;
static uint8_t sAirHang[MAX_PLAYERS];
static uint8_t sVelXFric[MAX_PLAYERS];
static int16_t sSeenX[MAX_PLAYERS], sSeenY[MAX_PLAYERS];
static uint16_t aimAccP[MAX_PLAYERS];
static uint16_t moveAccP[MAX_PLAYERS];
static uint16_t sCurAccP[MAX_PLAYERS];
static bool sAimWasU[MAX_PLAYERS], sAimWasD[MAX_PLAYERS];
static bool sMvWasL[MAX_PLAYERS], sMvWasR[MAX_PLAYERS], sMvPending[MAX_PLAYERS];
static bool sCurWasL[MAX_PLAYERS], sCurWasR[MAX_PLAYERS];
static uint32_t sCoyoteUntil[MAX_PLAYERS];
static uint32_t sUpTapAt[MAX_PLAYERS];
static bool sJumpEatAim[MAX_PLAYERS];
static bool sAimHoldOn[MAX_PLAYERS];
static uint32_t sWpnCd[MAX_PLAYERS];
static uint32_t sFireLockUntil[MAX_PLAYERS];
static uint32_t sJetHoverUntil[MAX_PLAYERS];
static uint16_t sJetMoveAcc[MAX_PLAYERS];
static uint8_t sJumpKind[MAX_PLAYERS];
static uint32_t sPendMsgAt;
static char sPendL1[16];
static char sPendL2[16];
static uint16_t sPendMsgMs;
static bool sSplashed[MAX_PLAYERS];
static uint32_t sDrownHoldUntil[MAX_PLAYERS];
static uint32_t sDrownMsgUntil[MAX_PLAYERS];
static uint32_t sSpawnProtectUntil[MAX_PLAYERS];
static bool sJetWasOn[MAX_PLAYERS];
static uint8_t sCfgPhys[BTN_COUNT];
static bool sCfgWaitRelease;
static uint16_t sLobbySeq;
static bool sLobbySeqInit;
static bool sWantPause;
static uint32_t sHeardAt;
static uint32_t sQuietUntil;
static bool sGoMpMenu;
static bool sAllowLeaveLobby;
static uint32_t sTickNow;
static int16_t sPeerX[MAX_PLAYERS];
static int16_t sPeerY[MAX_PLAYERS];
static uint8_t sPeerOn[MAX_PLAYERS];

static const uint8_t CFG_ORDER[BTN_COUNT] = {
  BTN_AIM_UP, BTN_AIM_DN, BTN_LEFT, BTN_RIGHT, BTN_FIRE, BTN_WEAPON
};

static void beginTurn();
static void beginMpMenu();
static void lockHomingFromCursor();
static void stepHomingTarget(int dir);
static void hostControlTick(uint32_t dt);
static void hostBeginGame();
static void startDraco(Player& pl);
static void tickDraco(Player& pl, uint32_t dt);
static void stopJetpack(Player& pl);
static void tickSentries(uint32_t now);
static void deploySentry(uint8_t owner, int x, int y);
static void hurtSentries(int cx, int cy, int r, int maxDmg, uint8_t skipOwner);
static void boomProj(Proj& pr, int x, int y, int r);
static bool tryJump(Player& pl);
static bool hasAmmo(const Player& pl, uint8_t w);
static void fallbackWeapon(Player& pl);
static bool wormFits(const Terrain& t, int x, int y);
static void abortMenusOnDeath(uint8_t id);
static void tryFireWeapon(Player& pl, bool showHint);

static int pidOf(const Player& pl) {
  int i = (int)(&pl - g.p);
  if (i < 0 || i >= MAX_PLAYERS) return 0;
  return i;
}

static bool wpnCooling(const Player& pl) {
  if (g.cheat) return false;
  return millis() < sWpnCd[pidOf(pl)];
}

static bool fireLocked(const Player& pl) {
  return millis() < sFireLockUntil[pidOf(pl)];
}

static bool cannotFire(const Player& pl) {
  return fireLocked(pl) || wpnCooling(pl);
}

static void armWpnCd(const Player& pl) {
  if (g.cheat) return;
  sWpnCd[pidOf(pl)] = millis() + WPN_COOLDOWN_MS;
}

static void knockFrom(int cx, int cy, int px, int py, int force, int* ovx, int* ovy) {
  int dx = px - cx;
  int dy = py - cy;
  int d2 = dx * dx + dy * dy;
  if (d2 < 1) {
    *ovx = 0;
    *ovy = wm_clamp((int16_t)(-force - 2), -8, -3);
    return;
  }
  int dist = (int)sqrtf((float)d2);
  if (dist < 1) dist = 1;
  int vx = (dx * force) / dist;
  int vy = (dy * force) / dist - (1 + force / 4);
  if (vx > 8) vx = 8;
  if (vx < -8) vx = -8;
  if (vy < -8) vy = -8;
  if (vy > 4) vy = 4;
  *ovx = vx;
  *ovy = vy;
}

static bool livePlay() { return g.live && !g.practice; }
static bool openSky() { return g.mapKind != MAP_CAVERN; }

static bool inSessionMode() {
  GameMode m = g.mode;
  return m == MODE_LOBBY || m == MODE_COUNTDOWN || m == MODE_PLAY ||
         m == MODE_PAUSE || m == MODE_GAMEOVER || m == MODE_REMATCH;
}

// Signed elapsed so a timestamp stamped with millis() after `now` was sampled
// cannot underflow to ~4e9 and look like a 4s timeout on the same frame.
static bool aged(uint32_t now, uint32_t t, uint32_t ms) {
  if (t == 0) return false;
  return (int32_t)(now - t) >= (int32_t)ms;
}

static uint32_t tickNow() { return sTickNow ? sTickNow : millis(); }

static bool joinerInLiveLobby() {
  return !g.isHost && !g.practice && g.session != 0 && !sAllowLeaveLobby &&
         (g.mode == MODE_LOBBY || g.mode == MODE_HOST_LOST);
}

static bool spendAmmo(uint8_t& n) {
  if (g.cheat) return true;
  if (n == 0) return false;
  n--;
  return true;
}

static int wormMinY() { return openSky() ? (SKY_MIN_Y + 5) : (WORLD_TOP + 5); }

static void hitWater(Player& pl) {
  int id = pidOf(pl);
  if (id < 0 || id >= MAX_PLAYERS) return;
  if (!sSplashed[id]) {
    sSplashed[id] = true;
    spawnSplash(pl.x, g.terrain.waterY);
  }
}

static PlayPhase jetOrControl(const Player& pl) {
  int id = pidOf(pl);
  if (id >= 0 && id < MAX_PLAYERS && sJetWasOn[id] && (g.cheat || pl.ammoJ > 0))
    return PHASE_JETPACK;
  if (id >= 0 && id < MAX_PLAYERS) sJetWasOn[id] = false;
  return PHASE_CONTROL;
}

static void afterShot() {
  g.phaseAt = millis();
  g.homeLocked = false;
  sDracoLeft = 0;
  if (g.turn < g.nPlayers) fallbackWeapon(g.p[g.turn]);
  if (livePlay() && g.turn < g.nPlayers) g.phase = jetOrControl(g.p[g.turn]);
  else g.phase = livePlay() ? PHASE_CONTROL : PHASE_PROJECTILE;
  if (g.turn < g.nPlayers) armWpnCd(g.p[g.turn]);
}

static void afterMelee() {
  g.phaseAt = millis();
  if (g.turn < g.nPlayers) fallbackWeapon(g.p[g.turn]);
  if (livePlay() && g.turn < g.nPlayers) g.phase = jetOrControl(g.p[g.turn]);
  else g.phase = livePlay() ? PHASE_CONTROL : PHASE_FALLING;
  if (g.turn < g.nPlayers) armWpnCd(g.p[g.turn]);
}

const Match& gameMatch() { return g; }
uint32_t gameFreeHeap() { return ESP.getFreeHeap(); }

static bool macEq(const uint8_t* a, const uint8_t* b) { return a && b && memcmp(a, b, 6) == 0; }
static bool macZero(const uint8_t* a) {
  if (!a) return true;
  for (int i = 0; i < 6; i++) if (a[i]) return false;
  return true;
}

static void setMsg(const char* a, const char* b, uint16_t ms) {
  memset(&g.msg, 0, sizeof(g.msg));
  strncpy(g.msg.line1, a, 15);
  strncpy(g.msg.line2, b, 15);
  g.msg.until = millis() + ms;
  g.msg.subtle = 0;
}

static void scheduleMsg(const char* a, const char* b, uint32_t delayMs, uint16_t ms) {
  if (sPendMsgAt && strcmp(sPendL1, a) == 0 && strcmp(sPendL2, b) == 0) return;
  if (g.msg.until > millis() && strcmp(g.msg.line1, a) == 0 && strcmp(g.msg.line2, b) == 0) return;
  memset(sPendL1, 0, sizeof(sPendL1));
  memset(sPendL2, 0, sizeof(sPendL2));
  strncpy(sPendL1, a, 15);
  strncpy(sPendL2, b, 15);
  sPendMsgMs = ms;
  sPendMsgAt = millis() + delayMs;
}

static void tickPendMsg(uint32_t now) {
  if (!sPendMsgAt || now < sPendMsgAt) return;
  sPendMsgAt = 0;
  setMsg(sPendL1, sPendL2, sPendMsgMs ? sPendMsgMs : MSG_MS);
}

static void clearPendMsg() {
  sPendMsgAt = 0;
  sPendMsgMs = 0;
}

static void setHudHint(const char* t, uint16_t ms) {
  memset(&g.msg, 0, sizeof(g.msg));
  strncpy(g.msg.line1, t, 15);
  g.msg.until = millis() + ms;
  g.msg.subtle = 1;
}

static void focusDeathCam(uint8_t id, uint32_t holdMs) {
  if (id >= g.nPlayers) return;
  g.camFocus = id;
  g.camFocusX = g.p[id].x;
  g.camFocusY = (int16_t)(g.p[id].y - 3);
  uint32_t until = millis() + holdMs;
  if (until > g.camFocusUntil) g.camFocusUntil = until;
}

static int livingCount() {
  int n = 0;
  for (int i = 0; i < g.nPlayers; i++) if (g.p[i].alive) n++;
  return n;
}

static bool wormActive(uint8_t id) {
  if (id >= g.nPlayers) return false;
  return g.p[id].alive && !g.p[id].dying && g.p[id].hp > 0;
}

static bool anyDying() {
  for (int i = 0; i < g.nPlayers; i++) if (g.p[i].alive && g.p[i].dying) return true;
  return false;
}

static bool deathBusy() {
  if (anyDying()) return true;
  if (sPendMsgAt) return true;
  if (g.deathHold && g.msg.until > millis()) return true;
  return false;
}

static int winnerId() {
  int w = -1;
  for (int i = 0; i < g.nPlayers; i++) {
    if (g.p[i].alive) {
      if (w >= 0) return -1;
      w = i;
    }
  }
  return w;
}

static Player* self() {
  if (g.selfId < MAX_PLAYERS) return &g.p[g.selfId];
  return nullptr;
}

static void clearProjs() {
  for (int i = 0; i < MAX_PROJ; i++) g.proj[i].active = false;
}

static Proj* allocProj() {
  for (int i = 0; i < MAX_PROJ; i++) if (!g.proj[i].active) return &g.proj[i];
  return nullptr;
}

static void resetPlayerCombat(Player& pl) {
  pl.alive = true;
  pl.hp = HP_MAX;
  pl.aim = 45;
  pl.facing = 1;
  pl.weapon = WPN_BAZOOKA;
  pl.ammoG = AMMO_GRENADE;
  pl.ammoA = AMMO_AIR;
  pl.ammoT = AMMO_TELE;
  pl.ammoB = AMMO_TORCH;
  pl.ammoM = AMMO_MINE;
  pl.ammoBat = AMMO_BAT;
  pl.ammoJ = AMMO_JET;
  pl.ammoH = AMMO_HOME;
  pl.ammoD = AMMO_DRACO;
  pl.ammoS = AMMO_SENTRY;
  pl.ammoP = AMMO_PIPE;
  pl.ammoDyn = AMMO_DYN;
  pl.ammoMo = AMMO_MOLLI;
  pl.knockFrames = 0;
  pl.velY = 0;
  pl.velX = 0;
  pl.jumpsLeft = JUMP_COUNT;
  pl.act = PHASE_CONTROL;
  pl.charge = 0;
  pl.cursorX = 0;
  pl.menuSel = 0;
  pl.actAt = 0;
  pl.held = 0;
  pl.dying = false;
  pl.bursting = false;
  pl.jetting = false;
  pl.dyingAt = 0;
  pl.burstAt = 0;
}

static bool landSpawnAt(int x, int minAboveWater, int* outY) {
  if (x < 8 || x > WORLD_W - 9) return false;
  int y = terrainStandY(g.terrain, x);
  if (y < 0 || y > g.terrain.waterY - minAboveWater) return false;
  if (!terrainGrounded(g.terrain, x, y)) return false;
  if (!wormFits(g.terrain, x, y)) return false;
  if (outY) *outY = y;
  return true;
}

static bool spawnColumnOk(int x, int* outY) {
  int y = -1;
  if (!landSpawnAt(x, 16, &y)) return false;
  int ok = 0;
  for (int dx = -5; dx <= 5; dx++) {
    int sx = x + dx;
    if (sx < 4 || sx > WORLD_W - 5) continue;
    int sy = -1;
    if (!landSpawnAt(sx, 12, &sy)) continue;
    int dy = sy - y;
    if (dy < 0) dy = -dy;
    if (dy <= 3) ok++;
  }
  if (ok < 8) return false;
  if (outY) *outY = y;
  return true;
}

static bool spawnSpotDangerous(int x, int y, uint8_t skipId) {
  const int mineR2 = 18 * 18;
  const int sentryR2 = 28 * 28;
  const int wormR2 = 16 * 16;
  const int pipeR2 = 20 * 20;
  for (int i = 0; i < MAX_MINES; i++) {
    if (!g.mines[i].on) continue;
    int dx = x - (int)g.mines[i].x;
    int dy = y - (int)g.mines[i].y;
    if (dx * dx + dy * dy <= mineR2) return true;
  }
  for (int i = 0; i < MAX_SENTRY; i++) {
    if (!g.sentries[i].on) continue;
    int dx = x - (int)g.sentries[i].x;
    int dy = y - (int)g.sentries[i].y;
    if (dx * dx + dy * dy <= sentryR2) return true;
  }
  for (int i = 0; i < g.nPlayers; i++) {
    if ((uint8_t)i == skipId) continue;
    if (!g.p[i].alive || g.p[i].dying) continue;
    int dx = x - g.p[i].x;
    int dy = y - g.p[i].y;
    if (dx * dx + dy * dy <= wormR2) return true;
  }
  for (int i = 0; i < MAX_PROJ; i++) {
    if (!g.proj[i].active || g.proj[i].type != PRJ_PIPE) continue;
    int px = g.proj[i].x >> 8;
    int py = g.proj[i].y >> 8;
    int dx = x - px;
    int dy = y - py;
    if (dx * dx + dy * dy <= pipeR2) return true;
  }
  return false;
}

static bool pickAnyDryLand(uint8_t skipId, int* outX, int* outY) {
  int bestX = -1, bestY = 9999;
  auto consider = [&](int minAbove, bool avoidDanger) {
    for (int x = 8; x < WORLD_W - 8; x++) {
      int y = -1;
      if (!landSpawnAt(x, minAbove, &y)) continue;
      if (avoidDanger && spawnSpotDangerous(x, y, skipId)) continue;
      if (y < bestY) {
        bestY = y;
        bestX = x;
      }
    }
  };
  consider(16, true);
  if (bestX < 0) consider(16, false);
  if (bestX < 0) consider(6, false);
  if (bestX < 0) {
    for (int i = 0; i < g.nPlayers; i++) {
      if ((uint8_t)i == skipId) continue;
      if (!g.p[i].alive || g.p[i].dying) continue;
      if (g.p[i].y + 1 >= g.terrain.waterY) continue;
      int cand[3] = {g.p[i].x + 10, g.p[i].x - 10, g.p[i].x};
      for (int k = 0; k < 3; k++) {
        int y = -1;
        if (!landSpawnAt(cand[k], 4, &y)) continue;
        bestX = cand[k];
        bestY = y;
        break;
      }
      if (bestX >= 0) break;
    }
  }
  if (bestX < 0) return false;
  if (outX) *outX = bestX;
  if (outY) *outY = bestY;
  return true;
}

static bool pickSpawnXY(uint8_t skipId, int* outX, int* outY, bool avoidDanger) {
  for (int tries = 0; tries < 96; tries++) {
    int x = rng.range(12, WORLD_W - 12);
    int y = -1;
    if (!spawnColumnOk(x, &y)) continue;
    if (avoidDanger && spawnSpotDangerous(x, y, skipId)) continue;
    if (outX) *outX = x;
    if (outY) *outY = y;
    return true;
  }
  for (int sx = 12; sx < WORLD_W - 12; sx += 2) {
    int y = -1;
    if (!spawnColumnOk(sx, &y)) continue;
    if (avoidDanger && spawnSpotDangerous(sx, y, skipId)) continue;
    if (outX) *outX = sx;
    if (outY) *outY = y;
    return true;
  }
  return pickAnyDryLand(skipId, outX, outY);
}

static void spawnPlayers() {
  for (int i = 0; i < g.nPlayers; i++) {
    resetPlayerCombat(g.p[i]);
    int prefer = 28 + ((WORLD_W - 56) * i) / (g.nPlayers > 1 ? g.nPlayers - 1 : 1);
    int x = prefer;
    int y = -1;
    if (!spawnColumnOk(x, &y)) {
      for (int d = 4; d < WORLD_W / 2 && y < 0; d += 4) {
        if (spawnColumnOk(prefer + d, &y)) { x = prefer + d; break; }
        if (spawnColumnOk(prefer - d, &y)) { x = prefer - d; break; }
      }
    }
    if (y < 0) {
      for (int sx = 16; sx < WORLD_W - 16 && y < 0; sx++) {
        if (spawnColumnOk(sx, &y)) x = sx;
      }
    }
    if (y < 0) {
      if (!pickAnyDryLand((uint8_t)i, &x, &y)) {
        x = prefer;
        y = terrainStandY(g.terrain, x);
        if (y < 0 || y + 1 >= g.terrain.waterY) y = g.terrain.waterY - 20;
        if (y < wormMinY()) y = wormMinY();
      }
    }
    g.p[i].x = (int16_t)wm_clamp((int16_t)x, 8, WORLD_W - 9);
    g.p[i].y = (int16_t)y;
    g.p[i].facing = (x < WORLD_W / 2) ? 1 : -1;
    g.p[i].aim = (g.p[i].facing > 0) ? 45 : 135;
    uint8_t lv = g.livesOpt;
    if (lv < LIVES_MIN) lv = LIVES_MIN;
    if (lv > LIVES_MAX) lv = LIVES_MAX;
    g.p[i].lives = livePlay() ? lv : 0;
  }
}

static void startMatchCommon() {
  if (g.mapKind >= MAP_COUNT) g.mapKind = MAP_ISLAND;
  if (g.livesOpt < LIVES_MIN || g.livesOpt > LIVES_MAX) g.livesOpt = 3;
  g.turnMs = turnMsFor(g.turnOpt);
  terrainGenerate(g.terrain, g.mapSeed, g.mapKind);
  g.waterY = g.terrain.waterY;
  spawnPlayers();
  for (int i = 0; i < MAX_PLAYERS; i++) {
    sSeenX[i] = g.p[i].x;
    sSeenY[i] = g.p[i].y;
    sFireLockUntil[i] = 0;
  }
  rng.s = g.gameSeed ? g.gameSeed : 1;
  g.turn = 0;
  if (g.nPlayers > 0) {
    uint8_t start = (uint8_t)rng.range(0, g.nPlayers);
    g.turn = start;
    for (int n = 0; n < g.nPlayers; n++) {
      uint8_t i = (uint8_t)((start + n) % g.nPlayers);
      if (g.p[i].alive && g.p[i].connected) {
        g.turn = i;
        break;
      }
    }
  }
  g.roundN = 0;
  g.turnsThisRound = 0;
  g.sudden = false;
  g.phase = PHASE_TURN_START;
  g.phaseAt = millis();
  g.charge = 0;
  g.cursorX = (uint8_t)(WORLD_W / 2);
  g.menuSel = 0;
  g.homeTarget = 0;
  g.homeLocked = false;
  sDracoLeft = 0;
  sDracoAcc = 0;
  g.winner = PLAYER_NONE;
  g.pausedByHost = false;
  g.heldButtons = 0;
  g.matchAt = millis();
  g.liveRiseAt = 0;
  g.turnBannerUntil = 0;
  g.turnBannerId = 0;
  g.explodeUntil = 0;
  g.camFocus = 0;
  g.camFocusUntil = 0;
  g.camFocusX = 0;
  g.camFocusY = 0;
  memset(&g.msg, 0, sizeof(g.msg));
  clearProjs();
  memset(g.mines, 0, sizeof(g.mines));
  memset(g.sentries, 0, sizeof(g.sentries));
  memset(sPeerOn, 0, sizeof(sPeerOn));
}

// ---------- packets ----------

static void sendRaw(const uint8_t* mac, uint8_t* buf, BufW& w) {
  if (g.practice) return;
  int n = pkt::finish(buf, w);
  if (n <= 0) return;
  if (mac) netSend(mac, buf, (uint8_t)n);
  else netBroadcast(buf, (uint8_t)n);
}

static void sendDiscovery() {
  uint8_t buf[48];
  BufW w(buf, sizeof(buf));
  pkt::writeHdr(w, pkt::DISCOVERY, 0, PLAYER_NONE, g.txSeq++);
  w.raw(g.selfMac, 6);
  bool hosting = g.isHost && g.mode == MODE_LOBBY;
  w.u8(hosting ? 1 : 0);
  w.u8(g.nPlayers);
  w.u32(g.session);
  w.u8(g.mapKind);
  w.u8(g.live ? 1 : 0);
  w.u8(g.joining ? 1 : 0);
  if (g.joining) w.raw(g.joinMac, 6);
  else {
    uint8_t z[6] = {};
    w.raw(z, 6);
  }
  sendRaw(nullptr, buf, w);
}

static void sendJoin(const uint8_t* hostMac) {
  if (g.practice) return;
  uint8_t buf[32];
  BufW w(buf, sizeof(buf));
  pkt::writeHdr(w, pkt::JOIN, 0, PLAYER_NONE, g.txSeq++);
  w.raw(g.selfMac, 6);
  int n = pkt::finish(buf, w);
  if (n <= 0) return;
  if (hostMac) netSend(hostMac, buf, (uint8_t)n);
  netBroadcast(buf, (uint8_t)n);
}

static void sendReady() {
  uint8_t buf[32];
  BufW w(buf, sizeof(buf));
  pkt::writeHdr(w, pkt::READY, g.session, g.selfId, g.txSeq++);
  uint8_t on = (g.selfId < MAX_PLAYERS && g.p[g.selfId].ready) ? 1 : 0;
  w.u8(on);
  w.raw(g.selfMac, 6);
  int n = pkt::finish(buf, w);
  if (n <= 0 || g.practice) return;
  if (g.nPlayers > 0 && !macZero(g.p[0].mac)) netSend(g.p[0].mac, buf, (uint8_t)n);
  netBroadcast(buf, (uint8_t)n);
}

static void sendHeartbeat() {
  uint8_t buf[24];
  BufW w(buf, sizeof(buf));
  pkt::writeHdr(w, pkt::HEARTBEAT, g.session, g.selfId, g.txSeq++);
  w.u8(g.mode);
  sendRaw(nullptr, buf, w);
}

static void sendLobby() {
  if (!g.isHost) return;
  uint8_t buf[80];
  BufW w(buf, sizeof(buf));
  pkt::writeHdr(w, pkt::LOBBY, g.session, g.selfId, g.txSeq++);
  w.u8(g.nPlayers);
  for (int i = 0; i < MAX_PLAYERS; i++) {
    w.raw(g.p[i].mac, 6);
    uint8_t fl = 0;
    if (g.p[i].present) fl |= 1;
    if (g.p[i].ready) fl |= 2;
    if (g.p[i].connected) fl |= 4;
    w.u8(fl);
  }
  w.u8(g.mapKind);
  w.u8(g.turnOpt);
  w.u8(g.live ? 1 : 0);
  sendRaw(nullptr, buf, w);
}

static void sendStart() {
  uint8_t buf[64];
  BufW w(buf, sizeof(buf));
  pkt::writeHdr(w, pkt::START, g.session, g.selfId, g.txSeq++);
  w.u8(g.nPlayers);
  w.u32(g.mapSeed);
  w.u32(g.gameSeed);
  w.u8(g.waterY);
  w.u8(g.mapKind);
  w.u8(g.turnOpt);
  w.u8(g.live ? 1 : 0);
  w.u8(g.cheat ? 1 : 0);
  w.u8(g.livesOpt);
  for (int i = 0; i < MAX_PLAYERS; i++) w.raw(g.p[i].mac, 6);
  sendRaw(nullptr, buf, w);
}

static void sendInput() {
  uint8_t buf[40];
  BufW w(buf, sizeof(buf));
  pkt::writeHdr(w, pkt::INP, g.session, g.selfId, g.txSeq++);
  uint8_t down = 0, pr = 0, rel = 0;
  for (int i = 0; i < BTN_COUNT; i++) {
    if (btnDown(i)) down |= (uint8_t)(1 << i);
    if (btnPressed(i)) pr |= (uint8_t)(1 << i);
    if (btnReleased(i)) rel |= (uint8_t)(1 << i);
  }
  w.u8(down); w.u8(pr); w.u8(rel);
  Player* me = self();
  w.i16(me ? me->aim : 45);
  w.u8(me ? me->weapon : 0);
  uint8_t cx = g.cursorX;
  if (g.phase == PHASE_TARGET && me && me->weapon == WPN_HOMING) cx = g.homeTarget;
  w.u8(cx);
  w.u8(g.menuSel);
  uint8_t act = 0;
  if (btnPressed(BTN_FIRE)) act |= 1;
  if (btnReleased(BTN_FIRE)) act |= 2;
  act |= sPendingAct;
  sPendingAct = 0;
  if (me && me->facing < 0) act |= 16;
  w.u8(act);
  w.u8(me ? (uint8_t)wm_clamp(me->x, 0, WORLD_W - 1) : 0);
  w.u8(me ? packY(me->y) : packY(0));
  w.i8(me ? me->velY : 0);
  w.i8(me ? me->velX : 0);
  sendRaw(nullptr, buf, w);
}

static void sendSnapshot() {
  if (!g.isHost || g.practice || g.session == 0) return;
  if (sChunkOut < MAP_CHUNK_N) return;
  uint8_t buf[PKT_MAX];
  BufW w(buf, PKT_MAX);
  pkt::writeHdr(w, pkt::SNAPSHOT, g.session, g.selfId, g.txSeq++);
  uint8_t snapMode = (uint8_t)g.mode;
  if (g.setupFromRematch && g.mode == MODE_SETUP) snapMode = (uint8_t)MODE_REMATCH;
  w.u8(snapMode);
  w.u8((uint8_t)g.phase);
  w.u8(g.turn);
  w.u8(g.nPlayers);
  w.i8(g.wind);
  w.u8(g.waterY);
  w.u16(g.turnLeft);
  w.u8(g.charge);
  w.u8(g.cursorX);
  w.u8(g.menuSel);
  w.u8(g.roundN);
  w.u8(g.sudden ? 1 : 0);
  w.u8(g.live ? 1 : 0);
  w.u8(g.winner);
  w.u8(g.explodeX); w.u8(g.explodeY); w.u8(g.explodeR);
  static uint32_t sCrcAt;
  uint32_t now = millis();
  uint16_t crc = 0;
  if (now - sCrcAt > 8000) {
    crc = terrainCrc(g.terrain);
    sCrcAt = now;
  }
  w.u16(crc);
  for (int i = 0; i < MAX_PLAYERS; i++) {
    const Player& pl = g.p[i];
    uint8_t fl = 0;
    if (pl.present) fl |= 1;
    if (pl.ready) fl |= 2;
    if (pl.alive) fl |= 4;
    if (pl.connected) fl |= 8;
    if (pl.facing < 0) fl |= 16;
    if (pl.dying) fl |= 32;
    if (pl.bursting) fl |= 64;
    if (pl.jetting || pl.act == PHASE_JETPACK || sJetWasOn[i]) fl |= 128;
    w.u8(fl);
    w.u8(pl.hp);
    w.u8((uint8_t)wm_clamp(pl.x, 0, WORLD_W - 1));
    w.u8(packY(pl.y));
    w.u8(pl.weapon);
    w.u8(pl.ammoG); w.u8(pl.ammoA); w.u8(pl.ammoT); w.u8(pl.ammoB); w.u8(pl.ammoM);
    w.u8((uint8_t)((pl.ammoBat & 0x0F) | ((pl.ammoMo & 0x0F) << 4))); w.u8(pl.ammoJ); w.u8(pl.ammoH); w.u8(pl.ammoD);
    w.u8((uint8_t)((pl.ammoS & 0x0F) | ((pl.ammoP & 0x0F) << 4)));
    w.i16(pl.aim);
    w.i8(pl.velY);
    w.i8(pl.velX);
    w.u8(pl.act);
    w.u8(pl.charge);
    w.u8(pl.cursorX);
    w.u8(pl.menuSel);
    uint8_t held = pl.held;
    if (i == g.selfId) {
      held = 0;
      for (int b = 0; b < BTN_COUNT; b++)
        if (btnDown(b)) held |= (uint8_t)(1 << b);
    }
    w.u8(held);
  }
  {
    uint8_t livesPack = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
      uint8_t lv = g.p[i].lives;
      if (lv > 0) lv--;
      if (lv > 3) lv = 3;
      livesPack |= (uint8_t)(lv << (i * 2));
    }
    w.u8(livesPack);
  }
  for (int i = 0; i < MAX_PLAYERS; i++) {
    int8_t a = (g.dmgUntil[i] > millis()) ? g.dmgAmt[i] : 0;
    w.i8(a);
  }
  for (int i = 0; i < MAX_PROJ; i++) {
    const Proj& pr = g.proj[i];
    w.u8(pr.active ? (uint8_t)(pr.type + 1) : 0);
    w.u8((uint8_t)wm_clamp((int16_t)(pr.x >> 8), 0, WORLD_W - 1));
    w.u8((uint8_t)wm_clamp((int16_t)((pr.y >> 8) + 64), 0, 255));
    int32_t pvx = pr.vx, pvy = pr.vy;
    if (pvx > 32767) pvx = 32767;
    if (pvx < -32767) pvx = -32767;
    if (pvy > 32767) pvy = 32767;
    if (pvy < -32767) pvy = -32767;
    w.i16((int16_t)pvx);
    w.i16((int16_t)pvy);
    w.u8(pr.owner);
    uint16_t fu = (pr.type == PRJ_PIPE) ? (uint16_t)((pr.fuseMs + 11) / 12)
                                       : (uint16_t)((pr.fuseMs + 7) / 8);
    if (fu > 255) fu = 255;
    w.u8((uint8_t)fu);
  }
  w.u8((uint8_t)((g.homeTarget & 0x7F) | (g.homeLocked ? 0x80 : 0)));
  for (int i = 0; i < MAX_MINES; i++) {
    const Mine& mn = g.mines[i];
    uint8_t fl = 0;
    if (mn.on) fl |= 1;
    if (mn.on && now >= mn.armAt) fl |= 2;
    if (mn.kind) fl |= 4;
    fl |= (uint8_t)((mn.owner & 3) << 3);
    w.u8(fl);
    w.u8(mn.x);
    w.u8(mn.y);
    uint8_t remain = 0;
    if (mn.on && now < mn.armAt) {
      uint32_t left = mn.armAt - now;
      uint32_t q = (left + 19) / 20;
      if (q > 255) q = 255;
      if (q == 0) q = 1;
      remain = (uint8_t)q;
    }
    w.u8(remain);
  }
  for (int i = 0; i < MAX_SENTRY; i++) {
    const Sentry& st = g.sentries[i];
    uint8_t fl = 0;
    if (st.on) fl |= 1;
    if (st.facing < 0) fl |= 2;
    w.u8(fl);
    w.u8(st.owner);
    w.u8(st.x);
    w.u8(st.y);
    w.u8(st.hp);
    w.u8(st.shots);
    int aim = st.aim;
    while (aim < 0) aim += 360;
    while (aim >= 360) aim -= 360;
    w.u8((uint8_t)((aim * 256) / 360));
  }
  if (w.overflow) {
    Serial.println(F("snap overflow"));
    return;
  }
  sendRaw(nullptr, buf, w);
  g.lastSnapSend = millis();
}

static void sendExplosion(int x, int y, int r) {
  uint8_t buf[32];
  BufW w(buf, sizeof(buf));
  pkt::writeHdr(w, pkt::EXPLOSION, g.session, g.selfId, g.txSeq++);
  w.u8((uint8_t)x); w.u8((uint8_t)y); w.u8((uint8_t)r);
  sendRaw(nullptr, buf, w);
}

static void sendEvent(uint8_t kind, uint8_t pid) {
  uint8_t buf[24];
  BufW w(buf, sizeof(buf));
  pkt::writeHdr(w, pkt::EVENT, g.session, g.selfId, g.txSeq++);
  w.u8(kind); w.u8(pid);
  sendRaw(nullptr, buf, w);
}

static void sendTerrainChunk(uint8_t idx) {
  if (idx >= MAP_CHUNK_N) return;
  uint8_t buf[PKT_MAX];
  BufW w(buf, PKT_MAX);
  pkt::writeHdr(w, pkt::TERRAIN_CHUNK, g.session, g.selfId, g.txSeq++);
  w.u8(idx);
  w.raw(g.terrain.bits + idx * MAP_CHUNK_LEN, MAP_CHUNK_LEN);
  sendRaw(nullptr, buf, w);
}

static void sendTerrainReq() {
  uint8_t buf[20];
  BufW w(buf, sizeof(buf));
  pkt::writeHdr(w, pkt::TERRAIN_REQ, g.session, g.selfId, g.txSeq++);
  sendRaw(nullptr, buf, w);
}

static void sendPause(uint8_t on) {
  uint8_t buf[20];
  BufW w(buf, sizeof(buf));
  pkt::writeHdr(w, pkt::PAUSE, g.session, g.selfId, g.txSeq++);
  w.u8(on);
  sendRaw(nullptr, buf, w);
}

static void sendQuit() {
  if (g.practice) return;
  if (g.isHost && g.session) sendEvent(EVT_HOST_LOST, 0);
  uint8_t buf[20];
  BufW w(buf, sizeof(buf));
  uint32_t sess = g.session ? g.session : g.joinSession;
  pkt::writeHdr(w, pkt::QUIT, sess, g.selfId, g.txSeq++);
  sendRaw(nullptr, buf, w);
}

static void sendRematch() {
  uint8_t buf[24];
  BufW w(buf, sizeof(buf));
  pkt::writeHdr(w, pkt::REMATCH, g.session, g.selfId, g.txSeq++);
  uint8_t on = (g.selfId < MAX_PLAYERS && g.p[g.selfId].ready) ? 1 : 0;
  w.u8(on);
  sendRaw(nullptr, buf, w);
}

// ---------- roster ----------

static void notePeer(const uint8_t* mac) {
  if (!mac || macZero(mac) || macEq(mac, g.selfMac)) return;
  uint32_t now = millis();
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (!g.p[i].present) continue;
    if (macEq(g.p[i].mac, mac) || (!macZero(g.p[i].rxMac) && macEq(g.p[i].rxMac, mac))) {
      g.p[i].lastSeen = now;
      g.p[i].connected = true;
      netEnsurePeer(mac);
      return;
    }
  }
}

static void rememberFound(const uint8_t* mac, uint32_t session, uint8_t nPlayers,
                          uint8_t mapKind, uint8_t live) {
  if (!mac || macEq(mac, g.selfMac)) return;
  uint32_t now = millis();
  int slot = -1;
  for (int i = 0; i < g.nFound; i++) {
    if (macEq(g.found[i].mac, mac) || (session && g.found[i].session == session)) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    if (g.nFound < MAX_FOUND) slot = g.nFound++;
    else {
      slot = 0;
      for (int i = 1; i < g.nFound; i++)
        if (g.found[i].seen < g.found[slot].seen) slot = i;
    }
    Serial.printf("found host %02X:%02X:%02X:%02X:%02X:%02X  %u/4\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                  (unsigned)nPlayers);
  }
  memcpy(g.found[slot].mac, mac, 6);
  g.found[slot].session = session;
  g.found[slot].nPlayers = nPlayers;
  g.found[slot].mapKind = mapKind;
  g.found[slot].live = live;
  g.found[slot].seen = now;
}

static void hostRemovePlayer(int i) {
  if (!g.isHost || i <= 0 || i >= g.nPlayers) return;
  for (int j = i; j < g.nPlayers - 1; j++) g.p[j] = g.p[j + 1];
  g.nPlayers--;
  memset(&g.p[g.nPlayers], 0, sizeof(Player));
  if (g.mode == MODE_LOBBY) {
    sendLobby();
    sendLobby();
    sendDiscovery();
  }
}

static void hostDropStale(uint32_t now) {
  if (!g.isHost || g.mode != MODE_LOBBY) return;
  for (int i = g.nPlayers - 1; i >= 1; i--) {
    if (!g.p[i].present) continue;
    if (aged(now, g.p[i].lastSeen, HOST_LOST_MS)) hostRemovePlayer(i);
  }
}

static int findPlayerMac(const uint8_t* a, const uint8_t* b) {
  for (int i = 0; i < g.nPlayers; i++) {
    if (!g.p[i].present) continue;
    if (a && !macZero(a) && (macEq(g.p[i].mac, a) || (!macZero(g.p[i].rxMac) && macEq(g.p[i].rxMac, a))))
      return i;
    if (b && !macZero(b) && (macEq(g.p[i].mac, b) || (!macZero(g.p[i].rxMac) && macEq(g.p[i].rxMac, b))))
      return i;
  }
  return -1;
}

static void hostAddJoiner(const uint8_t* claimed, const uint8_t* radio) {
  if (!g.isHost) return;
  const uint8_t* id = (!macZero(claimed) && !macEq(claimed, g.selfMac)) ? claimed : radio;
  if (macZero(id) || macEq(id, g.selfMac)) return;
  uint32_t now = millis();
  int i = findPlayerMac(claimed, radio);
  if (i == 0) i = -1;
  if (i >= 0) {
    g.p[i].lastSeen = now;
    g.p[i].connected = true;
    g.p[i].present = true;
    g.p[i].seqInit = false;
    g.p[i].lastSeq = 0;
    if (!macZero(radio)) memcpy(g.p[i].rxMac, radio, 6);
    netEnsurePeer(g.p[i].mac);
    if (!macZero(radio)) netEnsurePeer(radio);
    return;
  }
  if (g.mode != MODE_LOBBY) return;
  if (g.nPlayers >= MAX_PLAYERS) {
    sendLobby();
    return;
  }
  i = g.nPlayers;
  memset(&g.p[i], 0, sizeof(Player));
  memcpy(g.p[i].mac, id, 6);
  if (!macZero(radio)) memcpy(g.p[i].rxMac, radio, 6);
  g.p[i].present = g.p[i].connected = true;
  g.p[i].lastSeen = now;
  g.nPlayers++;
  netEnsurePeer(g.p[i].mac);
  if (!macZero(radio)) netEnsurePeer(radio);
  sendLobby();
  sendLobby();
  Serial.printf("joiner %02X:%02X:%02X:%02X:%02X:%02X  now %u\n",
                g.p[i].mac[0], g.p[i].mac[1], g.p[i].mac[2],
                g.p[i].mac[3], g.p[i].mac[4], g.p[i].mac[5],
                (unsigned)g.nPlayers);
}

static bool allLobbyReady() {
  if (g.nPlayers < 2) return false;
  for (int i = 0; i < g.nPlayers; i++) if (!g.p[i].ready) return false;
  return true;
}

static void hostTryStart() {
  if (g.nPlayers < 2) {
    g.lobbyWarn = 1;
    g.lobbyWarnUntil = millis() + 1800;
    return;
  }
  if (!allLobbyReady()) {
    g.lobbyWarn = 2;
    g.lobbyWarnUntil = millis() + 1800;
    return;
  }
  hostBeginGame();
}

static void applyLobbyRoster(Player np[MAX_PLAYERS], uint8_t n, uint32_t session, uint16_t seq,
                             uint8_t mapKind, uint8_t turnOpt, uint8_t live) {
  uint8_t keepRx[MAX_PLAYERS][6];
  uint8_t keepMac[MAX_PLAYERS][6];
  for (int i = 0; i < MAX_PLAYERS; i++) {
    memcpy(keepRx[i], g.p[i].rxMac, 6);
    memcpy(keepMac[i], g.p[i].mac, 6);
  }
  memcpy(g.p, np, sizeof(g.p));
  g.nPlayers = n;
  g.session = session;
  g.selfId = PLAYER_NONE;
  for (int i = 0; i < n; i++) {
    if (macEq(g.p[i].mac, g.selfMac)) g.selfId = (uint8_t)i;
    if (!macZero(g.p[i].rxMac)) continue;
    for (int j = 0; j < MAX_PLAYERS; j++) {
      if (macEq(g.p[i].mac, keepMac[j]) && !macZero(keepRx[j])) {
        memcpy(g.p[i].rxMac, keepRx[j], 6);
        break;
      }
    }
  }
  g.isHost = (g.selfId == 0 && macEq(g.p[0].mac, g.selfMac));
  sLobbySeq = seq;
  sLobbySeqInit = true;
  if (mapKind < MAP_COUNT) g.mapKind = mapKind;
  if (turnOpt < TURN_OPT_N) g.turnOpt = turnOpt;
  g.turnMs = turnMsFor(g.turnOpt);
  g.live = live != 0;
  sHeardAt = tickNow();
}

static bool lobbyIsJoinTarget(const uint8_t* srcMac, const uint8_t* hostMac, uint32_t session) {
  if (!g.joining) return false;
  if (srcMac && macEq(srcMac, g.joinMac)) return true;
  if (hostMac && macEq(hostMac, g.joinMac)) return true;
  if (g.joinSession && session == g.joinSession) return true;
  return false;
}

static void resetMatchNetState() {
  g.hostSeqInit = false;
  g.hostSeqSeen = 0;
  g.txSeq = 1;
  sWantPause = false;
  sQuietUntil = 0;
  sPendingAct = 0;
  sGesture = 0;
  sLobbySeqInit = false;
  sLobbySeq = 0;
  memset(sPeerOn, 0, sizeof(sPeerOn));
  for (int i = 0; i < MAX_PLAYERS; i++) {
    g.p[i].seqInit = false;
    g.p[i].lastSeq = 0;
  }
}

static void returnToTitle() {
  netFlush();
  g.mode = MODE_TITLE;
  g.practice = false;
  g.session = 0;
  g.nPlayers = 0;
  g.selfId = PLAYER_NONE;
  g.isHost = false;
  g.joining = false;
  g.autoJoin = false;
  g.nFound = 0;
  g.joinSession = 0;
  sHeardAt = 0;
  memset(g.p, 0, sizeof(g.p));
  resetMatchNetState();
}

static void returnToMenu() {
  returnToTitle();
  g.mode = MODE_MAIN_MENU;
}

static void clientHostGone() {
  if (g.isHost || g.practice) return;
  if (g.mode == MODE_HOST_LOST) return;
  if (g.mode == MODE_LOBBY) {
    sAllowLeaveLobby = true;
    beginMpMenu();
    return;
  }
  if (!inSessionMode()) return;
  g.mode = MODE_HOST_LOST;
  g.phaseAt = tickNow();
  g.session = 0;
  g.joinSession = 0;
  g.joining = false;
  sHeardAt = 0;
  memset(&g.msg, 0, sizeof(g.msg));
}

static void beginMpMenu() {
  if (joinerInLiveLobby()) {
    g.mode = MODE_LOBBY;
    g.lobbyQuitAsk = false;
    return;
  }
  sAllowLeaveLobby = false;
  netFlush();
  g.mode = MODE_MP_MENU;
  sHeardAt = 0;
  g.mpSel = 0;
  g.practice = false;
  g.lobbyQuitAsk = false;
  g.setupFromRematch = false;
  g.session = 0;
  g.nPlayers = 0;
  g.selfId = PLAYER_NONE;
  g.isHost = false;
  g.joining = false;
  g.autoJoin = false;
  g.nFound = 0;
  g.joinSession = 0;
  memset(g.p, 0, sizeof(g.p));
  resetMatchNetState();
  g.phaseAt = millis();
}

static void beginHostLobby() {
  netFlush();
  g.mode = MODE_LOBBY;
  g.practice = false;
  g.isHost = true;
  g.selfId = 0;
  g.joining = false;
  g.autoJoin = false;
  g.nFound = 0;
  g.session = esp_random();
  if (g.session == 0) g.session = 1;
  g.nPlayers = 1;
  memset(g.p, 0, sizeof(g.p));
  memcpy(g.p[0].mac, g.selfMac, 6);
  g.p[0].present = g.p[0].connected = true;
  g.p[0].ready = false;
  g.p[0].lastSeen = millis();
  g.phaseAt = millis();
  g.lobbySel = 0;
  g.lobbyQuitAsk = false;
  resetMatchNetState();
  sendDiscovery();
  sendLobby();
}

static void beginSeek(bool autoJoin) {
  if (joinerInLiveLobby()) {
    g.mode = MODE_LOBBY;
    g.lobbyQuitAsk = false;
    return;
  }
  FoundHost keep[MAX_FOUND];
  uint8_t nKeep = g.nFound;
  uint8_t sel = g.seekSel;
  memcpy(keep, g.found, sizeof(keep));
  bool reuse = (g.mode == MODE_SEEK || g.mode == MODE_LOBBY || g.mode == MODE_GROUP_FULL);
  netFlush();

  g.mode = MODE_SEEK;
  g.practice = false;
  g.isHost = false;
  g.selfId = PLAYER_NONE;
  g.session = 0;
  g.nPlayers = 0;
  g.joining = false;
  g.autoJoin = autoJoin;
  memset(g.p, 0, sizeof(g.p));
  memset(g.joinMac, 0, 6);
  g.joinSession = 0;
  resetMatchNetState();
  g.phaseAt = millis();
  if (reuse && nKeep) {
    g.nFound = nKeep;
    memcpy(g.found, keep, sizeof(keep));
    g.seekSel = sel < nKeep ? sel : 0;
  } else {
    g.nFound = 0;
    g.seekSel = 0;
  }
}

static void startJoining(uint8_t idx) {
  if (idx >= g.nFound) return;
  netFlush();
  FoundHost h = g.found[idx];
  g.joining = true;
  memcpy(g.joinMac, h.mac, 6);
  g.joinSession = h.session;
  g.phaseAt = millis();
  g.lastHb = millis();
  sLobbySeqInit = false;
  sendJoin(g.joinMac);
  sendJoin(g.joinMac);
  sendJoin(g.joinMac);
}

static void beginPractice() {
  netFlush();
  g.practice = true;
  g.live = false;
  g.isHost = true;
  g.session = 0;
  g.nPlayers = 2;
  g.selfId = 0;
  memset(g.p, 0, sizeof(g.p));
  memcpy(g.p[0].mac, g.selfMac, 6);
  g.p[0].present = g.p[0].connected = g.p[0].ready = true;
  memset(g.p[1].mac, 0x11, 6);
  g.p[1].mac[5] = 0x02;
  g.p[1].present = g.p[1].connected = g.p[1].ready = true;
  g.mapSeed = esp_random();
  g.gameSeed = esp_random();
  startMatchCommon();
  g.mode = MODE_PLAY;
  setMsg("PRACTICE", "HOTSEAT", 800);
}

static void beginPracticeSetup() {
  g.mode = MODE_SETUP;
  g.practice = false;
  g.live = false;
  g.lobbySel = 0;
  g.setupHost = false;
  g.setupFromRematch = false;
  g.phaseAt = millis();
}

static void beginHostSetup() {
  g.mode = MODE_SETUP;
  g.practice = false;
  g.lobbySel = 0;
  g.setupHost = true;
  g.setupFromRematch = false;
  g.phaseAt = millis();
}

static void beginBtnCfg() {
  g.mode = MODE_BTN_CFG;
  g.cfgStep = 0xFF;
  sCfgWaitRelease = false;
  g.phaseAt = millis();
}

static uint8_t setupRowN() { return g.setupHost ? SETUP_HOST_ROWS : SETUP_PRAC_ROWS; }

static void cycleSetup(int d) {
  if (d < 0) d = -1;
  else d = 1;
  if (g.lobbySel == 0) {
    g.mapKind = (uint8_t)((g.mapKind + MAP_COUNT + d) % MAP_COUNT);
  } else if (g.lobbySel == 1) {
    if (g.live && g.setupHost) return;
    int v = (int)g.turnOpt + d;
    if (v < 0) v = 0;
    if (v >= TURN_OPT_N) v = TURN_OPT_N - 1;
    g.turnOpt = (uint8_t)v;
    g.turnMs = turnMsFor(g.turnOpt);
  } else if (g.setupHost && g.lobbySel == 2) {
    g.live = !g.live;
  } else if (g.setupHost && g.lobbySel == 3) {
    if (!g.live) return;
    int v = (int)g.livesOpt + d;
    if (v < LIVES_MIN) v = LIVES_MAX;
    if (v > LIVES_MAX) v = LIVES_MIN;
    g.livesOpt = (uint8_t)v;
  } else if ((!g.setupHost && g.lobbySel == 2) || (g.setupHost && g.lobbySel == 4)) {
    g.cheat = !g.cheat;
  }
}

static uint32_t newSessionId() {
  uint32_t s = millis() ^ ((uint32_t)g.selfMac[4] << 8) ^ g.selfMac[5] ^ esp_random();
  return s ? s : 1u;
}

static void hostBeginGame() {
  uint32_t prev = g.session;
  do {
    g.session = newSessionId();
  } while (g.session == prev);
  g.mapSeed = esp_random();
  g.gameSeed = esp_random();
  uint32_t now = millis();
  for (int i = 0; i < g.nPlayers; i++) {
    g.p[i].ready = false;
    g.p[i].connected = true;
    g.p[i].lastSeen = now;
    g.p[i].seqInit = false;
    g.p[i].lastSeq = 0;
  }
  sChunkOut = 0xFF;
  g.lastChunkReq = now;
  resetMatchNetState();
  startMatchCommon();
  g.mode = MODE_COUNTDOWN;
  g.phaseAt = millis();
  sLastStartTx = g.phaseAt;
  sHeardAt = g.phaseAt;
  sendStart();
}

static void applyStart(uint8_t n, uint32_t mapSeed, uint32_t gameSeed, uint8_t waterY, uint8_t mapKind,
                       uint8_t turnOpt, uint8_t live, uint8_t cheat, uint8_t livesOpt,
                       uint8_t macs[MAX_PLAYERS][6]) {
  g.nPlayers = n;
  g.mapSeed = mapSeed;
  g.gameSeed = gameSeed;
  g.mapKind = mapKind < MAP_COUNT ? mapKind : MAP_ISLAND;
  g.turnOpt = turnOpt < TURN_OPT_N ? turnOpt : 1;
  g.live = live != 0;
  g.cheat = cheat != 0;
  g.livesOpt = livesOpt;
  if (g.livesOpt < LIVES_MIN) g.livesOpt = LIVES_MIN;
  if (g.livesOpt > LIVES_MAX) g.livesOpt = LIVES_MAX;
  g.turnMs = turnMsFor(g.turnOpt);
  memset(g.p, 0, sizeof(g.p));
  for (int i = 0; i < n && i < MAX_PLAYERS; i++) {
    memcpy(g.p[i].mac, macs[i], 6);
    g.p[i].present = g.p[i].connected = true;
    g.p[i].lastSeen = millis();
    netEnsurePeer(g.p[i].mac);
  }
  g.selfId = PLAYER_NONE;
  for (int i = 0; i < n; i++) if (macEq(g.p[i].mac, g.selfMac)) g.selfId = (uint8_t)i;
  g.isHost = (g.selfId == 0 && macEq(g.p[0].mac, g.selfMac));
  sChunkOut = 0xFF;
  startMatchCommon();
  g.terrain.waterY = waterY;
  g.waterY = waterY;
  g.mode = MODE_COUNTDOWN;
  g.phaseAt = millis();
  sHeardAt = tickNow();
  sQuietUntil = millis() + 2000;
  g.lastChunkReq = millis();
  g.hostSeqInit = false;
  g.hostSeqSeen = 0;
  g.txSeq = 1;
  sWantPause = false;
  sPendingAct = 0;
  for (int i = 0; i < g.nPlayers; i++) {
    g.p[i].lastSeen = millis();
    g.p[i].seqInit = false;
    g.p[i].lastSeq = 0;
  }
}

// ---------- combat ----------

static void showDamage(uint8_t id, int dmg) {
  if (id >= g.nPlayers) return;
  g.dmgAmt[id] = (int8_t)(-dmg);
  g.dmgX[id] = g.p[id].x;
  g.dmgY[id] = (int16_t)(g.p[id].y - 12);
  g.dmgUntil[id] = millis() + DMG_POP_MS;
}

static void showDrownDamage(uint8_t id) {
  if (id >= g.nPlayers) return;
  g.dmgAmt[id] = (int8_t)(-100);
  g.dmgX[id] = g.p[id].x;
  int wy = (int)g.terrain.waterY - 16;
  if (wy < 2) wy = 2;
  g.dmgY[id] = (int16_t)wy;
  g.dmgUntil[id] = millis() + DROWN_POP_MS;
}

static void flingPlayer(Player& pl, int vx, int vy) {
  if (vy >= 0 && (vx <= -2 || vx >= 2)) vy = -2;
  pl.velX = (int8_t)wm_clamp((int16_t)vx, -8, 8);
  pl.velY = (int8_t)wm_clamp((int16_t)vy, -8, 4);
}

static bool inWater(const Player& pl) {
  return pl.y + 1 >= g.terrain.waterY;
}

static bool isActionPhase() {
  return g.phase == PHASE_CONTROL || g.phase == PHASE_WEAPON_MENU || g.phase == PHASE_TARGET ||
         g.phase == PHASE_CHARGING || g.phase == PHASE_BLOWTORCH || g.phase == PHASE_MOLLI ||
         g.phase == PHASE_BAT || g.phase == PHASE_JETPACK || g.phase == PHASE_DRACO;
}

static void abortMenusOnDeath(uint8_t id) {
  if (id >= g.nPlayers) return;
  Player& pl = g.p[id];
  PlayPhase idle = livePlay() ? PHASE_CONTROL : PHASE_FALLING;
  pl.act = (uint8_t)idle;
  if (!livePlay() && g.turn == id && isActionPhase()) {
    g.phase = idle;
    g.phaseAt = millis();
  }
  if (id < MAX_PLAYERS) sJetWasOn[id] = false;
}

static void respawnPlayer(uint8_t id) {
  if (id >= g.nPlayers) return;
  Player& pl = g.p[id];
  int x = 16, y = -1;
  if (!pickSpawnXY(id, &x, &y, true) && !pickSpawnXY(id, &x, &y, false) &&
      !pickAnyDryLand(id, &x, &y)) {
    x = 16 + (int)id * 48;
    if (!landSpawnAt(x, 6, &y)) {
      y = -1;
      for (int sx = 8; sx < WORLD_W - 8; sx++) {
        int sy = -1;
        if (landSpawnAt(sx, 4, &sy)) {
          x = sx;
          y = sy;
          break;
        }
      }
    }
  }
  if (y < 0 || y + 1 >= g.terrain.waterY) {
    pl.alive = false;
    pl.hp = 0;
    pl.dying = false;
    return;
  }
  pl.alive = true;
  pl.hp = HP_MAX;
  pl.dying = false;
  pl.bursting = false;
  pl.dyingAt = 0;
  pl.burstAt = 0;
  pl.velX = 0;
  pl.velY = 0;
  pl.knockFrames = 0;
  pl.x = (int16_t)wm_clamp((int16_t)x, 8, WORLD_W - 9);
  pl.y = (int16_t)y;
  pl.facing = (pl.x < WORLD_W / 2) ? 1 : -1;
  pl.aim = (pl.facing > 0) ? 45 : 135;
  pl.jumpsLeft = JUMP_COUNT;
  pl.act = PHASE_CONTROL;
  sFireLockUntil[id] = 0;
  sWpnCd[id] = 0;
  sSeenX[id] = pl.x;
  sSeenY[id] = pl.y;
  sSplashed[id] = false;
  sDrownHoldUntil[id] = 0;
  sDrownMsgUntil[id] = 0;
  sSpawnProtectUntil[id] = millis() + 800;
  sJetWasOn[id] = false;
}

static void killPlayer(uint8_t id, const char* why) {
  if (!g.isHost && !g.practice) return;
  if (id >= g.nPlayers) return;
  if (!g.p[id].alive) return;
  bool drown = why && why[0] == 'D';
  if (drown) {
    if (sDrownHoldUntil[id] && millis() < sDrownHoldUntil[id]) return;
    if (sDrownMsgUntil[id] && millis() < sDrownMsgUntil[id]) return;
    if (livePlay() && g.p[id].hp == 0) return;
  }
  abortMenusOnDeath(id);
  char who[8];
  snprintf(who, sizeof(who), "P%u", (unsigned)(id + 1));
  if (drown) hitWater(g.p[id]);
  if (drown) {
    showDrownDamage(id);
    g.dmgHangUntil = millis() + DROWN_MSG_DELAY_MS + MSG_MS;
    sDrownMsgUntil[id] = millis() + DROWN_MSG_DELAY_MS + MSG_MS + 400;
  }
  if (livePlay() && g.p[id].lives > 0) {
    g.p[id].lives--;
    if (drown) {
      scheduleMsg(who, "UP", DROWN_MSG_DELAY_MS, MSG_MS);
      focusDeathCam(id, DROWN_MSG_DELAY_MS + MSG_MS + 400);
      g.deathHold = true;
      g.p[id].hp = 0;
      g.p[id].velX = 0;
      g.p[id].velY = 0;
      sDrownHoldUntil[id] = millis() + DROWN_MSG_DELAY_MS;
      if (g.isHost) sendEvent(EVT_DROWN, id);
      return;
    }
    respawnPlayer(id);
    setMsg(who, "UP", MSG_MS);
    return;
  }
  g.p[id].alive = false;
  g.p[id].hp = 0;
  g.p[id].dying = false;
  g.p[id].bursting = false;
  g.p[id].dyingAt = 0;
  g.p[id].burstAt = 0;
  g.p[id].velX = 0;
  g.p[id].velY = 0;
  if (drown) {
    scheduleMsg(who, why, DROWN_MSG_DELAY_MS, MSG_MS);
    focusDeathCam(id, DROWN_MSG_DELAY_MS + MSG_MS + 400);
  } else {
    setMsg(who, why, MSG_MS);
    focusDeathCam(id, MSG_MS + 400);
  }
  g.deathHold = true;
  if (g.phase == PHASE_FALLING || g.phase == PHASE_TURN_END) g.phaseAt = millis();
  if (g.isHost && why && why[0]) {
    if (drown) sendEvent(EVT_DROWN, id);
    else if (why[0] == 'K') sendEvent(EVT_KILL, id);
  }
  if (isActionPhase() && !livePlay() && (g.turn == id || livingCount() <= 1)) {
    g.phase = PHASE_FALLING;
    g.phaseAt = millis();
  }
}

static void applyDamage(uint8_t id, int dmg, int vx, int vy) {
  if (id >= g.nPlayers || !g.p[id].alive) return;
  if (dmg <= 0 && vx == 0 && vy == 0) return;
  flingPlayer(g.p[id], vx, vy);
  if (g.p[id].dying) {
    g.p[id].burstAt = 0;
    return;
  }
  if (dmg <= 0) return;
  showDamage(id, dmg);
  g.dmgHangUntil = millis() + DAMAGE_HANG_MS;
  if (g.p[id].hp > dmg) {
    g.p[id].hp = (uint8_t)(g.p[id].hp - dmg);
  } else {
    g.p[id].hp = 0;
    g.p[id].dying = true;
    g.p[id].bursting = false;
    g.p[id].dyingAt = millis();
    g.p[id].burstAt = 0;
    abortMenusOnDeath(id);
    focusDeathCam(id, DEATH_BURST_MS + 2500);
  }
  if (!livePlay() && isActionPhase() && g.turn == id) {
    g.phase = PHASE_FALLING;
    g.phaseAt = millis();
  }
}

static int explMaxDmg(uint8_t type) {
  if (type == PRJ_GRENADE) return DMG_GRENADE;
  if (type == PRJ_BOMB) return DMG_AIRSTRIKE;
  if (type == PRJ_HOME) return DMG_HOME;
  if (type == PRJ_PIPE) return DMG_PIPE;
  return DMG_BAZOOKA;
}

static void applyBlastMotion(int cx, int cy, int r) {
  int reach = r + 10;
  if (r >= 10) reach = r + 12;
  for (int i = 0; i < g.nPlayers; i++) {
    if (!g.p[i].alive) continue;
    int dx = g.p[i].x - cx;
    int dy = (g.p[i].y - 3) - cy;
    int d2 = dx * dx + dy * dy;
    if (d2 > reach * reach) continue;
    int dist = d2 > 0 ? (int)sqrtf((float)d2) : 0;
    int force = 5 + r / 2 + (reach - (dist < 1 ? 1 : dist)) / 2;
    if (force > 10) force = 10;
    int vx, vy;
    knockFrom(cx, cy, g.p[i].x, g.p[i].y - 3, force, &vx, &vy);
    flingPlayer(g.p[i], vx, vy);
  }
}

static void doExplosion(int cx, int cy, int r, int maxDmg, int skipId, int minDmg = 0) {
  cx = wm_clamp((int16_t)cx, 0, WORLD_W - 1);
  cy = wm_clamp((int16_t)cy, 0, WORLD_H - 1);
  r = wm_clamp((int16_t)r, 2, 14);
  if (maxDmg < 1) maxDmg = 1;
  terrainCrater(g.terrain, cx, cy, r);
  g.terrain.waterY = g.waterY;
  g.explodeX = (uint8_t)cx;
  g.explodeY = (uint8_t)cy;
  g.explodeR = (uint8_t)r;
  g.explodeUntil = millis() + (r >= 10 ? (EXPLODE_ANIM_MS * 2 + 80) : EXPLODE_ANIM_MS);
  spawnExplosionBurst(cx, cy, r);
  if (g.isHost) sendExplosion(cx, cy, r);
  for (int i = 0; i < g.nPlayers; i++) {
    if (!g.p[i].alive) continue;
    if (i == skipId) continue;
    int dx = g.p[i].x - cx;
    int dy = (g.p[i].y - 3) - cy;
    int d2 = dx * dx + dy * dy;
    int reach = (skipId >= 0) ? (r + 6) : (r + 10);
    if (r >= 10) reach = r + 12;
    if (d2 > reach * reach) continue;
    int dist = d2 > 0 ? (int)sqrtf((float)d2) : 0;
    int dmg;
    if (minDmg > 0) {
      dmg = minDmg + (maxDmg - minDmg) * (reach - dist) / reach;
      if (dmg < minDmg) dmg = minDmg;
    } else {
      dmg = dist >= reach ? 0 : maxDmg * (reach - dist) / reach;
    }
    if (dmg > maxDmg) dmg = maxDmg;
    int force = 5 + r / 2 + (reach - (dist < 1 ? 1 : dist)) / 2;
    if (force > 10) force = 10;
    int vx, vy;
    knockFrom(cx, cy, g.p[i].x, g.p[i].y - 3, force, &vx, &vy);
    applyDamage((uint8_t)i, dmg, vx, vy);
  }
  hurtSentries(cx, cy, r, maxDmg, 0xFF);
}

static bool hitPlayer(int px, int py, uint8_t owner, uint8_t grace, uint8_t* outId) {
  for (int i = 0; i < g.nPlayers; i++) {
    if (!g.p[i].alive) continue;
    if (grace && (uint8_t)i == owner) continue;
    int dx = g.p[i].x - px;
    int dy = (g.p[i].y - 3) - py;
    if (dx * dx + dy * dy <= 4 * 4) {
      if (outId) *outId = (uint8_t)i;
      return true;
    }
  }
  return false;
}

static void launchWeapon(Player& pl, uint8_t pid, uint8_t power) {
  if (pl.weapon == WPN_TELEPORT) {
    if (!hasAmmo(pl, WPN_TELEPORT)) { pl.weapon = WPN_BAZOOKA; return; }
    int x = g.cursorX;
    int y = terrainStandY(g.terrain, x);
    if (y < 0) {
      for (int d = 1; d < 20 && y < 0; d++) {
        y = terrainStandY(g.terrain, x + d);
        if (y < 0) y = terrainStandY(g.terrain, x - d);
        else x = x + d;
        if (y >= 0 && terrainStandY(g.terrain, x - d) >= 0 && d) { /* keep */ }
      }
    }
    if (y >= 0) {
      pl.x = (int16_t)x;
      pl.y = (int16_t)y;
      spendAmmo(pl.ammoT);
    }
    afterMelee();
    return;
  }
  if (pl.weapon == WPN_AIRSTRIKE) {
    if (!hasAmmo(pl, WPN_AIRSTRIKE)) { pl.weapon = WPN_BAZOOKA; return; }
    spendAmmo(pl.ammoA);
    int dropX = wm_clamp((int16_t)g.cursorX, 8, WORLD_W - 9);
    int first = dropX - (AIRSTRIKE_N / 2) * 8;
    int startX = first - 48;
    if (startX < 0) startX = 0;
    Proj* pr = allocProj();
    if (!pr) return;
    pr->active = true;
    pr->type = PRJ_PLANE;
    pr->owner = pid;
    pr->x = startX << 8;
    pr->y = -((int32_t)8 << 8);
    pr->vx = 360;
    pr->vy = 0;
    pr->fuseMs = (uint16_t)dropX;
    pr->grace = 0;
    pr->homing = AIRSTRIKE_N;
    afterShot();
    g.recoilFrames = 4;
    return;
  }

  bool grenade = (pl.weapon == WPN_GRENADE);
  bool sentry = (pl.weapon == WPN_SENTRY);
  bool pipe = (pl.weapon == WPN_PIPE);
  if (pl.weapon == WPN_HOMING && !hasAmmo(pl, WPN_HOMING)) pl.weapon = WPN_BAZOOKA;
  if (grenade) {
    if (!spendAmmo(pl.ammoG)) { pl.weapon = WPN_BAZOOKA; grenade = false; }
  }
  if (sentry) {
    if (!spendAmmo(pl.ammoS)) { pl.weapon = WPN_BAZOOKA; sentry = false; }
  }
  if (pipe) {
    if (!spendAmmo(pl.ammoP)) { pl.weapon = WPN_BAZOOKA; pipe = false; }
  }
  float rad = pl.aim * 3.14159265f / 180.f;
  int32_t spd = PROJ_SPD_BASE + (int32_t)power * PROJ_SPD_GAIN;
  if (pipe) spd = PROJ_PIPE_BASE + (int32_t)power * PROJ_PIPE_GAIN;
  int32_t c = (int32_t)(cosf(rad) * 256.f);
  int32_t s = (int32_t)(sinf(rad) * 256.f);
  Proj* pr = allocProj();
  if (!pr) return;
  pr->active = true;
  pr->type = grenade ? PRJ_GRENADE : (sentry ? PRJ_POD : (pipe ? PRJ_PIPE : PRJ_BAZOOKA));
  pr->owner = pid;
  pr->vx = (c * spd) >> 8;
  pr->vy = -((s * spd) >> 8);
  pr->fuseMs = grenade ? GRENADE_FUSE_MS : (sentry ? SENTRY_FUSE_MS : 0);
  pr->grace = 8;
  pr->homing = 0xFF;
  if (pl.weapon == WPN_HOMING) {
    pr->type = PRJ_HOME;
    pr->homing = g.homeTarget;
    pr->fuseMs = HOMING_LOCK_MS;
    if (pl.ammoH) spendAmmo(pl.ammoH);
    g.homeLocked = false;
  }
  int32_t ox = pl.x << 8;
  int32_t oy = (pl.y - 5) << 8;
  pr->x = ox;
  pr->y = oy;
  int muzzleX = pl.x;
  int muzzleY = pl.y - 5;
  int blastR = grenade || sentry ? GRENADE_R : BAZOOKA_R;
  if (pl.weapon == WPN_HOMING) blastR = BAZOOKA_R;
  if (pipe) blastR = PIPE_R;
  if (muzzleY >= 0 && terrainSolid(g.terrain, muzzleX, muzzleY)) {
    afterShot();
    g.recoilFrames = 5;
    noteFireKick(pid);
    boomProj(*pr, muzzleX, muzzleY, blastR);
    return;
  }
  for (int d = 2; d <= 10; d++) {
    int32_t nx = ox + c * d;
    int32_t ny = oy - s * d;
    int px = nx >> 8;
    int py = ny >> 8;
    if (px < 1 || px > WORLD_W - 2 || py >= g.terrain.waterY) break;
    if (py >= 0 && terrainSolid(g.terrain, px, py)) break;
    pr->x = nx;
    pr->y = ny;
  }
  afterShot();
  g.recoilFrames = 7;
  noteFireKick(pid);
  if (pipe) noteThrowAnim(pid);
  if (pl.weapon == WPN_BAZOOKA || pl.weapon == WPN_HOMING) {
    int mx = pr->x >> 8;
    int my = pr->y >> 8;
    spawnMuzzle(mx, my);
  }
}

static bool wormFits(const Terrain& t, int x, int y) {
  if (y < wormMinY()) return false;
  for (int k = 0; k <= 5; k++) {
    if (terrainSolid(t, x, y - k)) return false;
  }
  return true;
}

static bool tryMove(Player& pl, int dir) {
  if (!pl.alive) return false;
  if (inWater(pl)) {
    hitWater(pl);
    if (!pl.dying) killPlayer((uint8_t)(&pl - g.p), "DROWNED");
    return false;
  }
  int nx = pl.x + dir;
  if (nx < 3 || nx > WORLD_W - 4) return false;
  bool airborne = !terrainGrounded(g.terrain, pl.x, pl.y);
  if (airborne) {
    if (wormFits(g.terrain, nx, pl.y)) {
      pl.x = (int16_t)nx;
      pl.facing = (int8_t)dir;
      return true;
    }
    for (int up = 1; up <= 2; up++) {
      int ny = pl.y - up;
      if (!wormFits(g.terrain, nx, ny)) continue;
      pl.x = (int16_t)nx;
      pl.y = (int16_t)ny;
      pl.facing = (int8_t)dir;
      return true;
    }
    return false;
  }
  for (int up = 0; up <= WALK_CLIMB; up++) {
    int ny = pl.y - up;
    if (!wormFits(g.terrain, nx, ny)) continue;
    if (!terrainSolid(g.terrain, nx, ny + 1) && up > 0) continue;
    if (up == 0 || terrainSolid(g.terrain, nx, ny + 1)) {
      pl.x = (int16_t)nx;
      pl.y = (int16_t)ny;
      pl.facing = (int8_t)dir;
      return true;
    }
  }
  for (int down = 1; down <= WALK_DROP; down++) {
    int ny = pl.y + down;
    if (ny >= g.terrain.waterY - 1) break;
    if (!wormFits(g.terrain, nx, ny)) continue;
    if (!terrainSolid(g.terrain, nx, ny + 1)) continue;
    pl.x = (int16_t)nx;
    pl.y = (int16_t)ny;
    pl.facing = (int8_t)dir;
    return true;
  }
  if (wormFits(g.terrain, nx, pl.y)) {
    pl.x = (int16_t)nx;
    pl.facing = (int8_t)dir;
    return true;
  }
  return false;
}

static void boomProj(Proj& pr, int x, int y, int r) {
  if (g.isHost || g.practice) {
    int minDmg = (pr.type == PRJ_PIPE) ? DMG_PIPE_MIN : 0;
    if (pr.type == PRJ_PIPE) r = PIPE_R;
    doExplosion(x, y, r, explMaxDmg(pr.type), -1, minDmg);
  }
  pr.active = false;
}

static void stickPipe(Proj& pr, uint8_t onto) {
  if (pr.type != PRJ_PIPE || pr.homing != 0xFF) return;
  pr.vx = 0;
  pr.vy = 0;
  pr.homing = onto;
  pr.fuseMs = PIPE_STICK_MS;
}

static void tickStuckPipe(Proj& pr, uint16_t dt) {
  if (pr.homing < g.nPlayers) {
    Player& pl = g.p[pr.homing];
    if (pl.alive) {
      pr.x = pl.x << 8;
      pr.y = (pl.y - 4) << 8;
    } else {
      pr.homing = 0xFE;
    }
  }
  if (pr.homing == 0xFE || pr.homing == 0xFF) {
    int px = pr.x >> 8;
    int py = pr.y >> 8;
    bool stuck = terrainSolid(g.terrain, px, py) ||
                 terrainSolid(g.terrain, px, py + 1) ||
                 terrainSolid(g.terrain, px, py - 1) ||
                 terrainSolid(g.terrain, px - 1, py) ||
                 terrainSolid(g.terrain, px + 1, py);
    if (!stuck) {
      pr.homing = 0xFF;
      pr.vy += PROJ_GRAV_PIPE;
      pr.y += pr.vy;
      px = pr.x >> 8;
      py = pr.y >> 8;
      if (px < 0 || px >= WORLD_W || py >= g.terrain.waterY) {
        boomProj(pr, px, py >= g.terrain.waterY ? g.terrain.waterY - 1 : py, PIPE_R);
        return;
      }
      if (py >= 0 && terrainSolid(g.terrain, px, py)) {
        stickPipe(pr, 0xFE);
      }
      if (pr.fuseMs > dt) pr.fuseMs = (uint16_t)(pr.fuseMs - dt);
      else boomProj(pr, pr.x >> 8, pr.y >> 8, PIPE_R);
      return;
    }
  }
  pr.vx = 0;
  pr.vy = 0;
  if (pr.fuseMs > dt) pr.fuseMs = (uint16_t)(pr.fuseMs - dt);
  else boomProj(pr, pr.x >> 8, pr.y >> 8, PIPE_R);
}

static void boomNeedle(Proj& pr, int x, int y, uint8_t hitId, bool hitWorm, int dmg) {
  if (g.isHost || g.practice) {
    x = wm_clamp((int16_t)x, 0, WORLD_W - 1);
    y = wm_clamp((int16_t)y, 0, WORLD_H - 1);
    terrainCrater(g.terrain, x, y, DRACO_R);
    g.terrain.waterY = g.waterY;
    g.explodeX = (uint8_t)x;
    g.explodeY = (uint8_t)y;
    g.explodeR = DRACO_R;
    g.explodeUntil = millis() + EXPLODE_ANIM_MS / 2;
    spawnParticles(x, y, 2);
    if (g.isHost) sendExplosion(x, y, DRACO_R);
    if (hitWorm) applyDamage(hitId, dmg, pr.vx > 0 ? 1 : -1, -1);
    hurtSentries(x, y, 2, dmg, pr.owner);
  }
  pr.active = false;
}

static void boomDraco(Proj& pr, int x, int y, uint8_t hitId, bool hitWorm) {
  boomNeedle(pr, x, y, hitId, hitWorm, DMG_DRACO);
}

static void dropAirstrikeBomb(uint8_t owner, int x, int y) {
  Proj* b = allocProj();
  if (!b) return;
  x = wm_clamp((int16_t)x, 2, WORLD_W - 3);
  b->active = true;
  b->type = PRJ_BOMB;
  b->owner = owner;
  b->x = x << 8;
  b->y = y << 8;
  b->vx = (int32_t)g.wind * 2;
  b->vy = 48;
  b->fuseMs = 0;
  b->grace = 2;
  b->homing = 0xFF;
}

static void stepPlane(Proj& pr, bool hostDrop) {
  pr.x += pr.vx;
  pr.y += pr.vy;
  int px = pr.x >> 8;
  int py = pr.y >> 8;
  auto dropNext = [&]() {
    int idx = AIRSTRIKE_N - (int)pr.homing;
    int want = (int)pr.fuseMs + (idx - AIRSTRIKE_N / 2) * 8;
    dropAirstrikeBomb(pr.owner, want, py + 4);
    pr.homing--;
  };
  if (px > WORLD_W + 6) {
    if (hostDrop) {
      while (pr.homing) dropNext();
    }
    pr.active = false;
    return;
  }
  if (!hostDrop) return;
  while (pr.homing) {
    int idx = AIRSTRIKE_N - (int)pr.homing;
    int want = (int)pr.fuseMs + (idx - AIRSTRIKE_N / 2) * 8;
    if (px < want) break;
    dropNext();
  }
  if (pr.homing == 0) pr.active = false;
}

static void stepOneProj(Proj& pr, uint16_t dt, bool collide = true) {
  if (!pr.active) return;
  if (pr.type == PRJ_PLANE) {
    stepPlane(pr, collide);
    return;
  }
  if (!collide) {
    int windK = 0;
    int grav = PROJ_GRAV_BOMB;
    if (pr.type == PRJ_BAZOOKA) { windK = PROJ_WIND_BAZOOKA; grav = PROJ_GRAV_BAZOOKA; }
    else if (pr.type == PRJ_GRENADE || pr.type == PRJ_POD) { windK = 0; grav = PROJ_GRAV_GRENADE; }
    else if (pr.type == PRJ_PIPE) { windK = 0; grav = PROJ_GRAV_PIPE; }
    else if (pr.type == PRJ_HOME) { windK = PROJ_WIND_HOME; grav = PROJ_GRAV_HOME; }
    else if (pr.type == PRJ_DRACO || pr.type == PRJ_SENTRY) { windK = PROJ_WIND_NEEDLE; grav = PROJ_GRAV_NEEDLE; }
    else { windK = PROJ_WIND_BOMB; grav = PROJ_GRAV_BOMB; }
    if (pr.type == PRJ_PIPE && (pr.homing != 0xFF || (pr.vx == 0 && pr.vy == 0 && pr.fuseMs))) {
      if (pr.homing == 0xFF) pr.homing = 0xFE;
      tickStuckPipe(pr, dt);
      return;
    }
    if (pr.fuseMs > dt) pr.fuseMs = (uint16_t)(pr.fuseMs - dt);
    else {
      pr.fuseMs = 0;
      if (pr.type == PRJ_POD) {
        pr.active = false;
        return;
      }
    }
    int32_t vx = pr.vx + (int32_t)g.wind * windK;
    int32_t gravNow = grav;
    if (g.mapKind != MAP_CAVERN && (pr.y >> 8) < 0) gravNow += PROJ_GRAV_SKY;
    int32_t vy = pr.vy + gravNow;
    pr.x += vx;
    pr.y += vy;
    pr.vx = vx;
    pr.vy = vy;
    int px = pr.x >> 8;
    int py = pr.y >> 8;
    if (px < 0 || px >= WORLD_W || py >= g.terrain.waterY) {
      pr.active = false;
      return;
    }
    bool bounce = (pr.type == PRJ_GRENADE || pr.type == PRJ_POD);
    if (pr.type == PRJ_PIPE) {
      if (py >= 0 && terrainSolid(g.terrain, px, py)) stickPipe(pr, 0xFE);
    } else if (!bounce && py >= 0 && terrainSolid(g.terrain, px, py)) {
      pr.active = false;
    }
    if (pr.grace) pr.grace--;
    return;
  }
  int windK = 0;
  int grav = PROJ_GRAV_BOMB;
  int rExpl = BAZOOKA_R;
  if (pr.type == PRJ_BAZOOKA) { windK = PROJ_WIND_BAZOOKA; grav = PROJ_GRAV_BAZOOKA; rExpl = BAZOOKA_R; }
  else if (pr.type == PRJ_GRENADE || pr.type == PRJ_POD) { windK = 0; grav = PROJ_GRAV_GRENADE; rExpl = GRENADE_R; }
  else if (pr.type == PRJ_PIPE) { windK = 0; grav = PROJ_GRAV_PIPE; rExpl = PIPE_R; }
  else if (pr.type == PRJ_HOME) { windK = PROJ_WIND_HOME; grav = PROJ_GRAV_HOME; rExpl = BAZOOKA_R; }
  else if (pr.type == PRJ_DRACO || pr.type == PRJ_SENTRY) { windK = PROJ_WIND_NEEDLE; grav = PROJ_GRAV_NEEDLE; rExpl = DRACO_R; }
  else { windK = PROJ_WIND_BOMB; grav = PROJ_GRAV_BOMB; rExpl = AIRSTRIKE_R; }

  bool needle = (pr.type == PRJ_DRACO || pr.type == PRJ_SENTRY);
  int needleDmg = (pr.type == PRJ_SENTRY) ? DMG_SENTRY : DMG_DRACO;

  if (pr.type == PRJ_PIPE && (pr.homing != 0xFF || (pr.vx == 0 && pr.vy == 0 && pr.fuseMs))) {
    if (pr.homing == 0xFF) pr.homing = 0xFE;
    tickStuckPipe(pr, dt);
    return;
  }

  if (pr.type == PRJ_GRENADE || pr.type == PRJ_POD) {
    if (pr.fuseMs > dt) pr.fuseMs = (uint16_t)(pr.fuseMs - dt);
    else {
      if (pr.type == PRJ_POD) {
        if (g.isHost || g.practice) deploySentry(pr.owner, pr.x >> 8, pr.y >> 8);
        pr.active = false;
      } else boomProj(pr, pr.x >> 8, pr.y >> 8, rExpl);
      return;
    }
  }
  if (pr.type == PRJ_HOME) {
    if (pr.fuseMs > dt) pr.fuseMs = (uint16_t)(pr.fuseMs - dt);
    else pr.fuseMs = 0;
  }

  int n = PHYS_SUBSTEP;
  int32_t ax = (int32_t)g.wind * windK;
  int32_t ay = grav;
  if (g.mapKind != MAP_CAVERN && (pr.y >> 8) < 0) ay += PROJ_GRAV_SKY;
  int32_t vx = pr.vx + ax;
  int32_t vy = pr.vy + ay;
  if (pr.type == PRJ_HOME && pr.fuseMs == 0 && pr.homing < g.nPlayers && g.p[pr.homing].alive) {
    int32_t tx = g.p[pr.homing].x << 8;
    int32_t ty = (g.p[pr.homing].y - 3) << 8;
    vx += (tx - pr.x) / 10;
    vy += (ty - pr.y) / 10;
    int32_t cap = PROJ_HOME_CAP;
    if (vx > cap) vx = cap;
    if (vx < -cap) vx = -cap;
    if (vy > cap) vy = cap;
    if (vy < -cap) vy = -cap;
  }
  // extra microsteps if moving fast
  int32_t adx = vx < 0 ? -vx : vx;
  int32_t ady = vy < 0 ? -vy : vy;
  int need = (int)(((adx > ady ? adx : ady) >> 8) + 1);
  if (need > n) n = need;
  if (n > 12) n = 12;
  int32_t sx = vx / n;
  int32_t sy = vy / n;
  for (int i = 0; i < n; i++) {
    int32_t nx = pr.x + sx;
    int32_t ny = pr.y + sy;
    int px = nx >> 8;
    int py = ny >> 8;
    if (px < 0 || px >= WORLD_W) {
      pr.active = false;
      return;
    }
    if (py < 0) {
      if (g.mapKind == MAP_CAVERN) {
        if (needle) boomNeedle(pr, pr.x >> 8, 0, 0, false, needleDmg);
        else if (pr.type == PRJ_POD) {
          if (g.isHost || g.practice) deploySentry(pr.owner, pr.x >> 8, 0);
          pr.active = false;
        } else if (pr.type == PRJ_PIPE) {
          stickPipe(pr, 0xFE);
          pr.y = 0;
        } else boomProj(pr, pr.x >> 8, 0, rExpl);
        return;
      }
      pr.x = nx;
      pr.y = ny;
      if (pr.grace) pr.grace--;
      continue;
    }
    if (py >= g.terrain.waterY) {
      if (needle) boomNeedle(pr, px, g.terrain.waterY - 1, 0, false, needleDmg);
      else if (pr.type == PRJ_POD) {
        if (g.isHost || g.practice) deploySentry(pr.owner, px, g.terrain.waterY - 2);
        pr.active = false;
      } else if (pr.type == PRJ_PIPE) {
        stickPipe(pr, 0xFE);
        pr.y = (int32_t)(g.terrain.waterY - 2) << 8;
      } else boomProj(pr, px, g.terrain.waterY - 1, rExpl);
      return;
    }
    uint8_t hitId;
    if ((g.isHost || g.practice) && hitPlayer(px, py, pr.owner, pr.grace, &hitId)) {
      if (needle) boomNeedle(pr, px, py, hitId, true, needleDmg);
      else if (pr.type == PRJ_POD) {
        deploySentry(pr.owner, px, py);
        pr.active = false;
      } else if (pr.type == PRJ_PIPE) {
        stickPipe(pr, hitId);
      } else boomProj(pr, px, py, rExpl);
      return;
    }
    if (terrainSolid(g.terrain, px, py)) {
      if (pr.type == PRJ_GRENADE || pr.type == PRJ_POD) {
        bool hitX = terrainSolid(g.terrain, nx >> 8, pr.y >> 8);
        bool hitY = terrainSolid(g.terrain, pr.x >> 8, ny >> 8);
        int bounceN = (pr.type == PRJ_POD) ? 2 : 5;
        if (hitX) vx = -(vx * bounceN) / 8;
        if (hitY) vy = -(vy * bounceN) / 8;
        if (!hitX && !hitY) { vx = -vx / 2; vy = -vy / 2; }
        if (pr.type == PRJ_POD) {
          vx = (vx * 3) / 5;
          vy = (vy * 3) / 5;
        }
        pr.vx = vx; pr.vy = vy;
        int rest = (pr.type == PRJ_POD) ? (PROJ_BOUNCE_REST * 2) : PROJ_BOUNCE_REST;
        if ((vx < rest && vx > -rest) && (vy < rest && vy > -rest)) {
          int rx = pr.x >> 8, ry = pr.y >> 8;
          if (terrainSolid(g.terrain, rx, ry) || terrainSolid(g.terrain, rx, ry + 1)) {
            pr.vx = 0;
            pr.vy = 0;
          }
        }
        return;
      }
      if (pr.type == PRJ_PIPE) {
        stickPipe(pr, 0xFE);
        return;
      }
      if (needle) boomNeedle(pr, pr.x >> 8, pr.y >> 8, 0, false, needleDmg);
      else boomProj(pr, pr.x >> 8, pr.y >> 8, rExpl);
      return;
    }
    pr.x = nx;
    pr.y = ny;
    if (pr.grace) pr.grace--;
  }
  pr.vx = vx;
  pr.vy = vy;
}

static bool anyFlyingOwned(uint8_t pid) {
  for (int i = 0; i < MAX_PROJ; i++) {
    const Proj& pr = g.proj[i];
    if (!pr.active || pr.owner != pid) continue;
    if (pr.type == PRJ_SENTRY) continue;
    if (pr.type == PRJ_PIPE && pr.vx == 0 && pr.vy == 0) continue;
    return true;
  }
  return false;
}

static bool anyProj() {
  for (int i = 0; i < MAX_PROJ; i++) if (g.proj[i].active) return true;
  return false;
}

static void resolveFalling() {
  for (int i = 0; i < g.nPlayers; i++) {
    Player& pl = g.p[i];
    if (!pl.alive) continue;
    if (pl.knockFrames) {
      int nx = pl.x + pl.knockDir;
      if (nx > 3 && nx < WORLD_W - 4 && !terrainSolid(g.terrain, nx, pl.y) &&
          !terrainSolid(g.terrain, nx, pl.y - 1))
        pl.x = (int16_t)nx;
      pl.knockFrames--;
    }
    if (!terrainGrounded(g.terrain, pl.x, pl.y)) {
      pl.y++;
      if (pl.y >= g.terrain.waterY) killPlayer((uint8_t)i, "DROWNED");
    }
  }
}

static bool allGrounded() {
  for (int i = 0; i < g.nPlayers; i++) {
    if (g.p[i].alive && !terrainGrounded(g.terrain, g.p[i].x, g.p[i].y) &&
        g.p[i].y < g.terrain.waterY)
      return false;
  }
  return true;
}

static bool matchMsgsPending(uint32_t now) {
  if (sPendMsgAt) return true;
  if (g.msg.until > now && !g.msg.subtle) return true;
  return false;
}

static void checkWin() {
  if (anyDying()) return;
  if (matchMsgsPending(millis())) return;
  int w = winnerId();
  if (livingCount() <= 1) {
    g.winner = (w < 0) ? PLAYER_NONE : (uint8_t)w;
    g.mode = MODE_GAMEOVER;
    g.phaseAt = millis();
    if (g.isHost) sendEvent(EVT_WIN, g.winner);
  }
}

static void nextTurn() {
  if (deathBusy()) return;
  checkWin();
  if (g.mode != MODE_PLAY) return;
  int start = g.turn;
  int next = g.turn;
  for (int k = 0; k < MAX_PLAYERS; k++) {
    next = (next + 1) % g.nPlayers;
    if (g.p[next].alive && !g.p[next].dying && g.p[next].connected) break;
  }
  if (!(g.p[next].alive && !g.p[next].dying && g.p[next].connected)) {
    checkWin();
    return;
  }
  g.turnsThisRound++;
  bool sdBanner = false;
  if (g.turnsThisRound >= livingCount()) {
    g.turnsThisRound = 0;
    g.roundN++;
    if (g.roundN >= SUDDEN_AFTER_ROUNDS) {
      if (!g.sudden) {
        g.sudden = true;
        sdBanner = true;
        setMsg("SUDDEN", "DEATH", 1000);
        if (g.isHost) sendEvent(EVT_SUDDEN, 0);
      }
      if (g.waterY > WATER_MAX_Y) {
        g.waterY = (uint8_t)(g.waterY - WATER_RISE);
        g.terrain.waterY = g.waterY;
      }
    }
  }
  g.turn = (uint8_t)next;
  (void)start;
  g.deathHold = false;
  g.phase = sdBanner ? PHASE_SD_BANNER : PHASE_TURN_START;
  g.phaseAt = millis();
  if (!sdBanner) beginTurn();
}

static void resetHeldMovers() {
  memset(aimAccP, 0, sizeof(aimAccP));
  memset(moveAccP, 0, sizeof(moveAccP));
  memset(sCurAccP, 0, sizeof(sCurAccP));
  memset(sAimWasU, 0, sizeof(sAimWasU));
  memset(sAimWasD, 0, sizeof(sAimWasD));
  memset(sMvWasL, 0, sizeof(sMvWasL));
  memset(sMvWasR, 0, sizeof(sMvWasR));
  memset(sMvPending, 0, sizeof(sMvPending));
  memset(sCurWasL, 0, sizeof(sCurWasL));
  memset(sCurWasR, 0, sizeof(sCurWasR));
  memset(sBusyAcc, 0, sizeof(sBusyAcc));
  memset(sTorchAccX, 0, sizeof(sTorchAccX));
  memset(sTorchAccY, 0, sizeof(sTorchAccY));
  memset(sJetFuelAcc, 0, sizeof(sJetFuelAcc));
  memset(sAirHang, 0, sizeof(sAirHang));
  memset(sVelXFric, 0, sizeof(sVelXFric));
}

// Elevation 0=straight down, 90=horizontal, 180=straight up. Facing picks the side.
static int16_t elevOf(const Player& pl) {
  int16_t e;
  if (pl.facing >= 0) {
    if (pl.aim >= 180) e = (int16_t)(pl.aim - 270);
    else e = (int16_t)(pl.aim + 90);
  } else {
    e = (int16_t)(270 - pl.aim);
  }
  return wm_clamp(e, 0, 180);
}

static void applyAimFromElev(Player& pl, int16_t e) {
  e = wm_clamp(e, 0, 180);
  if (pl.facing >= 0) pl.aim = wrap360(e - 90);
  else pl.aim = wrap360(270 - e);
}

static void setFacing(Player& pl, int dir) {
  int8_t f = (dir >= 0) ? 1 : -1;
  if (pl.facing == f) return;
  int16_t e = elevOf(pl);
  pl.facing = f;
  applyAimFromElev(pl, e);
}

static void applyRemoteLook(Player& pl, int16_t aim, uint8_t act) {
  pl.aim = wrap360(aim);
  pl.facing = (act & 16) ? -1 : 1;
}

static void applyAimSky(Player& pl, bool up, bool dn, uint32_t dt) {
  int id = pidOf(pl);
  uint32_t now = millis();
  if (up && dn) return;
  if (sJumpEatAim[id]) {
    if (!up) sJumpEatAim[id] = false;
    if (!up && !dn) {
      aimAccP[id] = 0;
      sAimWasU[id] = sAimWasD[id] = false;
      sAimHoldOn[id] = false;
    }
    if (up && !dn) return;
  }
  if (!up && sUpTapAt[id] && (now - sUpTapAt[id]) >= JUMP_DOUBLE_MS) {
    applyAimFromElev(pl, (int16_t)(elevOf(pl) + 5));
    sUpTapAt[id] = 0;
  }
  if (!up && !dn) {
    aimAccP[id] = 0;
    sAimWasU[id] = sAimWasD[id] = false;
    sAimHoldOn[id] = false;
    return;
  }
  if (up && !sAimWasU[id] && !dn) {
    bool grounded = terrainGrounded(g.terrain, pl.x, pl.y);
    bool airborne = !grounded && now >= sCoyoteUntil[id];
    if (airborne || (sUpTapAt[id] && (now - sUpTapAt[id]) < JUMP_DOUBLE_MS)) {
      if (tryJump(pl)) sJumpEatAim[id] = true;
      sUpTapAt[id] = 0;
      sAimHoldOn[id] = false;
      sAimWasU[id] = true;
      aimAccP[id] = 0;
      return;
    }
    sUpTapAt[id] = now;
    sAimWasU[id] = true;
    sAimHoldOn[id] = false;
    aimAccP[id] = 0;
    return;
  }
  sAimWasU[id] = up;
  if (dn && !up) {
    bool edge = !sAimWasD[id];
    sAimWasD[id] = true;
    sUpTapAt[id] = 0;
    sAimHoldOn[id] = false;
    if (edge) {
      applyAimFromElev(pl, (int16_t)(elevOf(pl) - 5));
      aimAccP[id] = 0;
    }
    aimAccP[id] = (uint16_t)(aimAccP[id] + (uint16_t)dt);
    int steps = 0;
    while (aimAccP[id] >= AIM_MS_PER_DEG && steps < 6) {
      aimAccP[id] = (uint16_t)(aimAccP[id] - AIM_MS_PER_DEG);
      applyAimFromElev(pl, (int16_t)(elevOf(pl) - 1));
      steps++;
    }
    return;
  }
  sAimWasD[id] = false;
  if (up && !sAimHoldOn[id]) {
    if (sUpTapAt[id] && (now - sUpTapAt[id]) < JUMP_DOUBLE_MS) return;
    applyAimFromElev(pl, (int16_t)(elevOf(pl) + 5));
    sAimHoldOn[id] = true;
    aimAccP[id] = 0;
    sUpTapAt[id] = 0;
  }
  if (!sAimHoldOn[id] && up) return;
  aimAccP[id] = (uint16_t)(aimAccP[id] + (uint16_t)dt);
  int steps = 0;
  while (aimAccP[id] >= AIM_MS_PER_DEG && steps < 6) {
    aimAccP[id] = (uint16_t)(aimAccP[id] - AIM_MS_PER_DEG);
    applyAimFromElev(pl, (int16_t)(elevOf(pl) + 1));
    steps++;
  }
}

static void applyMoveHeld(Player& pl, bool left, bool right, uint32_t dt) {
  int id = pidOf(pl);
  if (sDrownHoldUntil[id] && millis() < sDrownHoldUntil[id]) return;
  if (left && right) {
    moveAccP[id] = 0;
    sMvPending[id] = false;
    return;
  }
  if (!left && !right) {
    moveAccP[id] = 0;
    sMvWasL[id] = sMvWasR[id] = sMvPending[id] = false;
    return;
  }
  int dir = left ? -1 : 1;
  setFacing(pl, dir);
  bool airborne = !terrainGrounded(g.terrain, pl.x, pl.y);
  bool jet = livePlay() ? (pl.act == PHASE_JETPACK) : (g.phase == PHASE_JETPACK);
  if (airborne && pl.velY == 0 && !jet) {
    for (int d = 1; d <= WALK_DROP; d++) {
      int ny = pl.y + d;
      if (ny >= g.terrain.waterY - 1) break;
      if (terrainGrounded(g.terrain, pl.x, ny)) {
        pl.y = (int16_t)ny;
        airborne = false;
        break;
      }
    }
  }
  if (airborne) {
    if (jet || pl.jetting || sJetWasOn[id]) {
      pl.velX = 0;
      moveAccP[id] = (uint16_t)(moveAccP[id] + (uint16_t)dt);
      while (moveAccP[id] >= JET_STRAFE_MS_PER_PX) {
        moveAccP[id] = (uint16_t)(moveAccP[id] - JET_STRAFE_MS_PER_PX);
        if (!tryMove(pl, dir)) break;
      }
      return;
    }
    moveAccP[id] = (uint16_t)(moveAccP[id] + (uint16_t)dt);
    while (moveAccP[id] >= AIR_MS_PER_PX) {
      moveAccP[id] = (uint16_t)(moveAccP[id] - AIR_MS_PER_PX);
      if (!tryMove(pl, dir)) break;
    }
    return;
  }
  if ((left && !sMvWasL[id]) || (right && !sMvWasR[id])) sMvPending[id] = true;
  sMvWasL[id] = left;
  sMvWasR[id] = right;
  if (sMvPending[id]) {
    moveAccP[id] = (uint16_t)(moveAccP[id] + (uint16_t)dt);
    if (moveAccP[id] < MOVE_MS_PER_PX) return;
    if (tryMove(pl, dir)) {
      sMvPending[id] = false;
      moveAccP[id] = (uint16_t)(moveAccP[id] - MOVE_MS_PER_PX);
      if (moveAccP[id] >= MOVE_MS_PER_PX) moveAccP[id] = MOVE_MS_PER_PX - 1;
    } else {
      moveAccP[id] = 0;
    }
    return;
  }
  moveAccP[id] = (uint16_t)(moveAccP[id] + (uint16_t)dt);
  if (moveAccP[id] >= MOVE_MS_PER_PX) {
    if (tryMove(pl, dir)) {
      moveAccP[id] = (uint16_t)(moveAccP[id] - MOVE_MS_PER_PX);
      if (moveAccP[id] >= MOVE_MS_PER_PX) moveAccP[id] = MOVE_MS_PER_PX - 1;
    } else {
      sMvPending[id] = true;
      moveAccP[id] = 0;
    }
  }
}

static void killProjsNear(int x, int y, int r) {
  int lim = r + 14;
  int lim2 = lim * lim;
  for (int i = 0; i < MAX_PROJ; i++) {
    if (!g.proj[i].active) continue;
    if (g.proj[i].type == PRJ_PLANE) continue;
    int dx = (g.proj[i].x >> 8) - x;
    int dy = (g.proj[i].y >> 8) - y;
    if (dx * dx + dy * dy <= lim2) g.proj[i].active = false;
  }
}

static void applyCursorHeld(bool left, bool right, uint32_t dt) {
  int id = g.turn < MAX_PLAYERS ? g.turn : 0;
  if (left && right) return;
  if (!left && !right) {
    sCurAccP[id] = 0;
    sCurWasL[id] = sCurWasR[id] = false;
    return;
  }
  int dir = left ? -1 : 1;
  if (g.turn < g.nPlayers && g.p[g.turn].weapon == WPN_HOMING) {
    if ((left && !sCurWasL[id]) || (right && !sCurWasR[id])) stepHomingTarget(dir);
    sCurWasL[id] = left;
    sCurWasR[id] = right;
    sCurAccP[id] = 0;
    return;
  }
  auto nudge = [&]() {
    int nx = (int)g.cursorX + dir;
    if (nx < 2 || nx > WORLD_W - 3) return false;
    g.cursorX = (uint8_t)nx;
    return true;
  };
  if ((left && !sCurWasL[id]) || (right && !sCurWasR[id])) nudge();
  sCurWasL[id] = left;
  sCurWasR[id] = right;
  sCurAccP[id] = (uint16_t)(sCurAccP[id] + (uint16_t)dt);
  while (sCurAccP[id] >= TARGET_MS_PER_PX) {
    sCurAccP[id] = (uint16_t)(sCurAccP[id] - TARGET_MS_PER_PX);
    if (!nudge()) break;
  }
}

static bool tryJump(Player& pl) {
  if (!pl.alive) return false;
  int id = pidOf(pl);
  if (sDrownHoldUntil[id] && millis() < sDrownHoldUntil[id]) return false;
  uint32_t now = millis();
  bool grounded = terrainGrounded(g.terrain, pl.x, pl.y);
  if (grounded) {
    pl.jumpsLeft = JUMP_COUNT;
    sCoyoteUntil[id] = now + COYOTE_MS;
  }
  bool coyote = !grounded && now < sCoyoteUntil[id];
  if (pl.jumpsLeft == 0 && !coyote) return false;
  bool doubleJump = !grounded && pl.jumpsLeft == 1;
  if (pl.jumpsLeft) pl.jumpsLeft--;
  else sCoyoteUntil[id] = 0;
  sJumpKind[id] = doubleJump ? 2 : 1;
  pl.velY = (int8_t)JUMP_VEL;
  if (g.heldButtons & (1 << BTN_LEFT)) {
    pl.facing = -1;
    if (pl.velX > 0) pl.velX = 0;
  } else if (g.heldButtons & (1 << BTN_RIGHT)) {
    pl.facing = 1;
    if (pl.velX < 0) pl.velX = 0;
  }
  return true;
}

static void tickTorch(Player& pl, uint32_t now) {
  int id = pidOf(pl);
  float rad = pl.aim * 3.14159265f / 180.f;
  float ux = cosf(rad);
  float uy = -sinf(rad);
  auto iround = [](float v) { return (int)(v >= 0.f ? v + 0.5f : v - 0.5f); };
  bool dirt = false;
  for (int d = 1; d <= 6; d++) {
    int tx = pl.x + iround(ux * (float)d);
    int ty = (pl.y - 3) + iround(uy * (float)d);
    if (terrainSolid(g.terrain, tx, ty) || terrainSolid(g.terrain, tx, ty - 1) ||
        terrainSolid(g.terrain, tx, ty + 1)) {
      dirt = true;
      break;
    }
  }
  if (!dirt) {
    afterMelee();
    return;
  }
  int ax = iround(ux * 256.f);
  int ay = iround(uy * 256.f);
  if (ax == 0 && ay == 0) ax = pl.facing >= 0 ? 256 : -256;
  sTorchAccX[id] = (int16_t)(sTorchAccX[id] + ax);
  sTorchAccY[id] = (int16_t)(sTorchAccY[id] + ay);
  int sx = 0, sy = 0;
  if (sTorchAccX[id] >= 256) { sx = 1; sTorchAccX[id] = (int16_t)(sTorchAccX[id] - 256); }
  else if (sTorchAccX[id] <= -256) { sx = -1; sTorchAccX[id] = (int16_t)(sTorchAccX[id] + 256); }
  if (sTorchAccY[id] >= 256) { sy = 1; sTorchAccY[id] = (int16_t)(sTorchAccY[id] - 256); }
  else if (sTorchAccY[id] <= -256) { sy = -1; sTorchAccY[id] = (int16_t)(sTorchAccY[id] + 256); }
  int nx = pl.x + sx;
  int ny = pl.y + sy;
  int cx = pl.x + iround(ux * 4.f);
  int cy = (pl.y - 4) + iround(uy * 4.f);
  terrainCrater(g.terrain, cx, cy, TORCH_R);
  terrainCrater(g.terrain, pl.x + iround(ux * 2.f), (pl.y - 2) + iround(uy * 2.f), TORCH_R);
  if (sx || sy) {
    int k0 = (sy > 0) ? -1 : 0;
    for (int k = k0; k <= 5; k++) {
      int ty = ny - k;
      if (ty >= WORLD_TOP) terrainSet(g.terrain, nx, ty, false);
    }
  }
  if (g.isHost) sendExplosion(cx, cy, TORCH_R);
  static uint8_t torchSpark;
  if ((++torchSpark & 1) == 0) spawnSparks(cx, cy, 3);
  pl.velX = 0;
  pl.velY = 0;
  if ((sx || sy) && nx > 3 && nx < WORLD_W - 4 && ny > WORLD_TOP + 5 && ny < g.terrain.waterY - 1) {
    if (wormFits(g.terrain, nx, ny)) {
      pl.x = (int16_t)nx;
      pl.y = (int16_t)ny;
    } else if (sx != 0 && wormFits(g.terrain, nx, pl.y)) {
      pl.x = (int16_t)nx;
    } else if (sy != 0 && wormFits(g.terrain, pl.x, ny)) {
      pl.y = (int16_t)ny;
    }
  }
  for (int i = 0; i < g.nPlayers; i++) {
    if (!g.p[i].alive) continue;
    if (&g.p[i] == &pl) continue;
    if (g.molliHit & (1 << i)) continue;
    int hx = g.p[i].x - cx;
    int hy = (g.p[i].y - 3) - cy;
    if (hx * hx + hy * hy <= 6 * 6) {
      g.molliHit |= (uint8_t)(1 << i);
      int vx, vy;
      knockFrom(cx, cy, g.p[i].x, g.p[i].y - 3, 5, &vx, &vy);
      applyDamage((uint8_t)i, DMG_TORCH, vx, vy);
    }
  }
  if ((now - g.phaseAt) >= TORCH_MS) afterMelee();
}

static void steerTorch(Player& pl, bool left, bool right, uint32_t dt) {
  int id = pidOf(pl);
  if (left && right) return;
  if (!left && !right) {
    moveAccP[id] = 0;
    return;
  }
  int delta = left ? 1 : -1;
  if ((left && !sMvWasL[id]) || (right && !sMvWasR[id])) {
    pl.aim = wrap360((int16_t)(pl.aim + delta * 8));
    moveAccP[id] = 0;
  }
  sMvWasL[id] = left;
  sMvWasR[id] = right;
  moveAccP[id] = (uint16_t)(moveAccP[id] + (uint16_t)dt);
  int steps = 0;
  while (moveAccP[id] >= AIM_MS_PER_DEG && steps < 6) {
    moveAccP[id] = (uint16_t)(moveAccP[id] - AIM_MS_PER_DEG);
    pl.aim = wrap360((int16_t)(pl.aim + delta));
    steps++;
  }
  if (pl.aim > 90 && pl.aim < 270) pl.facing = -1;
  else pl.facing = 1;
}

static bool meleeHit(Player& pl, int dir, int reach, int rad) {
  float radA = pl.aim * 3.14159265f / 180.f;
  bool bat = (g.phase == PHASE_BAT);
  int fx = pl.x + dir * reach;
  int fy = pl.y - 3;
  if (bat) {
    fx = pl.x + (int)(cosf(radA) * (float)reach);
    fy = pl.y - 3 + (int)(-sinf(radA) * (float)reach);
  }
  bool hit = false;
  for (int i = 0; i < g.nPlayers; i++) {
    if (!g.p[i].alive) continue;
    if (&g.p[i] == &pl) continue;
    if (g.molliHit & (1 << i)) continue;
    int dx = g.p[i].x - fx;
    int dy = (g.p[i].y - 3) - fy;
    int bx = g.p[i].x - pl.x;
    int by = g.p[i].y - pl.y;
    int hitR = bat ? rad + 8 : rad;
    bool near = (bx * bx + by * by) <= (bat ? 36 * 36 : 16 * 16) && (by < 0 ? -by : by) <= (bat ? 26 : 12);
    bool punch = (dx * dx + dy * dy) <= hitR * hitR;
    bool inFront = bat ? true : ((dir > 0) ? (g.p[i].x >= pl.x - 6) : (g.p[i].x <= pl.x + 6));
    int abx = bx < 0 ? -bx : bx;
    int aby = by < 0 ? -by : by;
    bool cone = inFront && abx <= reach + (bat ? 16 : 10) && aby <= (bat ? 18 : 12);
    if (bat) {
      int along = bx * (fx - pl.x) + (g.p[i].y - 3 - (pl.y - 3)) * (fy - (pl.y - 3));
      cone = punch || (along >= 0 && abx * abx + aby * aby <= (reach + 22) * (reach + 22));
    }
    if (!near && !punch && !cone) continue;
    g.molliHit |= (uint8_t)(1 << i);
    hit = true;
    if (bat) {
      int vx = (int)(cosf(radA) * 16.f);
      int vy = (int)(-sinf(radA) * 14.f);
      applyDamage((uint8_t)i, DMG_BAT, vx, vy);
      g.p[i].velX = (int8_t)wm_clamp((int16_t)vx, -10, 10);
      g.p[i].velY = (int8_t)wm_clamp((int16_t)vy, -8, 5);
    } else {
      int vx, vy;
      knockFrom(pl.x, pl.y - 3, g.p[i].x, g.p[i].y - 3, 6, &vx, &vy);
      applyDamage((uint8_t)i, DMG_MOLLI, vx, vy);
      spawnBlood(g.p[i].x, g.p[i].y - 3, dir);
    }
  }
  return hit;
}

static void startMolli(Player& pl) {
  if (!spendAmmo(pl.ammoMo)) { pl.weapon = WPN_BAZOOKA; return; }
  g.phase = PHASE_MOLLI;
  g.phaseAt = millis();
  g.molliHit = 0;
  pl.velX = 0;
  if (g.cheat) {
    g.recoilFrames = 8;
    int dir = pl.facing >= 0 ? 1 : -1;
    pl.velY = (int8_t)JUMP_VEL;
    int nx = pl.x + dir * 4;
    if (nx > 3 && nx < WORLD_W - 4 && wormFits(g.terrain, nx, pl.y))
      pl.x = (int16_t)nx;
    meleeHit(pl, dir, 10, 16);
    return;
  }
  pl.velY = 0;
}

static void tickMolli(Player& pl, uint32_t now) {
  int dir = pl.facing >= 0 ? 1 : -1;
  if (g.cheat) {
    int ny = pl.y - 2;
    terrainCrater(g.terrain, pl.x, pl.y - 5, 2);
    terrainCrater(g.terrain, pl.x + dir, pl.y - 6, 2);
    if (ny <= WORLD_TOP + 6) {
      pl.y = (int16_t)(WORLD_TOP + 6);
      meleeHit(pl, dir, 10, 16);
      afterMelee();
      return;
    }
    if (ny > WORLD_TOP + 5) pl.y = (int16_t)ny;
    meleeHit(pl, dir, 12, 18);
    return;
  }
  uint32_t age = now - g.phaseAt;
  if (age < MOLLI_WINDUP_MS) return;
  if (age < MOLLI_WINDUP_MS + 50 && g.recoilFrames < 4) {
    g.recoilFrames = 8;
    int nx = pl.x + dir * 4;
    if (nx > 3 && nx < WORLD_W - 4 && wormFits(g.terrain, nx, pl.y))
      pl.x = (int16_t)nx;
    meleeHit(pl, dir, 10, 16);
  }
  int nx = pl.x + dir * 2;
  if (nx > 3 && nx < WORLD_W - 4 && wormFits(g.terrain, nx, pl.y))
    pl.x = (int16_t)nx;
  meleeHit(pl, dir, 10, 16);
  if (age >= MOLLI_WINDUP_MS + MOLLI_MS) {
    afterMelee();
  }
}

static void startBat(Player& pl) {
  if (!spendAmmo(pl.ammoBat)) { pl.weapon = WPN_BAZOOKA; return; }
  g.phase = PHASE_BAT;
  g.phaseAt = millis();
  g.molliHit = 0;
  g.recoilFrames = 6;
  int dir = pl.facing >= 0 ? 1 : -1;
  int nx = pl.x + dir;
  if (nx > 3 && nx < WORLD_W - 4 && wormFits(g.terrain, nx, pl.y))
    pl.x = (int16_t)nx;
  meleeHit(pl, dir, 28, 26);
}

static void tickBat(Player& pl, uint32_t now) {
  int dir = pl.facing >= 0 ? 1 : -1;
  meleeHit(pl, dir, 28, 26);
  if ((now - g.phaseAt) >= BAT_MS) {
    afterMelee();
  }
}

static void lockHomingFromCursor() {
  int best = -1;
  int bestD = 9999;
  for (int i = 0; i < g.nPlayers; i++) {
    if (!g.p[i].alive || g.p[i].dying || g.p[i].hp == 0) continue;
    if ((uint8_t)i == g.turn && g.nPlayers > 1) continue;
    int dx = g.p[i].x - (int)g.cursorX;
    if (dx < 0) dx = -dx;
    if (dx < bestD) {
      bestD = dx;
      best = i;
    }
  }
  if (best >= 0) {
    g.homeTarget = (uint8_t)best;
    g.cursorX = (uint8_t)wm_clamp(g.p[best].x, 2, WORLD_W - 3);
  }
}

static void stepHomingTarget(int dir) {
  if (dir == 0 || g.nPlayers < 1) return;
  int n = g.nPlayers;
  int cur = (int)g.homeTarget;
  if (cur < 0 || cur >= n) cur = (int)g.turn;
  for (int k = 1; k <= n; k++) {
    int i = (cur + dir * k) % n;
    if (i < 0) i += n;
    if (!g.p[i].alive || g.p[i].dying || g.p[i].hp == 0) continue;
    if (i == (int)g.turn && n > 1) {
      int others = 0;
      for (int j = 0; j < n; j++) {
        if (j == (int)g.turn) continue;
        if (g.p[j].alive && !g.p[j].dying && g.p[j].hp > 0) others++;
      }
      if (others) continue;
    }
    g.homeTarget = (uint8_t)i;
    g.cursorX = (uint8_t)wm_clamp(g.p[i].x, 2, WORLD_W - 3);
    return;
  }
}

static void beginTargeting(Player& pl) {
  g.cursorX = (uint8_t)wm_clamp(pl.x, 2, WORLD_W - 3);
  if (pl.weapon == WPN_HOMING) {
    int best = -1, bd = 9999;
    for (int i = 0; i < g.nPlayers; i++) {
      if (!g.p[i].alive || (uint8_t)i == g.turn) continue;
      int d = g.p[i].x - pl.x;
      if (d < 0) d = -d;
      if (d < bd) {
        bd = d;
        best = i;
      }
    }
    if (best >= 0) {
      g.homeTarget = (uint8_t)best;
      g.cursorX = (uint8_t)wm_clamp(g.p[best].x, 2, WORLD_W - 3);
    } else {
      lockHomingFromCursor();
    }
    g.homeLocked = false;
  }
  g.phase = PHASE_TARGET;
}

static void stopJetpack(Player& pl) {
  int id = pidOf(pl);
  if (id >= 0 && id < MAX_PLAYERS) sJetWasOn[id] = false;
  pl.jetting = false;
  g.phase = PHASE_CONTROL;
  g.phaseAt = millis();
}

static void startJetpack(Player& pl) {
  if (pl.ammoJ == 0 && !g.cheat) { pl.weapon = WPN_BAZOOKA; return; }
  if (g.cheat && pl.ammoJ == 0) pl.ammoJ = AMMO_JET;
  g.phase = PHASE_JETPACK;
  g.phaseAt = millis();
  int id = pidOf(pl);
  sJetWasOn[id] = true;
  sJetFuelAcc[id] = 0;
  sJetMoveAcc[id] = 0;
  sJetHoverUntil[id] = 0;
  pl.velY = 0;
  pl.jetting = true;
  armWpnCd(pl);
}

static bool tryJetVert(Player& pl, int dy) {
  int ny = pl.y + dy;
  if (ny < wormMinY()) return false;
  if (ny + 1 >= g.terrain.waterY) return false;
  if (dy < 0) {
    if (terrainSolid(g.terrain, pl.x, ny - 5) || terrainSolid(g.terrain, pl.x, ny - 4))
      return false;
  }
  if (!wormFits(g.terrain, pl.x, ny)) return false;
  pl.y = (int16_t)ny;
  return true;
}

static void burnJetFuel(Player& pl, uint32_t dt) {
  if (g.cheat) return;
  int id = pidOf(pl);
  if (id < 0 || id >= MAX_PLAYERS) return;
  sJetFuelAcc[id] += dt;
  while (sJetFuelAcc[id] >= JET_FUEL_MS && pl.ammoJ > 0) {
    sJetFuelAcc[id] -= JET_FUEL_MS;
    pl.ammoJ--;
  }
}

static void tickJetHover(Player& pl, uint32_t dt) {
  (void)dt;
  if (!g.cheat && pl.ammoJ == 0) {
    int id = pidOf(pl);
    sJetWasOn[id] = false;
    pl.jetting = false;
    return;
  }
  pl.velY = 0;
}

static void tickJetpack(Player& pl, uint32_t dt) {
  int id = pidOf(pl);
  bool thrust = (g.heldButtons & (1 << BTN_AIM_UP)) != 0;
  bool dive = !thrust && (g.heldButtons & (1 << BTN_AIM_DN)) != 0;
  if (thrust) burnJetFuel(pl, dt);
  bool hasFuel = g.cheat || pl.ammoJ > 0;
  pl.jetting = true;
  sJetWasOn[id] = hasFuel;
  if ((thrust && hasFuel) || dive) {
    pl.velY = 0;
    sJetMoveAcc[id] = (uint16_t)(sJetMoveAcc[id] + (uint16_t)dt);
    while (sJetMoveAcc[id] >= JET_MS_PER_PX) {
      sJetMoveAcc[id] = (uint16_t)(sJetMoveAcc[id] - JET_MS_PER_PX);
      if (!tryJetVert(pl, thrust ? -1 : 1)) break;
    }
  } else {
    sJetMoveAcc[id] = 0;
  }
  applyMoveHeld(pl, (g.heldButtons & (1 << BTN_LEFT)) != 0,
                (g.heldButtons & (1 << BTN_RIGHT)) != 0, dt);
  if (!g.cheat && pl.ammoJ == 0) {
    stopJetpack(pl);
    fallbackWeapon(pl);
  }
}

static bool spawnDracoShot(Player& pl, uint8_t pid) {
  int16_t aim = wrap360(pl.aim + rng.range(-3, 4));
  float rad = aim * 3.14159265f / 180.f;
  int32_t spd = PROJ_DRACO_SPD;
  int32_t c = (int32_t)(cosf(rad) * 256.f);
  int32_t s = (int32_t)(sinf(rad) * 256.f);
  Proj* pr = allocProj();
  if (!pr) return false;
  pr->active = true;
  pr->type = PRJ_DRACO;
  pr->owner = pid;
  pr->vx = (c * spd) >> 8;
  pr->vy = -((s * spd) >> 8);
  pr->fuseMs = 0;
  pr->grace = 8;
  pr->homing = 0xFF;
  int32_t sx = pl.x << 8;
  int32_t sy = (pl.y - 5) << 8;
  for (int d = 8; d >= 3; d--) {
    int32_t nx = sx + c * d;
    int32_t ny = sy - s * d;
    int px = nx >> 8;
    int py = ny >> 8;
    if (px < 1 || px > WORLD_W - 2 || py < 1 || py >= g.terrain.waterY) continue;
    if (terrainSolid(g.terrain, px, py)) continue;
    sx = nx;
    sy = ny;
    break;
  }
  pr->x = sx;
  pr->y = sy;
  pl.aim = wrap360(pl.aim + (pl.facing >= 0 ? -1 : 1));
  g.recoilFrames = 2;
  noteFireKick(pid);
  spawnSparks(sx >> 8, sy >> 8, 2);
  return true;
}

static void startDraco(Player& pl) {
  if (!spendAmmo(pl.ammoD)) { pl.weapon = WPN_BAZOOKA; return; }
  sDracoLeft = DRACO_N;
  sDracoAcc = 0;
  g.phase = PHASE_DRACO;
  g.phaseAt = millis();
  if (spawnDracoShot(pl, g.turn)) sDracoLeft--;
}

static void tickDraco(Player& pl, uint32_t dt) {
  sDracoAcc += dt;
  while (sDracoLeft && sDracoAcc >= DRACO_SHOT_MS) {
    if (!spawnDracoShot(pl, g.turn)) break;
    sDracoAcc -= DRACO_SHOT_MS;
    sDracoLeft--;
  }
  if (sDracoLeft == 0 && !anyFlyingOwned((uint8_t)pidOf(pl))) afterShot();
}

static void placeMine(Player& pl) {
  if (!hasAmmo(pl, WPN_MINE)) { pl.weapon = WPN_BAZOOKA; return; }
  int slot = -1;
  for (int i = 0; i < MAX_MINES; i++) if (!g.mines[i].on) { slot = i; break; }
  if (slot < 0) return;
  spendAmmo(pl.ammoM);
  Mine& mn = g.mines[slot];
  mn.on = true;
  mn.kind = 0;
  mn.owner = (uint8_t)pidOf(pl);
  mn.x = (uint8_t)wm_clamp(pl.x, 0, WORLD_W - 1);
  mn.y = (uint8_t)wm_clamp(pl.y, 0, WORLD_H - 1);
  mn.armAt = millis() + MINE_ARM_MS;
  notePlaceAnim((uint8_t)pidOf(pl), 0);
  setMsg("MINE", "3s ARM", 900);
  fallbackWeapon(pl);
}

static void placeDynamite(Player& pl) {
  if (!hasAmmo(pl, WPN_DYNAMITE)) { pl.weapon = WPN_BAZOOKA; return; }
  int slot = -1;
  for (int i = 0; i < MAX_MINES; i++) if (!g.mines[i].on) { slot = i; break; }
  if (slot < 0) return;
  spendAmmo(pl.ammoDyn);
  uint32_t now = millis();
  Mine& mn = g.mines[slot];
  mn.on = true;
  mn.kind = 1;
  mn.owner = (uint8_t)pidOf(pl);
  mn.x = (uint8_t)wm_clamp(pl.x, 0, WORLD_W - 1);
  mn.y = (uint8_t)wm_clamp(pl.y, 0, WORLD_H - 1);
  mn.armAt = now + DYN_FUSE_MS;
  sFireLockUntil[pidOf(pl)] = now + DYN_FUSE_MS;
  notePlaceAnim((uint8_t)pidOf(pl), 1);
  if (!livePlay()) {
    g.turnEndsAt = now + DYN_FUSE_MS;
    g.turnLeft = DYN_FUSE_MS;
  }
  setMsg("DYNAMITE", "5s FUSE", 900);
  fallbackWeapon(pl);
}

static void tickMines(uint32_t now) {
  for (int i = 0; i < MAX_MINES; i++) {
    Mine& mn = g.mines[i];
    if (!mn.on) continue;
    int mx = mn.x, my = mn.y;
    if (!terrainSolid(g.terrain, mx, my) && !terrainSolid(g.terrain, mx, my + 1)) {
      int ny = my + 1;
      if (ny >= g.terrain.waterY) {
        mn.on = false;
        mn.armAt = 0;
        if (g.isHost || g.practice) doExplosion(mx, my, mn.kind == 1 ? DYN_R : 6,
                                                mn.kind == 1 ? DMG_DYN : DMG_MINE, -1);
        continue;
      }
      mn.y = (uint8_t)ny;
    }
    if (mn.kind == 1) {
      if (mn.armAt && now < mn.armAt) continue;
      int x = mn.x, y = mn.y;
      mn.on = false;
      mn.armAt = 0;
      if (g.isHost || g.practice) doExplosion(x, y, DYN_R, DMG_DYN, -1);
      else {
        spawnExplosionBurst(x, y, DYN_R);
        g.explodeX = (uint8_t)x;
        g.explodeY = (uint8_t)y;
        g.explodeR = DYN_R;
        g.explodeUntil = now + EXPLODE_ANIM_MS;
        terrainCrater(g.terrain, x, y, DYN_R);
        g.terrain.waterY = g.waterY;
      }
      continue;
    }
    if (now < mn.armAt) continue;
    for (int p = 0; p < g.nPlayers; p++) {
      if (!g.p[p].alive) continue;
      int dx = g.p[p].x - (int)mn.x;
      int dy = g.p[p].y - (int)mn.y;
      if (dx * dx + dy * dy <= 6 * 6) {
        int x = mn.x, y = mn.y;
        mn.on = false;
        if (g.isHost || g.practice) doExplosion(x, y, 6, DMG_MINE, -1);
        break;
      }
    }
  }
}

static void destroySentry(Sentry& st) {
  int x = st.x, y = st.y;
  st.on = false;
  spawnParticles(x, y, 6);
  g.explodeX = (uint8_t)x;
  g.explodeY = (uint8_t)y;
  g.explodeR = 2;
  g.explodeUntil = millis() + EXPLODE_ANIM_MS / 2;
}

static void hurtSentries(int cx, int cy, int r, int maxDmg, uint8_t skipOwner) {
  if (maxDmg < 1) maxDmg = 1;
  int reach = r + 8;
  for (int i = 0; i < MAX_SENTRY; i++) {
    Sentry& st = g.sentries[i];
    if (!st.on) continue;
    int dx = (int)st.x - cx;
    int dy = (int)st.y - 3 - cy;
    int d2 = dx * dx + dy * dy;
    if (skipOwner != 0xFF && st.owner == skipOwner && d2 <= 12 * 12) continue;
    if (d2 > reach * reach) continue;
    int dist = d2 > 0 ? (int)sqrtf((float)d2) : 0;
    int dmg = dist >= reach ? 0 : maxDmg * (reach - dist) / reach;
    if (dmg < 1) dmg = 1;
    if (st.hp > dmg) st.hp = (uint8_t)(st.hp - dmg);
    else destroySentry(st);
  }
}

static void deploySentry(uint8_t owner, int x, int y) {
  x = wm_clamp((int16_t)x, 2, WORLD_W - 3);
  int stand = terrainStandY(g.terrain, x);
  if (stand >= 0) y = stand;
  else y = wm_clamp((int16_t)y, WORLD_TOP + 6, g.terrain.waterY - 3);
  int slot = -1;
  for (int i = 0; i < MAX_SENTRY; i++) {
    if (!g.sentries[i].on) { slot = i; break; }
  }
  if (slot < 0) {
    uint32_t oldest = 0xFFFFFFFFu;
    for (int i = 0; i < MAX_SENTRY; i++) {
      if (g.sentries[i].armAt < oldest) {
        oldest = g.sentries[i].armAt;
        slot = i;
      }
    }
  }
  if (slot < 0) return;
  Sentry& st = g.sentries[slot];
  st.on = true;
  st.owner = owner;
  st.x = (uint8_t)x;
  st.y = (uint8_t)y;
  st.hp = SENTRY_HP;
  st.shots = SENTRY_SHOTS;
  st.facing = 1;
  st.aim = 0;
  if (owner < g.nPlayers && g.p[owner].facing < 0) {
    st.facing = -1;
    st.aim = 180;
  }
  st.lock = 0xFF;
  st.turnShots = 0;
  st.armAt = millis() + SENTRY_ARM_MS;
  st.lastShotAt = 0;
  st.turnReadyAt = 0;
  for (int i = 0; i < MAX_PROJ; i++) {
    if (g.proj[i].active && g.proj[i].type == PRJ_POD && g.proj[i].owner == owner)
      g.proj[i].active = false;
  }
  setMsg("SENTRY", "ARMED", 700);
}

static int sentryWrap360(int a) {
  while (a < 0) a += 360;
  while (a >= 360) a -= 360;
  return a;
}

static int sentryDegToT(int d, int curT) {
  d = sentryWrap360(d);
  if (d > 250 && d < 290) return (curT > 160) ? 320 : 0;
  if (d >= 290) return d - 290;
  return d + 70;
}

static int sentryTToDeg(int t) {
  if (t < 0) t = 0;
  if (t > 320) t = 320;
  if (t <= 69) return 290 + t;
  return t - 70;
}

static int sentryStepAim(int cur, int want, int maxStep) {
  int t = sentryDegToT(cur, 160);
  int wt = sentryDegToT(want, t);
  int dt = wt - t;
  if (dt > maxStep) dt = maxStep;
  if (dt < -maxStep) dt = -maxStep;
  return sentryTToDeg(t + dt);
}

static int sentryWantDeg(const Sentry& st, const Player& tgt) {
  int dx = tgt.x - (int)st.x;
  int dy = (tgt.y - 3) - ((int)st.y - 4);
  int want = (int)(atan2f((float)-dy, (float)dx) * 180.f / 3.14159265f);
  return sentryWrap360(want);
}

static bool fireSentryAt(Sentry& st, const Player& tgt) {
  (void)tgt;
  Proj* pr = allocProj();
  if (!pr) return false;
  float rad = (float)st.aim * 3.14159265f / 180.f;
  float c = cosf(rad);
  float s = sinf(rad);
  int32_t spd = PROJ_SENTRY_SPD;
  pr->active = true;
  pr->type = PRJ_SENTRY;
  pr->owner = st.owner;
  pr->vx = (int32_t)(c * (float)spd);
  pr->vy = (int32_t)(-s * (float)spd);
  pr->fuseMs = 0;
  pr->grace = 4;
  pr->homing = 0xFF;
  auto iround = [](float v) { return (int)(v >= 0.f ? v + 0.5f : v - 0.5f); };
  pr->x = ((int32_t)st.x + iround(c * 4.f)) << 8;
  pr->y = ((int32_t)st.y - 4 - iround(s * 4.f)) << 8;
  st.facing = (st.aim > 90 && st.aim < 270) ? -1 : 1;
  st.shots--;
  st.lastShotAt = millis();
  return true;
}

static bool sentryInRange(const Sentry& st, int p) {
  int dx = g.p[p].x - (int)st.x;
  int dy = g.p[p].y - (int)st.y;
  return dx * dx + dy * dy <= SENTRY_RANGE * SENTRY_RANGE;
}

static bool sentryHasLos(const Sentry& st, int p) {
  int x0 = (int)st.x;
  int y0 = (int)st.y - 4;
  int x1 = g.p[p].x;
  int y1 = g.p[p].y - 3;
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  int x = x0, y = y0;
  int steps = 0;
  while (x != x1 || y != y1) {
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x += sx; }
    if (e2 <= dx) { err += dx; y += sy; }
    int ax = x - x1, ay = y - y1;
    if (ax < 0) ax = -ax;
    if (ay < 0) ay = -ay;
    if (ax <= 1 && ay <= 1) break;
    if (terrainSolid(g.terrain, x, y)) return false;
    if (++steps > 64) break;
  }
  return true;
}

static bool sentryCanSee(const Sentry& st, int p) {
  return sentryInRange(st, p) && sentryHasLos(st, p);
}

static bool sentryStillShooting() {
  for (int i = 0; i < MAX_SENTRY; i++) {
    const Sentry& st = g.sentries[i];
    if (!st.on || st.shots == 0 || st.turnShots >= SENTRY_TURN_SHOTS) continue;
    if (st.lock >= g.nPlayers) continue;
    if (!wormActive(st.lock) || st.lock == st.owner) continue;
    if (sentryCanSee(st, st.lock)) return true;
  }
  return false;
}

static void tickSentries(uint32_t now) {
  bool moved[MAX_PLAYERS];
  for (int p = 0; p < g.nPlayers; p++) {
    moved[p] = (g.p[p].x != sSeenX[p] || g.p[p].y != sSeenY[p]);
  }
  for (int i = 0; i < MAX_SENTRY; i++) {
    Sentry& st = g.sentries[i];
    if (!st.on) continue;
    if (!terrainSolid(g.terrain, st.x, st.y) && !terrainSolid(g.terrain, st.x, st.y + 1)) {
      if (st.y + 1 < 255) st.y = (uint8_t)(st.y + 1);
    }
    if (st.y + 1 >= g.terrain.waterY) {
      destroySentry(st);
      continue;
    }
    if (st.lock < g.nPlayers && (!wormActive(st.lock) || st.lock == st.owner || !sentryCanSee(st, st.lock)))
      st.lock = 0xFF;
    int want = (st.facing < 0) ? 180 : 0;
    if (st.lock < g.nPlayers) want = sentryWantDeg(st, g.p[st.lock]);
    else if (now >= st.armAt && st.shots && st.turnShots < SENTRY_TURN_SHOTS) {
      int best = -1, bestD = SENTRY_RANGE * SENTRY_RANGE;
      for (int p = 0; p < g.nPlayers; p++) {
        if (!wormActive((uint8_t)p) || (uint8_t)p == st.owner || !moved[p]) continue;
        if (!sentryCanSee(st, p)) continue;
        int dx = g.p[p].x - (int)st.x;
        int dy = g.p[p].y - (int)st.y;
        int d2 = dx * dx + dy * dy;
        if (d2 <= bestD) {
          bestD = d2;
          best = p;
        }
      }
      if (best >= 0) {
        st.lock = (uint8_t)best;
        want = sentryWantDeg(st, g.p[best]);
      }
    }
    st.aim = (int16_t)sentryStepAim(st.aim, want, 5);
    st.facing = (st.aim > 90 && st.aim < 270) ? -1 : 1;
    if (now < st.armAt || st.shots == 0 || st.turnShots >= SENTRY_TURN_SHOTS) continue;
    if (st.lock >= g.nPlayers) continue;
    int t = sentryDegToT(st.aim, 160);
    int wt = sentryDegToT(sentryWantDeg(st, g.p[st.lock]), t);
    int dt = wt - t;
    if (dt < 0) dt = -dt;
    if (dt > 8) continue;
    if (now - st.lastShotAt < SENTRY_SHOT_MS && st.lastShotAt != 0) continue;
    if (fireSentryAt(st, g.p[st.lock])) st.turnShots++;
  }
  for (int p = 0; p < g.nPlayers; p++) {
    sSeenX[p] = g.p[p].x;
    sSeenY[p] = g.p[p].y;
  }
}

static void startDeathBurst(uint8_t id) {
  if (id >= g.nPlayers) return;
  Player& pl = g.p[id];
  if (!pl.alive || !pl.dying || pl.bursting) return;
  int cx = pl.x;
  int cy = pl.y - 3;
  pl.bursting = true;
  doExplosion(cx, cy, DEATH_BLAST_R, DEATH_BLAST_DMG, (int)id);
  spawnExplosionBurst(cx, cy, DEATH_BLAST_R);
  pl.burstAt = millis() + EXPLODE_ANIM_MS + DEATH_KILL_MS;
}

static void tickDeaths(uint32_t now) {
  for (int i = 0; i < g.nPlayers; i++) {
    Player& pl = g.p[i];
    if (!pl.alive || !pl.dying) continue;
    if (pl.bursting) {
      if (now >= pl.burstAt) killPlayer((uint8_t)i, "KILLED");
      continue;
    }
    bool water = inWater(pl);
    bool settled = !water && terrainGrounded(g.terrain, pl.x, pl.y) && pl.velX == 0 && pl.velY == 0;
    if (water) hitWater(pl);
    if (water || (pl.dyingAt && now - pl.dyingAt > 5000)) {
      startDeathBurst((uint8_t)i);
      continue;
    }
    if (!settled) {
      pl.burstAt = 0;
      continue;
    }
    if (pl.burstAt == 0) pl.burstAt = now + DEATH_BURST_MS;
    if (now >= pl.burstAt) startDeathBurst((uint8_t)i);
  }
}

static void tickAir(Player& pl) {
  if (!pl.alive) return;
  int id = pidOf(pl);
  if (sDrownHoldUntil[id]) {
    if (millis() < sDrownHoldUntil[id]) {
      pl.velX = 0;
      pl.velY = 0;
      return;
    }
    sDrownHoldUntil[id] = 0;
    if (livePlay()) {
      respawnPlayer((uint8_t)id);
      return;
    }
  }
  bool torching = livePlay() ? (pl.act == PHASE_BLOWTORCH ||
                                (id == g.turn && g.phase == PHASE_BLOWTORCH))
                             : (g.phase == PHASE_BLOWTORCH && id == g.turn);
  if (torching) {
    pl.velX = 0;
    pl.velY = 0;
    return;
  }
  if (!inWater(pl)) sSplashed[id] = false;
  if (inWater(pl)) {
    if (sSpawnProtectUntil[id] && millis() < sSpawnProtectUntil[id]) {
      int nx = pl.x, ny = -1;
      if (pickAnyDryLand((uint8_t)id, &nx, &ny)) {
        pl.x = (int16_t)nx;
        pl.y = (int16_t)ny;
        pl.velX = 0;
        pl.velY = 0;
      }
      return;
    }
    if (sDrownHoldUntil[id] && millis() < sDrownHoldUntil[id]) return;
    if (livePlay() && pl.hp == 0) return;
    hitWater(pl);
    if (!pl.dying) killPlayer((uint8_t)(&pl - g.p), "DROWNED");
    return;
  }
  sSpawnProtectUntil[id] = 0;

  bool grounded = terrainGrounded(g.terrain, pl.x, pl.y);
  uint8_t held = pl.held;
  if (pidOf(pl) == g.turn) held = (uint8_t)(held | g.heldButtons);
  bool jet = livePlay() ? (pl.act == PHASE_JETPACK)
                       : (g.phase == PHASE_JETPACK && pidOf(pl) == g.turn);
  bool jetHover = sJetWasOn[id] && (pl.act == PHASE_WEAPON_MENU || pl.act == PHASE_CHARGING ||
                                    pl.act == PHASE_TARGET ||
                                    (!livePlay() && (g.phase == PHASE_WEAPON_MENU ||
                                                     g.phase == PHASE_CHARGING ||
                                                     g.phase == PHASE_TARGET)));
  if (jetHover) {
    pl.velY = 0;
    jet = true;
  }
  bool jetThrust = jet && !jetHover && (held & (1 << BTN_AIM_UP)) != 0;
  bool jetDive = jet && !jetThrust && (held & (1 << BTN_AIM_DN)) != 0;
  if (jetThrust || jetDive) pl.velY = 0;
  if (!grounded && pl.velY == 0 && !jetThrust) {
    if (terrainGrounded(g.terrain, pl.x, pl.y + 1)) {
      pl.y = (int16_t)(pl.y + 1);
      grounded = true;
    }
  }
  if (grounded && pl.velY >= 0) {
    pl.jumpsLeft = JUMP_COUNT;
    sJumpKind[id] = 0;
    sCoyoteUntil[id] = millis() + COYOTE_MS;
  }

  if (!jetThrust && !jetDive && (pl.velY != 0 || !grounded)) {
    int stepY = pl.velY;
    if (stepY > 2) stepY = 2;
    if (stepY < -3) stepY = -3;
    int ny = pl.y + stepY;
    if (ny < wormMinY()) {
      ny = wormMinY();
      pl.velY = 0;
    }
    if (pl.velY < 0) {
      if (!wormFits(g.terrain, pl.x, ny) ||
          terrainSolid(g.terrain, pl.x, ny - 5) || terrainSolid(g.terrain, pl.x, ny - 4)) {
        pl.velY = 0;
      } else {
        pl.y = (int16_t)ny;
      }
    } else {
      if (ny + 1 >= g.terrain.waterY) {
        pl.y = (int16_t)ny;
        if (pl.y + 1 >= g.terrain.waterY) {
          hitWater(pl);
          if (!pl.dying) killPlayer((uint8_t)(&pl - g.p), "DROWNED");
        }
        return;
      }
      if (terrainSolid(g.terrain, pl.x, ny + 1)) {
        while (pl.y < ny && !terrainGrounded(g.terrain, pl.x, pl.y)) pl.y++;
        if (terrainGrounded(g.terrain, pl.x, pl.y)) pl.velY = 0;
        else pl.y = (int16_t)ny;
      } else {
        pl.y = (int16_t)ny;
      }
    }
    if (pl.velY != 0 || !terrainGrounded(g.terrain, pl.x, pl.y)) {
      sAirHang[id]++;
      bool airGlide = !jet && (held & ((1 << BTN_LEFT) | (1 << BTN_RIGHT))) != 0;
      uint8_t period = 1;
      if (jet) period = jetThrust ? 12 : ((pl.velY > 0) ? 6 : 5);
      else if (airGlide && pl.velY < 0) period = 5;
      else if (airGlide) period = 8;
      else if (pl.velY < 0) {
        if (sJumpKind[id] == 2) period = 3;
        else period = 4;
      }
      else if (pl.velY <= 1) period = 5;
      if (sAirHang[id] >= period) {
        sAirHang[id] = 0;
        if (!jetThrust) {
          int8_t cap = jet ? 1 : (airGlide ? 1 : 2);
          if (pl.velY < cap) pl.velY = (int8_t)(pl.velY + 1);
        }
      }
    } else {
      sAirHang[id] = 0;
    }
  }

  if (pl.velX) {
    int step = pl.velX;
    if (step > 2) step = 2;
    if (step < -2) step = -2;
    int dir = step > 0 ? 1 : -1;
    int left = step > 0 ? step : -step;
    uint8_t held = pl.held;
    if (id == g.turn) held = (uint8_t)(held | g.heldButtons);
    bool holdDir = (dir < 0 && (held & (1 << BTN_LEFT))) ||
                   (dir > 0 && (held & (1 << BTN_RIGHT)));
    for (int s = 0; s < left; s++) {
      int tx = pl.x + dir;
      bool moved = false;
      if (tx < 3 || tx > WORLD_W - 4) {
        pl.velX = 0;
        break;
      }
      if (wormFits(g.terrain, tx, pl.y)) {
        pl.x = (int16_t)tx;
        moved = true;
      } else {
        for (int up = 1; up <= 2; up++) {
          int ny = pl.y - up;
          if (!wormFits(g.terrain, tx, ny)) continue;
          pl.x = (int16_t)tx;
          pl.y = (int16_t)ny;
          moved = true;
          break;
        }
      }
      if (!moved) {
        pl.velX = 0;
        break;
      }
    }
    if (!holdDir) {
      sVelXFric[id]++;
      uint8_t fric = terrainGrounded(g.terrain, pl.x, pl.y) ? 1 : 3;
      if (sVelXFric[id] >= fric) {
        sVelXFric[id] = 0;
        if (pl.velX > 0) pl.velX--;
        else if (pl.velX < 0) pl.velX++;
      }
    } else {
      sVelXFric[id] = 0;
    }
  }

  if (inWater(pl) && !pl.dying) {
    hitWater(pl);
    killPlayer((uint8_t)(&pl - g.p), "DROWNED");
  }
}

static void pollGestures() {
  sGesture = 0;
  static bool consumed;
  if (btnPressed(BTN_WEAPON)) consumed = false;
  uint16_t held = btnHeld(BTN_WEAPON);
  if (btnDown(BTN_WEAPON) && !consumed && held >= PAUSE_HOLD_MS) consumed = true;
  if (btnReleased(BTN_WEAPON) && !consumed) sGesture = 2;
  if (sGesture == 2) sPendingAct |= 4;
  static bool zoomLatch = false;
  if (btnDown(BTN_LEFT) && btnDown(BTN_RIGHT)) {
    if (!zoomLatch && (btnPressed(BTN_LEFT) || btnPressed(BTN_RIGHT))) {
      g.zoom = (uint8_t)(g.zoom >= ZOOM_MAX ? ZOOM_MIN : g.zoom + 1);
      zoomLatch = true;
    }
  } else {
    zoomLatch = false;
  }
}

static bool weaponOffered(uint8_t w) {
  if (w >= WPN_COUNT) return false;
  if (w == WPN_AIRSTRIKE && g.mapKind == MAP_CAVERN) return false;
  return true;
}

static void stepWeaponMenuRow(int dir) {
  int col = g.menuSel / WPN_MENU_ROWS;
  int row = g.menuSel % WPN_MENU_ROWS;
  for (int n = 1; n <= WPN_MENU_ROWS; n++) {
    int r = (row + WPN_MENU_ROWS + dir * n) % WPN_MENU_ROWS;
    int cand = col * WPN_MENU_ROWS + r;
    if (cand < WPN_COUNT && weaponOffered(wpnMenuAt((uint8_t)cand))) {
      g.menuSel = (uint8_t)cand;
      return;
    }
  }
}

static void stepWeaponMenuCol(int dir) {
  int col = g.menuSel / WPN_MENU_ROWS;
  int row = g.menuSel % WPN_MENU_ROWS;
  for (int n = 1; n <= WPN_MENU_COLS; n++) {
    int c = (col + WPN_MENU_COLS + dir * n) % WPN_MENU_COLS;
    int cand = c * WPN_MENU_ROWS + row;
    if (cand < WPN_COUNT && weaponOffered(wpnMenuAt((uint8_t)cand))) {
      g.menuSel = (uint8_t)cand;
      return;
    }
  }
}

static void tickWeaponMenuNav(bool up, bool dn, bool upEdge, bool dnEdge) {
  static uint32_t lastStep;
  static bool repeating;
  uint32_t now = millis();
  if (up && dn) return;
  if (!up && !dn) {
    repeating = false;
    return;
  }
  if (upEdge || dnEdge) {
    stepWeaponMenuRow(upEdge ? -1 : 1);
    lastStep = now;
    repeating = false;
    return;
  }
  uint32_t wait = repeating ? MENU_REPEAT_MS : MENU_REPEAT_DELAY_MS;
  if (now - lastStep < wait) return;
  stepWeaponMenuRow(up ? -1 : 1);
  lastStep = now;
  repeating = true;
}

static void clientPredict(uint32_t dt) {
  if (g.isHost || g.practice) return;
  if (g.mode != MODE_PLAY) return;
  tickMines(millis());
  static uint32_t cPhys;
  cPhys += dt;
  while (cPhys >= PHYS_DT_MS) {
    cPhys -= PHYS_DT_MS;
    for (int i = 0; i < MAX_PROJ; i++) stepOneProj(g.proj[i], PHYS_DT_MS, false);
  }
  static uint32_t airAccR;
  airAccR += dt;
  while (airAccR >= 16) {
    airAccR -= 16;
    for (int i = 0; i < g.nPlayers; i++) {
      if (i == g.selfId || !g.p[i].alive) continue;
      tickAir(g.p[i]);
    }
  }
  for (int i = 0; i < g.nPlayers; i++) {
    if (i == g.selfId || !g.p[i].alive) continue;
    uint8_t h = g.p[i].held;
    PlayPhase ph = (PlayPhase)g.p[i].act;
    if (ph == PHASE_CONTROL || ph == PHASE_JETPACK || livePlay()) {
      applyMoveHeld(g.p[i], (h & (1 << BTN_LEFT)) != 0, (h & (1 << BTN_RIGHT)) != 0, dt);
      applyAimSky(g.p[i], (h & (1 << BTN_AIM_UP)) != 0, (h & (1 << BTN_AIM_DN)) != 0, dt);
    }
  }
  if (g.selfId >= g.nPlayers) return;
  if (!livePlay() && g.turn != g.selfId) return;
  Player& pl = g.p[g.selfId];
  if (!pl.alive) return;
  if (pl.dying) {
    abortMenusOnDeath(g.selfId);
    static uint32_t airAccD;
    airAccD += dt;
    while (airAccD >= 16) {
      airAccD -= 16;
      tickAir(pl);
    }
    return;
  }
  uint8_t down = 0;
  for (int b = 0; b < BTN_COUNT; b++) {
    if (btnDown(b)) down |= (uint8_t)(1 << b);
  }
  g.heldButtons = down;
  pl.held = down;
  if (g.phase != PHASE_TURN_START && g.phase != PHASE_SD_BANNER &&
      g.phase != PHASE_TURN_END && g.phase != PHASE_EXPLOSION &&
      g.phase != PHASE_FALLING)
    hostControlTick(dt);
  static uint32_t airAccC;
  airAccC += dt;
  while (airAccC >= 16) {
    airAccC -= 16;
    tickAir(pl);
  }
  pl.act = (uint8_t)g.phase;
  pl.charge = g.charge;
  pl.cursorX = g.cursorX;
  pl.menuSel = g.menuSel;
  pl.actAt = g.phaseAt;
}

static void beginTurn() {
  g.wind = (int8_t)rng.range(-4, 5);
  g.charge = 0;
  g.menuSel = 0;
  Player& pl = g.p[g.turn];
  pl.velY = 0;
  pl.velX = 0;
  pl.jumpsLeft = JUMP_COUNT;
  resetHeldMovers();
  sJetFuelAcc[g.turn] = 0;
  sJetWasOn[g.turn] = false;
  sFireLockUntil[g.turn] = 0;
  sDracoLeft = 0;
  sDracoAcc = 0;
  g.homeLocked = false;
  for (int i = 0; i < MAX_SENTRY; i++) {
    g.sentries[i].lock = 0xFF;
    g.sentries[i].turnShots = 0;
    g.sentries[i].turnReadyAt = 0;
    if (g.sentries[i].on) g.sentries[i].shots = SENTRY_SHOTS;
  }
  for (int p = 0; p < g.nPlayers; p++) {
    sSeenX[p] = g.p[p].x;
    sSeenY[p] = g.p[p].y;
  }
  pl.act = PHASE_CONTROL;
  if (!g.cheat) {
    if (pl.weapon == WPN_GRENADE && pl.ammoG == 0) pl.weapon = WPN_BAZOOKA;
    if (pl.weapon == WPN_AIRSTRIKE && (pl.ammoA == 0 || g.mapKind == MAP_CAVERN)) pl.weapon = WPN_BAZOOKA;
    if (pl.weapon == WPN_TELEPORT && pl.ammoT == 0) pl.weapon = WPN_BAZOOKA;
    if (pl.weapon == WPN_BLOWTORCH && pl.ammoB == 0) pl.weapon = WPN_BAZOOKA;
    if (pl.weapon == WPN_MOLLI && pl.ammoMo == 0) pl.weapon = WPN_BAZOOKA;
    if (pl.weapon == WPN_BAT && pl.ammoBat == 0) pl.weapon = WPN_BAZOOKA;
    if (pl.weapon == WPN_JETPACK && pl.ammoJ == 0) pl.weapon = WPN_BAZOOKA;
    if (pl.weapon == WPN_MINE && pl.ammoM == 0) pl.weapon = WPN_BAZOOKA;
    if (pl.weapon == WPN_DYNAMITE && pl.ammoDyn == 0) pl.weapon = WPN_BAZOOKA;
    if (pl.weapon == WPN_HOMING && pl.ammoH == 0) pl.weapon = WPN_BAZOOKA;
    if (pl.weapon == WPN_DRACO && pl.ammoD == 0) pl.weapon = WPN_BAZOOKA;
    if (pl.weapon == WPN_SENTRY && pl.ammoS == 0) pl.weapon = WPN_BAZOOKA;
    if (pl.weapon == WPN_PIPE && pl.ammoP == 0) pl.weapon = WPN_BAZOOKA;
  }
  g.turnMs = turnMsFor(g.turnOpt);
  g.turnArrowUntil = millis() + TURN_ARROW_MS;
  g.cursorX = (uint8_t)pl.x;
  g.phase = PHASE_TURN_START;
  g.phaseAt = millis();
  g.turnBannerUntil = millis() + TURN_BANNER_MS;
  g.turnBannerId = g.turn;
  g.turnLeft = g.turnMs;
  g.turnEndsAt = 0;
  clearProjs();
}

static bool hasAmmo(const Player& pl, uint8_t w) {
  if (w == WPN_AIRSTRIKE && g.mapKind == MAP_CAVERN) return false;
  if (g.cheat) return true;
  if (w == WPN_BAZOOKA) return true;
  if (w == WPN_GRENADE) return pl.ammoG > 0;
  if (w == WPN_AIRSTRIKE) return pl.ammoA > 0;
  if (w == WPN_TELEPORT) return pl.ammoT > 0;
  if (w == WPN_BLOWTORCH) return pl.ammoB > 0;
  if (w == WPN_MOLLI) return pl.ammoMo > 0;
  if (w == WPN_BAT) return pl.ammoBat > 0;
  if (w == WPN_JETPACK) return pl.ammoJ > 0;
  if (w == WPN_MINE) return pl.ammoM > 0;
  if (w == WPN_DYNAMITE) return pl.ammoDyn > 0;
  if (w == WPN_HOMING) return pl.ammoH > 0;
  if (w == WPN_DRACO) return pl.ammoD > 0;
  if (w == WPN_SENTRY) return pl.ammoS > 0;
  if (w == WPN_PIPE) return pl.ammoP > 0;
  return false;
}

static void fallbackWeapon(Player& pl) {
  if (g.cheat) return;
  if (pl.weapon == WPN_BAZOOKA) return;
  if (hasAmmo(pl, pl.weapon)) return;
  if (g.phase == PHASE_DRACO || g.phase == PHASE_BLOWTORCH || g.phase == PHASE_BAT ||
      g.phase == PHASE_MOLLI)
    return;
  pl.weapon = WPN_BAZOOKA;
  g.homeLocked = false;
}

static void tryFireWeapon(Player& pl, bool showHint) {
  if (cannotFire(pl)) {
    if (showHint) setHudHint(fireLocked(pl) ? "FUSE" : "WAIT", 280);
    return;
  }
  if (pl.weapon == WPN_HOMING && g.homeLocked) {
    g.phase = PHASE_CHARGING;
    g.phaseAt = millis();
    g.charge = 1;
    return;
  }
  if (pl.weapon == WPN_AIRSTRIKE || pl.weapon == WPN_TELEPORT || pl.weapon == WPN_HOMING) {
    beginTargeting(pl);
    return;
  }
  if (pl.weapon == WPN_DRACO) {
    startDraco(pl);
    return;
  }
  if (pl.weapon == WPN_BLOWTORCH) {
    if (!spendAmmo(pl.ammoB)) { pl.weapon = WPN_BAZOOKA; return; }
    g.molliHit = 0;
    g.phase = PHASE_BLOWTORCH;
    g.phaseAt = millis();
    sTorchAccX[pidOf(pl)] = 0;
    sTorchAccY[pidOf(pl)] = 0;
    sBusyAcc[pidOf(pl)] = 0;
    return;
  }
  if (pl.weapon == WPN_MOLLI) {
    startMolli(pl);
    return;
  }
  if (pl.weapon == WPN_BAT) {
    startBat(pl);
    return;
  }
  if (pl.weapon == WPN_JETPACK) {
    startJetpack(pl);
    return;
  }
  if (pl.weapon == WPN_MINE) {
    placeMine(pl);
    if (livePlay()) g.phase = jetOrControl(pl);
    return;
  }
  if (pl.weapon == WPN_DYNAMITE) {
    placeDynamite(pl);
    if (livePlay()) g.phase = jetOrControl(pl);
    return;
  }
  g.phase = PHASE_CHARGING;
  g.phaseAt = millis();
  g.charge = 1;
}

static void applyLocalOrRemoteButtons(Player& pl, uint8_t down, uint8_t pr, uint8_t rel, int16_t aim, uint8_t wpn,
                                      uint8_t cursor, uint8_t menuSel, uint8_t act) {
  (void)aim;
  (void)menuSel;
  if (g.phase == PHASE_WEAPON_MENU) {
    if ((act & 1) || (pr & (1 << BTN_FIRE))) {
      if (hasAmmo(pl, wpnMenuAt(g.menuSel))) {
        uint8_t w = wpnMenuAt(g.menuSel);
        if (pl.weapon != w) g.homeLocked = false;
        pl.weapon = w;
      }
      g.phase = jetOrControl(pl);
    }
    if ((act & 4)) g.phase = jetOrControl(pl);
    return;
  }
  if (g.phase == PHASE_TARGET) {
    if (pl.weapon == WPN_HOMING) {
      if (cursor < g.nPlayers && g.p[cursor].alive && !g.p[cursor].dying && g.p[cursor].hp > 0) {
        g.homeTarget = cursor;
        g.cursorX = (uint8_t)wm_clamp(g.p[cursor].x, 2, WORLD_W - 3);
      }
    } else {
      g.cursorX = cursor;
    }
    if (act & 1) {
      if (pl.weapon == WPN_HOMING) {
        g.homeLocked = true;
        g.phase = jetOrControl(pl);
        g.phaseAt = millis();
      } else launchWeapon(pl, g.turn, 50);
    }
    if (act & 4) g.phase = jetOrControl(pl);
    return;
  }
  if (g.phase == PHASE_CHARGING) {
    uint32_t held = millis() - g.phaseAt;
    g.charge = (uint8_t)wm_clamp((int16_t)(held * 100 / CHARGE_MS), 1, 100);
    if ((act & 2) || !(down & (1 << BTN_FIRE))) launchWeapon(pl, g.turn, g.charge);
    return;
  }
  if (g.phase == PHASE_JETPACK) {
    if (act & 4) {
      g.menuSel = wpnMenuSlot(pl.weapon);
      g.phase = PHASE_WEAPON_MENU;
      return;
    }
    if ((act & 1) && (millis() - g.phaseAt > JET_CANCEL_MS)) {
      if (pl.weapon == WPN_JETPACK) stopJetpack(pl);
      else tryFireWeapon(pl, false);
    }
    return;
  }
  if (g.phase == PHASE_BLOWTORCH) {
    if ((act & 1) && (millis() - g.phaseAt > TORCH_CANCEL_MS)) afterMelee();
    return;
  }
  if (g.phase != PHASE_CONTROL) return;

  if (wpn < WPN_COUNT) {
    // don't blindly accept empty
  }

  if (act & 8) {
    tryJump(pl);
  }
  if (act & 4) {
    g.menuSel = wpnMenuSlot(pl.weapon);
    g.phase = PHASE_WEAPON_MENU;
    return;
  }
  if (act & 1) tryFireWeapon(pl, false);
}

static void hostControlTick(uint32_t dt) {
  Player& pl = g.p[g.turn];
  if (!pl.alive || pl.dying) {
    abortMenusOnDeath((uint8_t)g.turn);
    if (isActionPhase() && !livePlay()) {
      g.phase = PHASE_FALLING;
      g.phaseAt = millis();
    }
    return;
  }
  bool local = g.practice || (g.turn == g.selfId);
  if (local) {
    uint8_t down = 0;
    for (int i = 0; i < BTN_COUNT; i++) {
      if (btnDown(i)) down |= (uint8_t)(1 << i);
    }
    g.heldButtons = down;
    if (g.phase == PHASE_CONTROL) {
      applyMoveHeld(pl, btnDown(BTN_LEFT), btnDown(BTN_RIGHT), dt);
      applyAimSky(pl, btnDown(BTN_AIM_UP), btnDown(BTN_AIM_DN), dt);
    } else if (g.phase == PHASE_TARGET) {
      applyCursorHeld(btnDown(BTN_LEFT), btnDown(BTN_RIGHT), dt);
    } else if (g.phase == PHASE_BLOWTORCH) {
      steerTorch(pl, btnDown(BTN_LEFT), btnDown(BTN_RIGHT), dt);
      applyAimSky(pl, btnDown(BTN_AIM_UP), btnDown(BTN_AIM_DN), dt);
      sBusyAcc[g.turn] += dt;
      while (sBusyAcc[g.turn] >= 40) {
        sBusyAcc[g.turn] -= 40;
        tickTorch(pl, millis());
        if (g.phase != PHASE_BLOWTORCH) break;
      }
      if (btnPressed(BTN_FIRE) && millis() - g.phaseAt > TORCH_CANCEL_MS) {
        afterMelee();
        return;
      }
    } else if (g.phase == PHASE_MOLLI) {
      sBusyAcc[g.turn] += dt;
      while (sBusyAcc[g.turn] >= 40) {
        sBusyAcc[g.turn] -= 40;
        tickMolli(pl, millis());
        if (g.phase != PHASE_MOLLI) break;
      }
    } else if (g.phase == PHASE_BAT) {
      sBusyAcc[g.turn] += dt;
      while (sBusyAcc[g.turn] >= 40) {
        sBusyAcc[g.turn] -= 40;
        tickBat(pl, millis());
        if (g.phase != PHASE_BAT) break;
      }
    } else if (g.phase == PHASE_JETPACK) {
      tickJetpack(pl, dt);
      applyAimSky(pl, btnDown(BTN_AIM_UP), btnDown(BTN_AIM_DN), dt);
      if (btnPressed(BTN_FIRE) && millis() - g.phaseAt > JET_CANCEL_MS) {
        if (pl.weapon == WPN_JETPACK) {
          stopJetpack(pl);
          return;
        }
        tryFireWeapon(pl, true);
      }
    } else if (g.phase == PHASE_DRACO) {
      tickDraco(pl, dt);
    }
    if (g.phase == PHASE_WEAPON_MENU) {
      if (sJetWasOn[pidOf(pl)]) tickJetHover(pl, dt);
      tickWeaponMenuNav(btnDown(BTN_AIM_UP), btnDown(BTN_AIM_DN),
                        btnPressed(BTN_AIM_UP), btnPressed(BTN_AIM_DN));
      if (btnPressed(BTN_LEFT)) stepWeaponMenuCol(-1);
      if (btnPressed(BTN_RIGHT)) stepWeaponMenuCol(1);
      if (btnPressed(BTN_FIRE)) {
        if (hasAmmo(pl, wpnMenuAt(g.menuSel))) {
          uint8_t w = wpnMenuAt(g.menuSel);
          if (pl.weapon != w) g.homeLocked = false;
          pl.weapon = w;
        }
        g.phase = jetOrControl(pl);
      }
      if (sGesture == 2) g.phase = jetOrControl(pl);
    } else if (g.phase == PHASE_TARGET) {
      if (sJetWasOn[pidOf(pl)]) tickJetHover(pl, dt);
      if (btnPressed(BTN_FIRE)) {
        if (pl.weapon == WPN_HOMING) {
          g.homeLocked = true;
          g.phase = jetOrControl(pl);
          g.phaseAt = millis();
        } else launchWeapon(pl, g.turn, 50);
      }
      if (sGesture == 2) g.phase = jetOrControl(pl);
    } else if (g.phase == PHASE_CHARGING) {
      if (sJetWasOn[pidOf(pl)]) tickJetHover(pl, dt);
      applyAimSky(pl, btnDown(BTN_AIM_UP), btnDown(BTN_AIM_DN), dt);
      uint32_t held = millis() - g.phaseAt;
      g.charge = (uint8_t)wm_clamp((int16_t)(held * 100 / CHARGE_MS), 1, 100);
      if (btnReleased(BTN_FIRE)) launchWeapon(pl, g.turn, g.charge);
    } else if (g.phase == PHASE_JETPACK) {
      if (sGesture == 2) {
        g.menuSel = wpnMenuSlot(pl.weapon);
        g.phase = PHASE_WEAPON_MENU;
      }
    } else if (g.phase == PHASE_CONTROL) {
      if (sGesture == 2) {
        g.menuSel = wpnMenuSlot(pl.weapon);
        g.phase = PHASE_WEAPON_MENU;
      } else if (btnPressed(BTN_FIRE)) {
        tryFireWeapon(pl, true);
      }
    }
  } else {
    static uint8_t prevHeldP[MAX_PLAYERS];
    uint8_t& prevHeld = prevHeldP[g.turn < MAX_PLAYERS ? g.turn : 0];
    uint8_t down = pl.held;
    g.heldButtons = down;
    bool up = (down & (1 << BTN_AIM_UP)) != 0;
    bool dn = (down & (1 << BTN_AIM_DN)) != 0;
    bool upEdge = up && !(prevHeld & (1 << BTN_AIM_UP));
    bool dnEdge = dn && !(prevHeld & (1 << BTN_AIM_DN));
    bool lEdge = (down & (1 << BTN_LEFT)) && !(prevHeld & (1 << BTN_LEFT));
    bool rEdge = (down & (1 << BTN_RIGHT)) && !(prevHeld & (1 << BTN_RIGHT));
    bool fireEdge = (down & (1 << BTN_FIRE)) && !(prevHeld & (1 << BTN_FIRE));
    prevHeld = down;
    if (g.phase == PHASE_CONTROL) {
      if (local || !livePlay()) {
        applyMoveHeld(pl, (down & (1 << BTN_LEFT)) != 0, (down & (1 << BTN_RIGHT)) != 0, dt);
        applyAimSky(pl, up, dn, dt);
      }
    } else if (g.phase == PHASE_WEAPON_MENU) {
      if (sJetWasOn[pidOf(pl)]) tickJetHover(pl, dt);
      if (local || !livePlay()) {
        tickWeaponMenuNav(up, dn, upEdge, dnEdge);
        if (lEdge) stepWeaponMenuCol(-1);
        if (rEdge) stepWeaponMenuCol(1);
      }
    } else if (g.phase == PHASE_TARGET) {
      if (sJetWasOn[pidOf(pl)]) tickJetHover(pl, dt);
      if (local || !livePlay())
        applyCursorHeld((down & (1 << BTN_LEFT)) != 0, (down & (1 << BTN_RIGHT)) != 0, dt);
    } else if (g.phase == PHASE_BLOWTORCH) {
      if (local || !livePlay()) {
        steerTorch(pl, (down & (1 << BTN_LEFT)) != 0, (down & (1 << BTN_RIGHT)) != 0, dt);
        applyAimSky(pl, up, dn, dt);
      }
      int16_t keepX = pl.x, keepY = pl.y;
      sBusyAcc[g.turn] += dt;
      while (sBusyAcc[g.turn] >= 40) {
        sBusyAcc[g.turn] -= 40;
        tickTorch(pl, millis());
        if (g.phase != PHASE_BLOWTORCH) break;
      }
      if (!local && livePlay()) {
        pl.x = keepX;
        pl.y = keepY;
      }
      if (fireEdge && millis() - g.phaseAt > TORCH_CANCEL_MS) afterMelee();
    } else if (g.phase == PHASE_MOLLI) {
      sBusyAcc[g.turn] += dt;
      while (sBusyAcc[g.turn] >= 40) {
        sBusyAcc[g.turn] -= 40;
        tickMolli(pl, millis());
        if (g.phase != PHASE_MOLLI) break;
      }
    } else if (g.phase == PHASE_BAT) {
      sBusyAcc[g.turn] += dt;
      while (sBusyAcc[g.turn] >= 40) {
        sBusyAcc[g.turn] -= 40;
        tickBat(pl, millis());
        if (g.phase != PHASE_BAT) break;
      }
    } else if (g.phase == PHASE_JETPACK) {
      tickJetpack(pl, dt);
      applyAimSky(pl, up, dn, dt);
    } else if (g.phase == PHASE_DRACO) {
      tickDraco(pl, dt);
    } else if (g.phase == PHASE_CHARGING) {
      if (sJetWasOn[pidOf(pl)]) tickJetHover(pl, dt);
      uint32_t heldMs = millis() - g.phaseAt;
      g.charge = (uint8_t)wm_clamp((int16_t)(heldMs * 100 / CHARGE_MS), 1, 100);
    }
  }
  fallbackWeapon(pl);
  pl.jetting = sJetWasOn[pidOf(pl)] || g.phase == PHASE_JETPACK;
}

static void syncLiveIn(uint8_t pid) {
  Player& pl = g.p[pid];
  g.turn = pid;
  PlayPhase ph = (PlayPhase)pl.act;
  if (ph == PHASE_TURN_START || ph == PHASE_PROJECTILE || ph == PHASE_EXPLOSION ||
      ph == PHASE_FALLING || ph == PHASE_TURN_END || ph == PHASE_SD_BANNER)
    ph = PHASE_CONTROL;
  g.phase = ph;
  g.charge = pl.charge;
  g.cursorX = pl.cursorX;
  g.menuSel = pl.menuSel;
  g.phaseAt = pl.actAt ? pl.actAt : millis();
  g.heldButtons = pl.held;
}

static void syncLiveOut(uint8_t pid) {
  Player& pl = g.p[pid];
  PlayPhase ph = g.phase;
  if (ph == PHASE_PROJECTILE || ph == PHASE_EXPLOSION || ph == PHASE_FALLING ||
      ph == PHASE_TURN_END || ph == PHASE_SD_BANNER || ph == PHASE_TURN_START)
    ph = PHASE_CONTROL;
  pl.act = (uint8_t)ph;
  pl.charge = g.charge;
  pl.cursorX = g.cursorX;
  pl.menuSel = g.menuSel;
  pl.actAt = g.phaseAt;
  pl.held = g.heldButtons;
  pl.jetting = sJetWasOn[pid] || ph == PHASE_JETPACK;
}

static void startLivePlay() {
  g.phase = PHASE_CONTROL;
  g.phaseAt = millis();
  g.matchAt = millis();
  g.liveRiseAt = 0;
  g.turnEndsAt = 0xFFFFFFFFu;
  g.turnLeft = 0;
  g.turn = g.selfId < g.nPlayers ? g.selfId : 0;
  resetHeldMovers();
  for (int i = 0; i < g.nPlayers; i++) {
    g.p[i].act = PHASE_CONTROL;
    g.p[i].actAt = millis();
    g.p[i].charge = 0;
    g.p[i].menuSel = g.p[i].weapon;
    g.p[i].cursorX = (uint8_t)wm_clamp(g.p[i].x, 2, WORLD_W - 3);
    sJetWasOn[i] = false;
  }
}

static void hostLiveSim(uint32_t now, uint32_t dt) {
  checkWin();
  if (g.mode != MODE_PLAY) return;
  tickMines(now);
  tickSentries(now);
  tickDeaths(now);
  if (now - g.matchAt > 50000) {
    if (!g.sudden) {
      g.sudden = true;
      g.liveRiseAt = now;
      setMsg("SUDDEN", "DEATH", 1000);
      if (g.isHost) sendEvent(EVT_SUDDEN, 0);
    } else if (now - g.liveRiseAt > 12000 && g.waterY > WATER_MAX_Y) {
      g.liveRiseAt = now;
      g.waterY = (uint8_t)(g.waterY - WATER_RISE);
      g.terrain.waterY = g.waterY;
    }
  }
  for (int i = 0; i < g.nPlayers; i++) {
    if (!g.p[i].alive || !g.p[i].connected || g.p[i].dying) continue;
    syncLiveIn((uint8_t)i);
    hostControlTick(dt);
    syncLiveOut((uint8_t)i);
  }
  static uint32_t airAccL;
  airAccL += dt;
  while (airAccL >= 16) {
    airAccL -= 16;
    for (int i = 0; i < g.nPlayers; i++) tickAir(g.p[i]);
  }
  physAcc += dt;
  while (physAcc >= PHYS_DT_MS) {
    physAcc -= PHYS_DT_MS;
    for (int i = 0; i < MAX_PROJ; i++) stepOneProj(g.proj[i], PHYS_DT_MS);
  }
  if (g.selfId < g.nPlayers) {
    g.turn = g.selfId;
    g.phase = (PlayPhase)g.p[g.selfId].act;
    g.charge = g.p[g.selfId].charge;
    g.cursorX = g.p[g.selfId].cursorX;
    g.menuSel = g.p[g.selfId].menuSel;
    g.phaseAt = g.p[g.selfId].actAt;
  } else {
    g.phase = PHASE_CONTROL;
    g.turn = 0;
  }
  if (g.recoilFrames) g.recoilFrames--;
  if (now - g.lastSnapSend >= 50) sendSnapshot();
}

static void hostSim(uint32_t now, uint32_t dt) {
  if (g.mode == MODE_COUNTDOWN) {
    if (now - g.phaseAt >= COUNTDOWN_MS) {
      g.mode = MODE_PLAY;
      if (livePlay()) startLivePlay();
      else beginTurn();
    }
    return;
  }
  if (g.mode != MODE_PLAY) return;
  if (livePlay()) {
    hostLiveSim(now, dt);
    return;
  }

  if (g.phase == PHASE_TURN_START) {
    if (now - g.phaseAt >= TURN_BANNER_MS) {
      g.phase = PHASE_CONTROL;
      g.phaseAt = now;
      g.turnMs = turnMsFor(g.turnOpt);
      g.turnEndsAt = now + g.turnMs;
      g.turnLeft = g.turnMs;
    }
    return;
  }
  if (g.phase == PHASE_SD_BANNER) {
    if (now - g.phaseAt > 1000) beginTurn();
    return;
  }

  tickMines(now);
  tickSentries(now);
  tickDeaths(now);

  if (g.phase == PHASE_CONTROL || g.phase == PHASE_WEAPON_MENU || g.phase == PHASE_TARGET ||
      g.phase == PHASE_CHARGING || g.phase == PHASE_BLOWTORCH || g.phase == PHASE_MOLLI ||
      g.phase == PHASE_BAT || g.phase == PHASE_JETPACK || g.phase == PHASE_DRACO) {
    if (g.turnEndsAt == 0) g.turnEndsAt = now + (g.turnLeft ? g.turnLeft : g.turnMs);
    if (g.phase == PHASE_CHARGING) {
      g.turnEndsAt = now + (g.turnLeft ? g.turnLeft : 1);
    } else if (now >= g.turnEndsAt) {
      g.turnLeft = 0;
      g.phase = PHASE_TURN_END;
      g.phaseAt = now;
    } else {
      g.turnLeft = (uint16_t)(g.turnEndsAt - now);
    }
    hostControlTick(dt);
    if (g.turn < MAX_PLAYERS) g.p[g.turn].act = (uint8_t)g.phase;
    static uint32_t airAcc = 0;
    airAcc += dt;
    while (airAcc >= 16) {
      airAcc -= 16;
      for (int i = 0; i < g.nPlayers; i++) tickAir(g.p[i]);
    }
    if (g.phase == PHASE_DRACO || anyProj()) {
      physAcc += dt;
      while (physAcc >= PHYS_DT_MS) {
        physAcc -= PHYS_DT_MS;
        for (int i = 0; i < MAX_PROJ; i++) stepOneProj(g.proj[i], PHYS_DT_MS);
      }
    }
  }

  if (g.phase == PHASE_PROJECTILE) {
    physAcc += dt;
    while (physAcc >= PHYS_DT_MS) {
      physAcc -= PHYS_DT_MS;
      for (int i = 0; i < MAX_PROJ; i++) stepOneProj(g.proj[i], PHYS_DT_MS);
    }
    static uint32_t shotAirAcc = 0;
    shotAirAcc += dt;
    while (shotAirAcc >= 16) {
      shotAirAcc -= 16;
      for (int i = 0; i < g.nPlayers; i++) tickAir(g.p[i]);
    }
    if (!anyProj()) {
      g.phase = PHASE_EXPLOSION;
      g.phaseAt = now;
    }
  }

  if (g.phase == PHASE_EXPLOSION) {
    static uint32_t boomAirAcc = 0;
    boomAirAcc += dt;
    while (boomAirAcc >= 16) {
      boomAirAcc -= 16;
      for (int i = 0; i < g.nPlayers; i++) tickAir(g.p[i]);
    }
    if (now >= g.explodeUntil) {
      g.phase = PHASE_FALLING;
      g.phaseAt = now;
    }
  }

  if (g.phase == PHASE_FALLING) {
    if (anyProj()) {
      physAcc += dt;
      while (physAcc >= PHYS_DT_MS) {
        physAcc -= PHYS_DT_MS;
        for (int i = 0; i < MAX_PROJ; i++) stepOneProj(g.proj[i], PHYS_DT_MS);
      }
    }
    static uint32_t fallAcc = 0;
    fallAcc += dt;
    while (fallAcc >= 16) {
      fallAcc -= 16;
      for (int i = 0; i < g.nPlayers; i++) tickAir(g.p[i]);
    }
    bool settled = allGrounded();
    for (int i = 0; i < g.nPlayers; i++) {
      if (!g.p[i].alive) continue;
      if (g.p[i].velX || g.p[i].velY) settled = false;
    }
    uint32_t hold = g.deathHold ? TURN_END_MS : 400;
    if (g.dmgHangUntil > now) {
      uint32_t extra = g.dmgHangUntil - now;
      if (extra > hold) hold = extra;
    }
    if (g.msg.until > now) {
      uint32_t extra = g.msg.until - now;
      if (extra > hold) hold = extra;
    }
    for (int i = 0; i < g.nPlayers; i++) {
      if (g.dmgUntil[i] > now) {
        uint32_t extra = g.dmgUntil[i] - now;
        if (extra > hold) hold = extra;
      }
    }
    if ((settled && !deathBusy() && !sentryStillShooting() && now - g.phaseAt > hold) ||
        (settled && now - g.phaseAt > FALL_MAX_MS && !deathBusy() && !sentryStillShooting())) {
      if (g.mode == MODE_PLAY) {
        g.phase = PHASE_TURN_END;
        g.phaseAt = now;
      }
    }
  }

  if (g.phase == PHASE_TURN_END) {
    if (deathBusy()) {
      g.phaseAt = now;
    } else {
      uint32_t wait = g.deathHold ? TURN_END_MS : 500;
      if (g.dmgHangUntil > now) {
        uint32_t extra = g.dmgHangUntil - now;
        if (extra > wait) wait = extra;
      }
      if (g.msg.until > now) {
        uint32_t extra = g.msg.until - now;
        if (extra > wait) wait = extra;
      }
      if (wait < 500) wait = 500;
      if (now - g.phaseAt > wait) nextTurn();
    }
  }

  if (g.recoilFrames) g.recoilFrames--;

  uint32_t snapMs = 200;
  bool poseBusy = anyProj();
  for (int i = 0; i < g.nPlayers; i++) {
    if (!g.p[i].alive) continue;
    if (g.p[i].velX || g.p[i].velY) poseBusy = true;
    uint8_t h = g.p[i].held;
    if (i == g.selfId) {
      h = 0;
      for (int b = 0; b < BTN_COUNT; b++)
        if (btnDown(b)) h |= (uint8_t)(1 << b);
    }
    if (h & ((1 << BTN_LEFT) | (1 << BTN_RIGHT) | (1 << BTN_AIM_UP) | (1 << BTN_AIM_DN)))
      poseBusy = true;
  }
  if (g.phase == PHASE_CONTROL || g.phase == PHASE_CHARGING || g.phase == PHASE_JETPACK ||
      g.phase == PHASE_BLOWTORCH || g.phase == PHASE_TURN_START || g.phase == PHASE_DRACO)
    snapMs = poseBusy ? 40 : 90;
  if (g.phase == PHASE_PROJECTILE || anyProj()) snapMs = 40;
  if (now - g.lastSnapSend >= snapMs) sendSnapshot();
}

static bool validSender(uint8_t sender) {
  if (sender == PLAYER_NONE) return true;
  return sender < MAX_PLAYERS;
}

static void handlePkt(const RxPkt& rx) {
  if (!pkt::verify(rx.data, rx.len)) return;
  g.pktOk++;
  BufR r(rx.data, rx.len - 1);
  uint8_t type, sender;
  uint32_t session;
  uint16_t seq;
  if (!pkt::checkHdr(r, type, session, sender, seq) || r.err) return;
  if (!validSender(sender)) return;

  notePeer(rx.mac);
  if (!g.isHost && g.nPlayers > 0 && !macZero(rx.mac) &&
      (macEq(g.p[0].mac, rx.mac) || (!macZero(g.p[0].rxMac) && macEq(g.p[0].rxMac, rx.mac)))) {
    g.p[0].lastSeen = tickNow();
    sHeardAt = tickNow();
  }

  bool sessionFree = type == pkt::DISCOVERY || type == pkt::JOIN || type == pkt::LOBBY ||
                     type == pkt::QUIT;
  if (type == pkt::START &&
      (g.mode == MODE_LOBBY || g.mode == MODE_SEEK || g.mode == MODE_REMATCH ||
       g.mode == MODE_GAMEOVER))
    sessionFree = true;
  if (!sessionFree) {
    if (g.practice) return;
    if (g.session == 0 || session == 0 || session != g.session) return;
  }

  if (g.isHost && type == pkt::INP) {
    int byMac = findPlayerMac(rx.mac, rx.mac);
    if (byMac > 0) sender = (uint8_t)byMac;
  }

  if (sender < MAX_PLAYERS && g.p[sender].present) {
    g.p[sender].lastSeen = millis();
    g.p[sender].connected = true;
    if (!macZero(rx.mac)) memcpy(g.p[sender].rxMac, rx.mac, 6);
    if (type == pkt::INP) {
      if (!g.p[sender].seqInit) g.p[sender].seqInit = true;
      else if ((int16_t)(seq - g.p[sender].lastSeq) <= 0) return;
      g.p[sender].lastSeq = seq;
    }
  }

  switch (type) {
    case pkt::DISCOVERY: {
      uint8_t mac[6];
      if (!r.raw(mac, 6) || r.err) return;
      uint8_t hosting = 0, n = 1, mk = 0, live = 0;
      uint32_t sess = 0;
      if (r.left() >= 1) hosting = r.u8();
      if (r.left() >= 1) n = r.u8();
      if (r.left() >= 4) sess = r.u32();
      if (r.left() >= 1) mk = r.u8();
      if (r.left() >= 1) live = r.u8();
      uint8_t wantJoin = 0;
      uint8_t wantMac[6] = {};
      if (r.left() >= 1) wantJoin = r.u8();
      if (r.left() >= 6) r.raw(wantMac, 6);
      (void)wantJoin;
      (void)wantMac;
      if (g.mode == MODE_SEEK && hosting && !g.joining) {
        rememberFound(mac, sess, n, mk, live);
        if (!macEq(mac, rx.mac)) rememberFound(rx.mac, sess, n, mk, live);
      }
      break;
    }
    case pkt::JOIN: {
      uint8_t mac[6];
      if (!r.raw(mac, 6) || r.err) return;
      hostAddJoiner(mac, rx.mac);
      break;
    }
    case pkt::LOBBY: {
      if (g.isHost && g.mode == MODE_LOBBY) break;
      uint8_t n = r.u8();
      if (r.err || n > MAX_PLAYERS) return;
      bool inGroup = false;
      Player np[MAX_PLAYERS];
      memset(np, 0, sizeof(np));
      for (int i = 0; i < MAX_PLAYERS; i++) {
        r.raw(np[i].mac, 6);
        uint8_t fl = r.u8();
        if (r.err) return;
        np[i].present = fl & 1;
        np[i].ready = fl & 2;
        np[i].connected = fl & 4;
        np[i].lastSeen = millis();
        if (np[i].present && macEq(np[i].mac, g.selfMac)) inGroup = true;
        if (np[i].present) netEnsurePeer(np[i].mac);
      }
      uint8_t mk = r.u8();
      uint8_t to = r.u8();
      uint8_t live = r.u8();
      const uint8_t* hostMac = np[0].present ? np[0].mac : rx.mac;
      if (g.mode == MODE_SEEK && !g.joining) {
        rememberFound(rx.mac, session, n, mk, live);
        if (!macEq(rx.mac, hostMac)) rememberFound(hostMac, session, n, mk, live);
      }
      if (g.mode == MODE_SEEK) {
        bool target = lobbyIsJoinTarget(rx.mac, hostMac, session);
        if (target && inGroup && n >= 2) {
          if (!macZero(rx.mac)) memcpy(np[0].rxMac, rx.mac, 6);
          applyLobbyRoster(np, n, session, seq, mk, to, live);
          g.joining = false;
          g.mode = MODE_LOBBY;
          g.phaseAt = tickNow();
          g.lobbyQuitAsk = false;
        } else if (target && n >= MAX_PLAYERS && !inGroup) {
          g.joining = false;
          g.mode = MODE_GROUP_FULL;
          g.phaseAt = tickNow();
        }
        break;
      }
      if (g.mode != MODE_LOBBY || g.isHost) break;
      if (g.session != 0 && session != 0 && session != g.session && !inGroup) break;
      if (inGroup || session == g.session) sHeardAt = tickNow();
      if (!macZero(rx.mac)) memcpy(np[0].rxMac, rx.mac, 6);
      if (sLobbySeqInit && session == g.session && (int16_t)(seq - sLobbySeq) <= 0) break;
      if (!inGroup) break;
      applyLobbyRoster(np, n, session, seq, mk, to, live);
      g.joining = false;
      break;
    }
    case pkt::READY: {
      uint8_t on = r.u8();
      if (r.err || !g.isHost || g.mode != MODE_LOBBY) break;
      uint8_t mac[6] = {};
      if (r.left() >= 6) r.raw(mac, 6);
      int i = findPlayerMac(mac, rx.mac);
      if (i < 0 && sender > 0 && sender < g.nPlayers) i = (int)sender;
      if (i > 0 && i < g.nPlayers) g.p[i].ready = on != 0;
      sendLobby();
      sendLobby();
      break;
    }
    case pkt::START: {
      if (g.mode == MODE_PLAY || g.mode == MODE_COUNTDOWN || g.mode == MODE_PAUSE) {
        if (session == 0 || session == g.session) break;
      }
      if (g.mode == MODE_SEEK && !g.joining) break;
      if (g.mode != MODE_LOBBY && g.mode != MODE_SEEK && g.mode != MODE_REMATCH &&
          g.mode != MODE_GAMEOVER && g.mode != MODE_PLAY && g.mode != MODE_COUNTDOWN &&
          g.mode != MODE_PAUSE)
        break;
      uint8_t n = r.u8();
      uint32_t ms = r.u32();
      uint32_t gs = r.u32();
      uint8_t wy = r.u8();
      uint8_t mk = r.u8();
      uint8_t to = r.u8();
      uint8_t live = r.u8();
      uint8_t cheat = r.u8();
      uint8_t lives = r.u8();
      uint8_t macs[MAX_PLAYERS][6];
      for (int i = 0; i < MAX_PLAYERS; i++) r.raw(macs[i], 6);
      if (r.err || n < 2 || n > 4) return;
      bool inGroup = false;
      for (int i = 0; i < n && i < MAX_PLAYERS; i++)
        if (macEq(macs[i], g.selfMac)) inGroup = true;
      if (!inGroup) break;
      g.session = session;
      g.joining = false;
      applyStart(n, ms, gs, wy, mk, to, live, cheat, lives, macs);
      break;
    }
    case pkt::INP: {
      if (!g.isHost) break;
      if (g.mode != MODE_PLAY) break;
      if (sender >= g.nPlayers || sender == g.selfId || !g.p[sender].present) {
        int byMac = findPlayerMac(rx.mac, rx.mac);
        if (byMac > 0) sender = (uint8_t)byMac;
      }
      if (sender >= g.nPlayers) break;
      if (!livePlay() && sender != g.turn) break;
      uint8_t down = r.u8(), pr = r.u8(), rel = r.u8();
      int16_t aim = r.i16();
      uint8_t wpn = r.u8(), cursor = r.u8(), menu = r.u8(), act = r.u8();
      uint8_t rxX = 0, rxY = 0;
      int8_t rxVelY = 0, rxVelX = 0;
      bool hasPos = r.left() >= 2;
      if (hasPos) {
        rxX = r.u8();
        rxY = r.u8();
      }
      bool hasVel = r.left() >= 2;
      if (hasVel) {
        rxVelY = r.i8();
        rxVelX = r.i8();
      }
      if (r.err) return;
      Player& pl = g.p[sender];
      applyRemoteLook(pl, aim, act);
      if (hasPos) {
        int nx = rxX;
        int ny = unpackY(rxY);
        int minY = openSky() ? SKY_MIN_Y : WORLD_TOP;
        if (nx > 2 && nx < WORLD_W - 3 && ny >= minY && ny < g.terrain.waterY) {
          if (hasVel) {
            pl.velY = rxVelY;
            pl.velX = rxVelX;
          }
          pl.x = (int16_t)nx;
          pl.y = (int16_t)ny;
        }
      }
      if (livePlay()) {
        uint8_t savedTurn = g.turn;
        PlayPhase savedPh = g.phase;
        uint8_t savedCh = g.charge, savedCx = g.cursorX, savedMn = g.menuSel;
        uint32_t savedAt = g.phaseAt;
        g.turn = sender;
        g.phase = (PlayPhase)pl.act;
        g.charge = pl.charge;
        g.cursorX = cursor;
        g.menuSel = menu;
        g.phaseAt = pl.actAt;
        g.heldButtons = down;
        pl.held = down;
        if (act & 8) tryJump(pl);
        applyLocalOrRemoteButtons(pl, down, pr, rel, aim, wpn, cursor, menu, act);
        pl.act = (uint8_t)g.phase;
        pl.charge = g.charge;
        pl.cursorX = g.cursorX;
        pl.menuSel = g.menuSel;
        pl.actAt = g.phaseAt;
        g.turn = savedTurn;
        g.phase = savedPh;
        g.charge = savedCh;
        g.cursorX = savedCx;
        g.menuSel = savedMn;
        g.phaseAt = savedAt;
      } else {
        g.heldButtons = down;
        pl.held = down;
        if (act & 8) tryJump(pl);
        applyLocalOrRemoteButtons(pl, down, pr, rel, aim, wpn, cursor, menu, act);
      }
      break;
    }
    case pkt::SNAPSHOT: {
      if (g.isHost || g.practice) break;
      if (g.mode != MODE_COUNTDOWN && g.mode != MODE_PLAY && g.mode != MODE_PAUSE &&
          g.mode != MODE_GAMEOVER && g.mode != MODE_REMATCH)
        break;
      if (g.hostSeqInit && (int16_t)(seq - g.hostSeqSeen) <= 0) {
        sHeardAt = tickNow();
        break;
      }
      uint8_t mode = r.u8(), phase = r.u8(), turn = r.u8(), n = r.u8();
      PlayPhase prevPhase = g.phase;
      uint8_t prevTurn = g.turn;
      int8_t wind = r.i8();
      uint8_t water = r.u8();
      uint16_t tl = r.u16();
      uint8_t charge = r.u8(), cursor = r.u8(), menu = r.u8(), roundN = r.u8(), sudden = r.u8();
      uint8_t live = r.u8(), winner = r.u8();
      uint8_t ex = r.u8(), ey = r.u8(), er = r.u8();
      uint16_t crc = r.u16();
      if (r.err) return;
      g.hostSeqInit = true;
      g.hostSeqSeen = seq;
      sHeardAt = tickNow();
      if (g.nPlayers > 0) g.p[0].lastSeen = tickNow();
      GameMode snapMode = (GameMode)mode;
      bool keepReady = (g.mode == MODE_REMATCH && snapMode == MODE_REMATCH &&
                        g.selfId < MAX_PLAYERS);
      bool myReady = keepReady && g.p[g.selfId].ready;
      bool modeOk = snapMode == MODE_COUNTDOWN || snapMode == MODE_PLAY ||
                    snapMode == MODE_PAUSE || snapMode == MODE_GAMEOVER ||
                    snapMode == MODE_REMATCH;
      bool alreadyPlay = g.mode == MODE_PLAY || g.mode == MODE_PAUSE ||
                         g.mode == MODE_GAMEOVER || g.mode == MODE_REMATCH;
      if (snapMode == MODE_COUNTDOWN && alreadyPlay) {
        /* keep play — late countdown snapshots must not pin GET READY */
      } else if (modeOk && !(sWantPause && g.mode == MODE_PAUSE && snapMode == MODE_PLAY)) {
        uint32_t nowMs = millis();
        bool holdRematch = snapMode == MODE_REMATCH &&
                           (g.msg.until > nowMs || sPendMsgAt != 0 || g.mode == MODE_PLAY ||
                            (g.mode == MODE_GAMEOVER &&
                             (nowMs - g.phaseAt < 1800 || matchMsgsPending(nowMs))));
        if (holdRematch && g.mode == MODE_GAMEOVER && nowMs - g.phaseAt > 5500)
          holdRematch = false;
        if (holdRematch) {
          if (g.mode == MODE_PLAY) {
            g.mode = MODE_GAMEOVER;
            g.phaseAt = nowMs;
          }
        } else {
          g.mode = snapMode;
        }
      }
      g.nPlayers = n;
      g.wind = wind;
      g.waterY = water;
      g.terrain.waterY = water;
      g.turnLeft = tl;
      if (!g.live || g.isHost) {
        g.cursorX = cursor;
        g.menuSel = menu;
      }
      g.roundN = roundN;
      if (snapMode == MODE_COUNTDOWN || g.mode == MODE_COUNTDOWN ||
          g.mode == MODE_REMATCH || g.mode == MODE_GAMEOVER)
        g.sudden = false;
      else
        g.sudden = sudden != 0;
      g.live = live != 0;
      g.winner = winner;
      g.explodeX = ex; g.explodeY = ey; g.explodeR = er;
      g.session = session;
      PlayPhase hostPh = (PlayPhase)phase;
      bool holdLocalAction = !g.live && g.turn == g.selfId &&
                             ((g.phase == PHASE_JETPACK && hostPh == PHASE_CONTROL) ||
                              (g.phase == PHASE_CHARGING && hostPh == PHASE_CONTROL) ||
                              (g.phase == PHASE_DRACO && hostPh == PHASE_CONTROL) ||
                              (g.phase == PHASE_BLOWTORCH && hostPh == PHASE_CONTROL) ||
                              (g.phase == PHASE_MOLLI && hostPh == PHASE_CONTROL) ||
                              (g.phase == PHASE_BAT && hostPh == PHASE_CONTROL) ||
                              (g.phase == PHASE_TARGET && hostPh == PHASE_CONTROL) ||
                              (g.phase == PHASE_WEAPON_MENU && hostPh == PHASE_CONTROL));
      if (!g.live) {
        g.turn = turn;
        if (!holdLocalAction) {
          g.phase = hostPh;
          g.charge = charge;
        }
      } else {
        g.turn = g.selfId < n ? g.selfId : turn;
      }
      if (g.phase == PHASE_PROJECTILE && hostPh == PHASE_CONTROL && g.turn == g.selfId)
        g.phase = PHASE_CONTROL;
      bool keepSelf = !g.isHost && g.selfId < n && g.mode == MODE_PLAY &&
                      (g.live ||
                       (g.turn == g.selfId &&
                        (g.phase == PHASE_CONTROL || g.phase == PHASE_CHARGING ||
                         g.phase == PHASE_BLOWTORCH || g.phase == PHASE_MOLLI ||
                         g.phase == PHASE_BAT || g.phase == PHASE_JETPACK ||
                         g.phase == PHASE_DRACO)));
      for (int i = 0; i < MAX_PLAYERS; i++) {
        uint8_t fl = r.u8();
        bool wasAlive = g.p[i].alive;
        bool wasDying = g.p[i].dying;
        g.p[i].present = fl & 1;
        g.p[i].ready = fl & 2;
        g.p[i].alive = fl & 4;
        g.p[i].connected = fl & 8;
        g.p[i].dying = (fl & 32) != 0;
        g.p[i].bursting = (fl & 64) != 0;
        g.p[i].jetting = (fl & 128) != 0;
        if (!wasDying && g.p[i].dying && g.p[i].alive)
          focusDeathCam((uint8_t)i, DEATH_BURST_MS + 2500);
        if (wasAlive && !g.p[i].alive)
          focusDeathCam((uint8_t)i, MSG_MS + 400);
        int8_t face = (fl & 16) ? -1 : 1;
        uint8_t oldHp = g.p[i].hp;
        g.p[i].hp = r.u8();
        uint8_t nx = r.u8(), nyb = r.u8();
        int16_t ny = unpackY(nyb);
        g.p[i].weapon = r.u8();
        g.p[i].ammoG = r.u8();
        g.p[i].ammoA = r.u8();
        g.p[i].ammoT = r.u8();
        g.p[i].ammoB = r.u8();
        g.p[i].ammoM = r.u8();
        uint8_t batMo = r.u8();
        g.p[i].ammoBat = batMo & 0x0F;
        g.p[i].ammoMo = batMo >> 4;
        g.p[i].ammoJ = r.u8();
        g.p[i].ammoH = r.u8();
        g.p[i].ammoD = r.u8();
        uint8_t sm = r.u8();
        g.p[i].ammoS = sm & 0x0F;
        g.p[i].ammoP = sm >> 4;
        int16_t nAim = r.i16();
        int8_t nVelY = r.i8(), nVelX = r.i8();
        uint8_t nAct = r.u8();
        uint8_t nCharge = r.u8();
        uint8_t nCursor = r.u8();
        uint8_t nMenu = r.u8();
        uint8_t nHeld = r.u8();
        if (i != g.selfId) g.p[i].held = nHeld;
        bool hold = keepSelf && i == g.selfId;
        if (hold && g.p[i].alive && !g.p[i].dying) {
          if (g.phase != PHASE_CHARGING) g.p[i].charge = nCharge;
        } else {
          g.p[i].act = nAct;
          g.p[i].charge = nCharge;
          g.p[i].cursorX = nCursor;
          g.p[i].menuSel = nMenu;
        }
        int dx = (int)g.p[i].x - (int)nx;
        int dy = (int)g.p[i].y - (int)ny;
        if (hold) {
          bool gotHit = oldHp > g.p[i].hp;
          bool respawned = oldHp == 0 && g.p[i].hp > 0;
          if (gotHit || respawned || (nVelX && nVelY < 0 && g.p[i].velY >= 0 && g.p[i].velX == 0)) {
            g.p[i].x = nx;
            g.p[i].y = ny;
            g.p[i].velY = nVelY;
            g.p[i].velX = nVelX;
            g.p[i].facing = face;
          } else {
            if (nVelY == 0 && g.p[i].velY == 0 && (dy < -2 || dy > 2)) g.p[i].y = ny;
            if (dx < -48 || dx > 48 || dy < -40 || dy > 40) {
              g.p[i].x = nx;
              g.p[i].y = ny;
              g.p[i].velY = nVelY;
              g.p[i].velX = nVelX;
              g.p[i].facing = face;
            }
          }
        } else {
          sPeerX[i] = (int16_t)nx;
          sPeerY[i] = (int16_t)ny;
          g.p[i].aim = nAim;
          g.p[i].velY = nVelY;
          g.p[i].velX = nVelX;
          g.p[i].facing = face;
          if (!sPeerOn[i] || dx < -12 || dx > 12 || dy < -10 || dy > 10) {
            g.p[i].x = nx;
            g.p[i].y = ny;
          } else {
            if (dx > 3) g.p[i].x--;
            else if (dx < -3) g.p[i].x++;
            if (dy > 2) g.p[i].y--;
            else if (dy < -2) g.p[i].y++;
          }
          sPeerOn[i] = 1;
        }
      }
      {
        uint8_t livesPack = r.u8();
        if (!r.err) {
          for (int i = 0; i < MAX_PLAYERS; i++)
            g.p[i].lives = (uint8_t)(((livesPack >> (i * 2)) & 3) + 1);
        }
      }
      if (keepReady) g.p[g.selfId].ready = myReady;
      if (g.live && g.selfId < n) {
        if (g.phase != PHASE_CHARGING) {
          PlayPhase na = (PlayPhase)g.p[g.selfId].act;
          if (na != PHASE_CHARGING) {
            g.phase = na;
            g.charge = g.p[g.selfId].charge;
          }
        }
        g.cursorX = g.p[g.selfId].cursorX;
        g.menuSel = g.p[g.selfId].menuSel;
      }
      if (!g.live &&
          (turn != prevTurn ||
           ((PlayPhase)phase == PHASE_TURN_START && prevPhase != PHASE_TURN_START))) {
        if (!deathBusy() && g.msg.until <= millis()) {
          g.turnBannerUntil = millis() + TURN_BANNER_MS;
          g.turnBannerId = turn;
        }
      }
      if ((PlayPhase)phase == PHASE_TURN_START && prevPhase != PHASE_TURN_START)
        g.turnArrowUntil = millis() + TURN_ARROW_MS;
      else if (turn != prevTurn)
        g.turnArrowUntil = millis() + TURN_ARROW_MS;
      if ((PlayPhase)phase == PHASE_TURN_START && g.turnArrowUntil < millis())
        g.turnArrowUntil = millis() + TURN_ARROW_MS;
      for (int i = 0; i < MAX_PLAYERS; i++) {
        int8_t a = r.i8();
        g.dmgAmt[i] = a;
        if (a != 0) {
          bool drown = (a == (int8_t)(-100));
          bool keep = drown && g.dmgUntil[i] > millis();
          if (!keep) {
            g.dmgX[i] = g.p[i].x;
            g.dmgY[i] = drown ? (int16_t)(g.waterY - 16) : (int16_t)(g.p[i].y - 12);
          }
          uint32_t hold = drown ? DROWN_POP_MS : DMG_POP_MS;
          if (g.dmgUntil[i] < millis()) g.dmgUntil[i] = millis() + hold;
        } else {
          g.dmgUntil[i] = 0;
        }
      }
      uint8_t pt[MAX_PROJ], ppx[MAX_PROJ], ppyb[MAX_PROJ], pown[MAX_PROJ], pfuse[MAX_PROJ];
      int16_t ppvx[MAX_PROJ], ppvy[MAX_PROJ];
      bool hostAny = false;
      for (int i = 0; i < MAX_PROJ; i++) {
        pt[i] = r.u8();
        ppx[i] = r.u8();
        ppyb[i] = r.u8();
        ppvx[i] = r.i16();
        ppvy[i] = r.i16();
        pown[i] = r.u8();
        pfuse[i] = r.u8();
        if (pt[i]) hostAny = true;
      }
      for (int i = 0; i < MAX_PROJ; i++) {
        uint8_t t = pt[i];
        int16_t py = (int16_t)ppyb[i] - 64;
        uint8_t kind = t ? (uint8_t)(t - 1) : 0;
        uint16_t fuse = (kind == PRJ_PIPE) ? (uint16_t)pfuse[i] * 12 : (uint16_t)pfuse[i] * 8;
        if (t == 0) {
          bool keepShot = !hostAny && g.proj[i].active && g.proj[i].owner == g.selfId &&
                          (hostPh == PHASE_CONTROL || hostPh == PHASE_CHARGING) &&
                          g.explodeUntil < millis();
          if (g.proj[i].type == PRJ_POD) keepShot = false;
          if (keepShot) continue;
          g.proj[i].active = false;
        } else {
          g.proj[i].active = true;
          g.proj[i].type = kind;
          g.proj[i].x = ppx[i] << 8;
          g.proj[i].y = py << 8;
          g.proj[i].vx = ppvx[i];
          g.proj[i].vy = ppvy[i];
          g.proj[i].owner = pown[i];
          g.proj[i].fuseMs = fuse;
          if (kind == PRJ_PIPE && ppvx[i] == 0 && ppvy[i] == 0 && fuse)
            g.proj[i].homing = 0xFE;
          if (!g.proj[i].grace) g.proj[i].grace = 1;
        }
      }
      if (hostPh == PHASE_EXPLOSION) killProjsNear(ex, ey, er ? er : 8);
      uint8_t ht = r.u8();
      if (!(g.live && !g.isHost && g.phase == PHASE_TARGET)) {
        g.homeTarget = ht & 0x7F;
        g.homeLocked = (ht & 0x80) != 0;
      }
      for (int i = 0; i < MAX_MINES; i++) {
        uint8_t fl = r.u8();
        g.mines[i].on = (fl & 1) != 0;
        g.mines[i].kind = (fl & 4) ? 1 : 0;
        g.mines[i].owner = (fl >> 3) & 3;
        g.mines[i].x = r.u8();
        g.mines[i].y = r.u8();
        uint8_t remain = r.u8();
        if (!g.mines[i].on) g.mines[i].armAt = 0;
        else if (g.mines[i].kind == 1 && ((fl & 2) || remain == 0))
          g.mines[i].armAt = millis();
        else if (fl & 2) g.mines[i].armAt = 0;
        else g.mines[i].armAt = millis() + (uint32_t)remain * 20;
        if (g.mines[i].on && g.mines[i].kind == 1 && !(fl & 2) && remain &&
            g.mines[i].owner < MAX_PLAYERS) {
          sFireLockUntil[g.mines[i].owner] = g.mines[i].armAt;
        }
      }
      for (int i = 0; i < MAX_SENTRY; i++) {
        uint8_t fl = r.u8();
        g.sentries[i].on = (fl & 1) != 0;
        g.sentries[i].facing = (fl & 2) ? -1 : 1;
        g.sentries[i].owner = r.u8();
        g.sentries[i].x = r.u8();
        g.sentries[i].y = r.u8();
        g.sentries[i].hp = r.u8();
        g.sentries[i].shots = r.u8();
        g.sentries[i].aim = (int16_t)((r.u8() * 360) / 256);
        g.sentries[i].facing = (g.sentries[i].aim > 90 && g.sentries[i].aim < 270) ? -1 : 1;
      }
      for (int i = 0; i < MAX_PROJ; i++) {
        if (!g.proj[i].active || g.proj[i].type != PRJ_POD) continue;
        for (int s = 0; s < MAX_SENTRY; s++) {
          if (!g.sentries[s].on) continue;
          int dx = (g.proj[i].x >> 8) - (int)g.sentries[s].x;
          int dy = (g.proj[i].y >> 8) - (int)g.sentries[s].y;
          if (dx * dx + dy * dy <= 20 * 20) {
            g.proj[i].active = false;
            break;
          }
        }
      }
      if (r.err) return;
      if (crc != 0 && g.mode == MODE_PLAY && millis() - g.lastChunkReq > 8000) {
        if (crc != terrainCrc(g.terrain)) sendTerrainReq();
        g.lastChunkReq = millis();
      }
      break;
    }
    case pkt::EXPLOSION: {
      uint8_t x = r.u8(), y = r.u8(), rr = r.u8();
      if (r.err) return;
      if (!g.isHost) {
        terrainCrater(g.terrain, x, y, rr);
        g.explodeX = x; g.explodeY = y; g.explodeR = rr;
        g.explodeUntil = millis() + (rr >= 10 ? (EXPLODE_ANIM_MS * 2 + 80) : EXPLODE_ANIM_MS);
        spawnExplosionBurst(x, y, rr);
        if (rr >= DYN_R - 1) {
          for (int i = 0; i < MAX_MINES; i++) {
            if (!g.mines[i].on) continue;
            int dx = (int)g.mines[i].x - (int)x;
            int dy = (int)g.mines[i].y - (int)y;
            if (dx * dx + dy * dy <= (rr + 4) * (rr + 4)) g.mines[i].on = false;
          }
        }
        if (rr > TORCH_R) {
          killProjsNear(x, y, rr);
          applyBlastMotion(x, y, rr);
        }
      }
      break;
    }
    case pkt::TERRAIN_REQ: {
      if (!g.isHost || g.practice) break;
      if (sChunkOut >= MAP_CHUNK_N) sChunkOut = 0;
      break;
    }
    case pkt::TERRAIN_CHUNK: {
      uint8_t idx = r.u8();
      if (idx >= MAP_CHUNK_N) return;
      if (!r.raw(g.terrain.bits + idx * MAP_CHUNK_LEN, MAP_CHUNK_LEN) || r.err) return;
      break;
    }
    case pkt::HEARTBEAT: {
      sHeardAt = tickNow();
      if (!g.isHost && g.nPlayers > 0) g.p[0].lastSeen = tickNow();
      break;
    }
    case pkt::PAUSE: {
      if (g.mode != MODE_PLAY && g.mode != MODE_PAUSE && g.mode != MODE_COUNTDOWN) break;
      uint8_t on = r.u8();
      if (on) {
        g.pauseSel = 0;
        sWantPause = true;
        if (g.turnEndsAt > millis()) g.turnLeft = (uint16_t)(g.turnEndsAt - millis());
      } else {
        sWantPause = false;
        g.turnEndsAt = millis() + g.turnLeft;
      }
      g.mode = on ? MODE_PAUSE : MODE_PLAY;
      if (g.isHost) {
        g.pausedByHost = on != 0;
        if (sender != g.selfId) sendPause(on);
      }
      break;
    }
    case pkt::QUIT: {
      if (g.isHost && g.mode == MODE_LOBBY) {
        int i = (sender < g.nPlayers && sender != g.selfId) ? (int)sender
                                                           : findPlayerMac(rx.mac, rx.mac);
        if (i > 0) hostRemovePlayer(i);
        break;
      }
      if (g.isHost && (g.mode == MODE_PLAY || g.mode == MODE_PAUSE || g.mode == MODE_COUNTDOWN)) {
        if (sender < g.nPlayers) {
          g.p[sender].connected = false;
          g.p[sender].alive = false;
          g.p[sender].hp = 0;
        }
        checkWin();
        break;
      }
      if (g.isHost && (g.mode == MODE_REMATCH || g.mode == MODE_GAMEOVER)) {
        if (sender < g.nPlayers && sender != g.selfId) {
          g.p[sender].connected = false;
          g.p[sender].present = false;
          g.p[sender].ready = false;
        }
        int still = 0;
        for (int i = 0; i < g.nPlayers; i++)
          if (g.p[i].present && g.p[i].connected) still++;
        if (still < 2) sGoMpMenu = true;
        break;
      }
      if (!g.isHost && !g.practice && inSessionMode()) {
        bool macHost = g.nPlayers > 0 && !macZero(rx.mac) &&
                       (macEq(g.p[0].mac, rx.mac) ||
                        (!macZero(g.p[0].rxMac) && macEq(g.p[0].rxMac, rx.mac)));
        bool sessOk = (session == 0 || g.session == 0 || session == g.session);
        if (macHost && sessOk) clientHostGone();
      }
      break;
    }
    case pkt::REMATCH: {
      uint8_t on = 1;
      if (r.left() >= 1) on = r.u8();
      if (sender < g.nPlayers) g.p[sender].ready = on != 0;
      if (g.isHost) {
        bool all = g.nPlayers >= 2;
        for (int i = 0; i < g.nPlayers; i++) if (!g.p[i].ready) all = false;
        if (all) hostBeginGame();
      }
      break;
    }
    case pkt::EVENT: {
      uint8_t kind = r.u8(), pid = r.u8();
      if (r.err) return;
      if (kind == EVT_HOST_LOST) {
        clientHostGone();
        break;
      } else if (kind == EVT_DROWN && pid < MAX_PLAYERS) {
        if (g.isHost || g.practice) break;
        if (pid < MAX_PLAYERS && sDrownMsgUntil[pid] && millis() < sDrownMsgUntil[pid]) break;
        char who[8];
        snprintf(who, sizeof(who), "P%u", (unsigned)(pid + 1));
        if (pid < g.nPlayers) {
          hitWater(g.p[pid]);
          g.p[pid].hp = 0;
          g.p[pid].velX = 0;
          g.p[pid].velY = 0;
          showDrownDamage(pid);
          g.dmgHangUntil = millis() + DROWN_MSG_DELAY_MS + MSG_MS;
        }
        if (pid < MAX_PLAYERS)
          sDrownMsgUntil[pid] = millis() + DROWN_MSG_DELAY_MS + MSG_MS + 400;
        scheduleMsg(who, "DROWNED", DROWN_MSG_DELAY_MS, MSG_MS);
        focusDeathCam(pid, DROWN_MSG_DELAY_MS + MSG_MS + 400);
        g.deathHold = true;
        if (g.mode == MODE_REMATCH) {
          g.mode = MODE_GAMEOVER;
          g.phaseAt = millis();
        }
      } else if (kind == EVT_KILL && pid < MAX_PLAYERS) {
        char who[8];
        snprintf(who, sizeof(who), "P%u", (unsigned)(pid + 1));
        setMsg(who, "KILLED", MSG_MS);
        focusDeathCam(pid, MSG_MS + 400);
        if (g.mode == MODE_REMATCH) {
          g.mode = MODE_GAMEOVER;
          g.phaseAt = millis();
        }
      } else if (kind == EVT_WIN) {
        g.winner = pid;
        if (g.mode != MODE_REMATCH || g.msg.until > millis()) {
          g.mode = MODE_GAMEOVER;
          g.phaseAt = millis();
        }
      } else if (kind == EVT_SUDDEN) {
        if (g.mode == MODE_PLAY && (millis() - g.matchAt) > 2000)
          setMsg("SUDDEN", "DEATH", 1000);
      }
      break;
    }
    default:
      break;
  }
}

static void localUi() {
  static bool pauseLatch = false;
  if (g.mode == MODE_PLAY && btnHeld(BTN_WEAPON) > PAUSE_HOLD_MS && !pauseLatch) {
    pauseLatch = true;
    g.pauseSel = 0;
    if (g.turnEndsAt > millis()) g.turnLeft = (uint16_t)(g.turnEndsAt - millis());
    if (g.isHost || g.practice) {
      g.mode = MODE_PAUSE;
      g.pausedByHost = true;
      sWantPause = true;
      if (!g.practice) sendPause(1);
    } else {
      sWantPause = true;
      g.mode = MODE_PAUSE;
      sendPause(1);
    }
  }
  if (!btnDown(BTN_WEAPON)) pauseLatch = false;

  if (g.mode == MODE_PAUSE) {
    if (btnPressed(BTN_AIM_UP) || btnPressed(BTN_AIM_DN))
      g.pauseSel = (uint8_t)(g.pauseSel ^ 1);
    if (btnPressed(BTN_WEAPON) && !pauseLatch) {
      sWantPause = false;
      if (!g.practice) sendQuit();
      returnToMenu();
    } else if (btnPressed(BTN_FIRE) && btnHeld(BTN_WEAPON) == 0) {
      if (g.pauseSel == 0) {
        if (g.isHost || g.practice) {
          g.mode = MODE_PLAY;
          g.pausedByHost = false;
          sWantPause = false;
          g.turnEndsAt = millis() + g.turnLeft;
          if (!g.practice) sendPause(0);
        } else {
          sWantPause = false;
          g.mode = MODE_PLAY;
          g.turnEndsAt = millis() + g.turnLeft;
          sendPause(0);
        }
      } else {
        sWantPause = false;
        if (!g.practice) sendQuit();
        returnToMenu();
      }
    }
  }
}

void gameBegin() {
  memset(&g, 0, sizeof(g));
  g.mode = MODE_INTRO;
  g.selfId = PLAYER_NONE;
  g.bootAt = millis();
  g.phaseAt = millis();
  g.winner = PLAYER_NONE;
  g.zoom = 1;
  g.mapKind = MAP_ISLAND;
  g.turnOpt = 1;
  g.live = false;
  g.cheat = false;
  g.livesOpt = 3;
  g.turnMs = turnMsFor(g.turnOpt);
  buttonsBegin();
  displayBegin();
  netBegin();
  netGetMac(g.selfMac);
  lastMs = millis();
  Serial.printf("WARTOADS %s  heap=%u  oled=%s addr=0x%02X\n", WM_VERSION_STR,
                (unsigned)ESP.getFreeHeap(), displayOk() ? "ok" : "MISSING", displayAddr());
  Serial.printf("pins UP=%d DN=%d OK=%d BACK=%d L=%d R=%d\n",
                PIN_BTN_AIM_UP, PIN_BTN_AIM_DN, PIN_BTN_FIRE, PIN_BTN_WEAPON,
                PIN_BTN_LEFT, PIN_BTN_RIGHT);
}

void gameLoop() {
  uint32_t now = millis();
  sTickNow = now;
  uint32_t dt = now - lastMs;
  if (dt > 80) dt = 80;
  lastMs = now;
  tickPendMsg(now);

  mirrorPoll();
  buttonsPoll();
  netTick();
  RxPkt rx;
  RxPkt latestSnap;
  bool haveSnap = false;
  while (netPop(rx)) {
    uint8_t t = (rx.len > 3) ? rx.data[3] : 0;
    if (t == pkt::SNAPSHOT) {
        if (pkt::verify(rx.data, rx.len)) {
          uint16_t nseq = (rx.len >= 11)
                              ? (uint16_t)(rx.data[9] | ((uint16_t)rx.data[10] << 8))
                              : 0;
          bool newer = true;
          if (haveSnap && latestSnap.len >= 11) {
            uint16_t oseq =
                (uint16_t)(latestSnap.data[9] | ((uint16_t)latestSnap.data[10] << 8));
            newer = (int16_t)(nseq - oseq) >= 0;
          }
          if (newer) latestSnap = rx;
          haveSnap = true;
          sHeardAt = now;
          if (g.nPlayers > 0) g.p[0].lastSeen = now;
        }
    } else {
      handlePkt(rx);
    }
  }
  if (haveSnap) handlePkt(latestSnap);
  if (sGoMpMenu) {
    sGoMpMenu = false;
    beginMpMenu();
  }
  if (!g.isHost && !g.practice && g.session != 0 && sHeardAt != 0) {
    GameMode m = g.mode;
    if (m != MODE_LOBBY &&
        (m == MODE_COUNTDOWN || m == MODE_PLAY || m == MODE_PAUSE ||
         m == MODE_GAMEOVER || m == MODE_REMATCH) &&
        aged(now, sHeardAt, HOST_LOST_MS)) {
      clientHostGone();
    }
  }

  switch (g.mode) {
    case MODE_INTRO:
      if (btnPressed(BTN_FIRE) || btnPressed(BTN_WEAPON) || (now - g.phaseAt) >= INTRO_MS)
        g.mode = MODE_TITLE;
      break;
    case MODE_TITLE:
      if (btnPressed(BTN_FIRE)) g.mode = MODE_MAIN_MENU;
      break;
    case MODE_CREDITS:
      if (btnPressed(BTN_FIRE) || btnPressed(BTN_WEAPON)) g.mode = MODE_MAIN_MENU;
      break;
    case MODE_MAIN_MENU:
      if (btnPressed(BTN_WEAPON)) {
        returnToTitle();
        break;
      }
      if (btnPressed(BTN_AIM_UP) || btnPressed(BTN_AIM_DN)) {
        int d = btnPressed(BTN_AIM_UP) ? -1 : 1;
        g.titleSel = (uint8_t)((g.titleSel + TITLE_ROWS + d) % TITLE_ROWS);
      }
      if (btnPressed(BTN_FIRE)) {
        if (g.titleSel == 0) beginPracticeSetup();
        else if (g.titleSel == 1) beginMpMenu();
        else if (g.titleSel == 2) g.mode = MODE_BTN_TEST;
        else if (g.titleSel == 3) beginBtnCfg();
        else g.mode = MODE_CREDITS;
      }
      break;
    case MODE_SETUP:
      if (btnPressed(BTN_WEAPON)) {
        if (g.setupFromRematch) {
          g.setupFromRematch = false;
          g.mode = MODE_REMATCH;
          g.lobbySel = 0;
          if (g.isHost) {
            sendSnapshot();
            sendHeartbeat();
            g.lastHb = now;
          }
        } else if (g.setupHost) beginMpMenu();
        else returnToMenu();
        break;
      }
      if (btnPressed(BTN_AIM_UP) || btnPressed(BTN_AIM_DN)) {
        int d = btnPressed(BTN_AIM_UP) ? -1 : 1;
        g.lobbySel = (uint8_t)((g.lobbySel + setupRowN() + d) % setupRowN());
      }
      if (btnPressed(BTN_LEFT) || btnRepeat(BTN_LEFT)) cycleSetup(-1);
      if (btnPressed(BTN_RIGHT) || btnRepeat(BTN_RIGHT)) cycleSetup(1);
      if (btnPressed(BTN_FIRE)) {
        if (g.lobbySel == (uint8_t)(setupRowN() - 1)) {
          if (g.setupFromRematch) {
            g.setupFromRematch = false;
            g.mode = MODE_REMATCH;
            g.lobbySel = 0;
            for (int i = 0; i < g.nPlayers; i++) g.p[i].ready = false;
            if (g.isHost) {
              sendSnapshot();
              sendHeartbeat();
              g.lastHb = now;
            }
          } else if (g.setupHost) beginHostLobby();
          else beginPractice();
        } else {
          cycleSetup(1);
        }
      }
      if (g.isHost && g.setupFromRematch && g.session) {
        if (now - g.lastSnapSend > 400) sendSnapshot();
        if (now - g.lastHb > 600) {
          sendHeartbeat();
          g.lastHb = now;
        }
      }
      break;
    case MODE_BTN_CFG: {
      if (g.cfgStep == 0xFF) {
        static uint16_t cfgHold = 0;
        if (btnPressed(BTN_WEAPON)) {
          cfgHold = 0;
          returnToMenu();
          break;
        }
        if (btnDown(BTN_FIRE)) cfgHold = btnHeld(BTN_FIRE);
        if (btnHeld(BTN_FIRE) > 1200) {
          buttonsResetMap();
          setMsg("BUTTONS", "DEFAULT", 1000);
          g.cfgStep = 6;
          cfgHold = 0;
          break;
        }
        if (btnReleased(BTN_FIRE) && cfgHold >= 30 && cfgHold < 1200) {
          g.cfgStep = 0;
          sCfgWaitRelease = true;
          memset(sCfgPhys, 0xFF, sizeof(sCfgPhys));
          cfgHold = 0;
        }
        break;
      }
      if (g.cfgStep >= BTN_COUNT) {
        if (btnPressed(BTN_FIRE) || btnPressed(BTN_WEAPON)) returnToMenu();
        break;
      }
      if (btnPressed(BTN_WEAPON) && CFG_ORDER[g.cfgStep] != BTN_WEAPON) {
        returnToMenu();
        break;
      }
      if (sCfgWaitRelease) {
        if (!buttonsAnyRawDown()) sCfgWaitRelease = false;
        break;
      }
      int phys = buttonsRawPressed();
      if (phys < 0) break;
      bool used = false;
      for (uint8_t i = 0; i < g.cfgStep; i++) if (sCfgPhys[i] == (uint8_t)phys) used = true;
      if (used) {
        setMsg("ALREADY", "USED", 700);
        break;
      }
      sCfgPhys[g.cfgStep] = (uint8_t)phys;
      sCfgWaitRelease = true;
      g.cfgStep++;
      if (g.cfgStep >= BTN_COUNT) {
        uint8_t map[BTN_COUNT];
        for (int i = 0; i < BTN_COUNT; i++) map[CFG_ORDER[i]] = sCfgPhys[i];
        buttonsApplyMap(map);
        setMsg("BUTTONS", "SAVED", 900);
      }
      break;
    }
    case MODE_BTN_TEST:
      if (btnPressed(BTN_WEAPON) && btnHeld(BTN_FIRE) == 0) returnToMenu();
      break;
    case MODE_MP_MENU:
      if (btnPressed(BTN_WEAPON)) {
        returnToMenu();
        break;
      }
      if (btnPressed(BTN_AIM_UP) || btnPressed(BTN_AIM_DN)) {
        int d = btnPressed(BTN_AIM_UP) ? -1 : 1;
        g.mpSel = (uint8_t)((g.mpSel + MP_MENU_ROWS + d) % MP_MENU_ROWS);
      }
      if (btnPressed(BTN_FIRE)) {
        if (g.mpSel == 0) beginHostSetup();
        else beginSeek(false);
      }
      break;
    case MODE_SEEK:
      if (g.joining && now - g.lastHb > 280) {
        sendJoin(g.joinMac);
        g.lastHb = now;
      }
      if (btnPressed(BTN_WEAPON)) {
        if (g.joining) {
          sendQuit();
          sendQuit();
        }
        beginMpMenu();
        break;
      }
      if (g.nFound > 0 && (btnPressed(BTN_AIM_UP) || btnPressed(BTN_AIM_DN))) {
        int d = btnPressed(BTN_AIM_UP) ? -1 : 1;
        g.seekSel = (uint8_t)((g.seekSel + g.nFound + d) % g.nFound);
      }
      if (g.joining && now - g.phaseAt > 8000) {
        sendQuit();
        sendQuit();
        g.joining = false;
        memset(g.joinMac, 0, 6);
        g.joinSession = 0;
      }
      if (btnPressed(BTN_FIRE) && g.nFound > 0 && !g.joining) {
        startJoining(g.seekSel);
      }
      break;
    case MODE_DISCOVERY:
      break;
    case MODE_LOBBY:
      if (g.isHost) {
        g.p[0].lastSeen = now;
        hostDropStale(now);
      }
      if (now - g.lastHb > 500) {
        if (g.isHost) {
          sendLobby();
          static uint8_t discoSkip;
          if ((++discoSkip & 1) == 0) sendDiscovery();
        } else {
          sendHeartbeat();
        }
        g.lastHb = now;
      }
      if (g.lobbyQuitAsk) {
        if (btnPressed(BTN_AIM_UP) || btnPressed(BTN_AIM_DN))
          g.pauseSel = (uint8_t)(g.pauseSel ^ 1);
        if (btnPressed(BTN_WEAPON) && !btnDown(BTN_FIRE)) {
          g.lobbyQuitAsk = false;
          break;
        }
        if (btnPressed(BTN_FIRE) && !btnDown(BTN_WEAPON)) {
          if (g.pauseSel == 0) {
            sAllowLeaveLobby = true;
            sendQuit();
            sendQuit();
            beginMpMenu();
          } else {
            g.lobbyQuitAsk = false;
          }
        }
        break;
      }
      if (btnPressed(BTN_WEAPON) && !btnPressed(BTN_FIRE) && !btnDown(BTN_FIRE)) {
        g.lobbyQuitAsk = true;
        g.pauseSel = 1;
        break;
      }
      if (g.isHost && (btnPressed(BTN_AIM_UP) || btnPressed(BTN_AIM_DN))) {
        g.lobbySel = (uint8_t)(g.lobbySel ^ 1);
      }
      if (btnPressed(BTN_FIRE) && !btnDown(BTN_WEAPON)) {
        if (g.selfId >= MAX_PLAYERS) {
          for (int i = 0; i < g.nPlayers; i++)
            if (macEq(g.p[i].mac, g.selfMac)) g.selfId = (uint8_t)i;
        }
        if (g.selfId >= MAX_PLAYERS) break;
        if (g.isHost && g.lobbySel == 1) {
          hostTryStart();
        } else {
          g.p[g.selfId].ready = !g.p[g.selfId].ready;
          if (g.isHost) sendLobby();
          else sendReady();
        }
      }
      break;
    case MODE_GROUP_FULL:
      if (btnPressed(BTN_WEAPON)) {
        beginMpMenu();
        break;
      }
      if (btnPressed(BTN_FIRE)) beginSeek(false);
      break;
    case MODE_HOST_LOST:
      if (!aged(now, g.phaseAt, 400)) break;
      if (btnPressed(BTN_FIRE) || btnPressed(BTN_WEAPON) || aged(now, g.phaseAt, 5000))
        beginMpMenu();
      break;
    case MODE_COUNTDOWN:
    case MODE_PLAY:
      if (g.mode == MODE_COUNTDOWN && !g.isHost && !g.practice && btnPressed(BTN_WEAPON)) {
        sendQuit();
        beginMpMenu();
        break;
      }
      if (g.mode == MODE_COUNTDOWN && now - g.phaseAt >= COUNTDOWN_MS) {
        if (g.isHost || g.practice) {
          g.mode = MODE_PLAY;
          g.phaseAt = now;
          if (livePlay()) startLivePlay();
          else beginTurn();
        }
      }
      if (g.mode == MODE_COUNTDOWN && g.isHost && !g.practice) {
        if ((now - g.phaseAt) < 1800 && (now - sLastStartTx) > 700) {
          sendStart();
          sLastStartTx = now;
        }
        if (now - g.lastSnapSend > 200) sendSnapshot();
      }
      pollGestures();
      if (!g.practice && g.isHost && now - g.lastHb > HEARTBEAT_MS) {
        if ((now - g.lastSnapSend) >= 500) sendHeartbeat();
        g.lastHb = now;
      }
      if (!g.practice && !g.isHost && g.mode == MODE_PLAY && now >= sQuietUntil &&
          (livePlay() ||
           (g.turn == g.selfId &&
            (g.phase == PHASE_CONTROL || g.phase == PHASE_WEAPON_MENU || g.phase == PHASE_TARGET ||
             g.phase == PHASE_CHARGING || g.phase == PHASE_BLOWTORCH || g.phase == PHASE_MOLLI ||
             g.phase == PHASE_BAT || g.phase == PHASE_JETPACK || g.phase == PHASE_DRACO)))) {
        bool edge = false;
        for (int i = 0; i < BTN_COUNT; i++) {
          if (btnPressed(i) || btnReleased(i)) edge = true;
        }
        if (sPendingAct) edge = true;
        bool moving = btnDown(BTN_LEFT) || btnDown(BTN_RIGHT) ||
                      btnDown(BTN_AIM_UP) || btnDown(BTN_AIM_DN);
        Player* me = self();
        bool air = me && (me->velX || me->velY || !terrainGrounded(g.terrain, me->x, me->y));
        bool busy = moving || air || g.phase == PHASE_JETPACK || g.phase == PHASE_CHARGING ||
                    g.phase == PHASE_DRACO || g.phase == PHASE_WEAPON_MENU ||
                    g.phase == PHASE_TARGET || g.phase == PHASE_BLOWTORCH;
        uint32_t gap = busy ? 25 : 160;
        if (edge || now - g.lastInputSend > gap) {
          sendInput();
          g.lastInputSend = now;
        }
      }
      if (g.isHost || g.practice) hostSim(now, dt);
      else clientPredict(dt);
      localUi();
      break;
    case MODE_PAUSE:
      pollGestures();
      localUi();
      if (g.isHost && now - g.lastSnapSend > 400) sendSnapshot();
      break;
    case MODE_GAMEOVER:
      if ((!matchMsgsPending(now) && now - g.phaseAt > 1800) || now - g.phaseAt > 5500) {
        for (int i = 0; i < MAX_PLAYERS; i++) g.p[i].ready = false;
        g.lobbySel = 0;
        memset(&g.msg, 0, sizeof(g.msg));
        clearPendMsg();
        g.mode = MODE_REMATCH;
      }
      if (g.isHost && now - g.lastSnapSend > 400) sendSnapshot();
      break;
    case MODE_REMATCH:
      if (g.practice) {
        if (btnPressed(BTN_WEAPON) || (btnPressed(BTN_FIRE) && g.lobbySel == 2)) {
          returnToMenu();
          break;
        }
        if (btnPressed(BTN_AIM_UP) || btnPressed(BTN_AIM_DN)) {
          int d = btnPressed(BTN_AIM_UP) ? -1 : 1;
          g.lobbySel = (uint8_t)((g.lobbySel + 3 + d) % 3);
        }
        if (btnPressed(BTN_FIRE)) {
          if (g.lobbySel == 0) {
            g.mapSeed = esp_random();
            g.gameSeed = esp_random();
            startMatchCommon();
            g.mode = MODE_PLAY;
          } else if (g.lobbySel == 1) {
            g.mode = MODE_SETUP;
            g.setupHost = false;
            g.setupFromRematch = true;
            g.lobbySel = 0;
          }
        }
        break;
      }
      if (btnPressed(BTN_WEAPON)) {
        sendQuit();
        beginMpMenu();
        break;
      }
      if (g.isHost && (btnPressed(BTN_AIM_UP) || btnPressed(BTN_AIM_DN))) {
        g.lobbySel = (uint8_t)(g.lobbySel ^ 1);
      }
      if (btnPressed(BTN_FIRE) && g.selfId < MAX_PLAYERS) {
        if (g.isHost && g.lobbySel == 1) {
          for (int i = 0; i < g.nPlayers; i++) g.p[i].ready = false;
          g.mode = MODE_SETUP;
          g.setupHost = true;
          g.setupFromRematch = true;
          g.lobbySel = 0;
          sendSnapshot();
          sendHeartbeat();
          g.lastHb = now;
          break;
        }
        g.p[g.selfId].ready = !g.p[g.selfId].ready;
        sendRematch();
        if (g.isHost) {
          bool all = g.nPlayers >= 2;
          for (int i = 0; i < g.nPlayers; i++) if (!g.p[i].ready) all = false;
          if (all) hostBeginGame();
        }
      }
      if (g.isHost && now - g.lastSnapSend > 400) sendSnapshot();
      if (now - g.lastHb > 600) { sendHeartbeat(); g.lastHb = now; }
      break;
    default:
      break;
  }

  tickParticles();
  if (!g.practice && g.session != 0 && g.isHost && sChunkOut < MAP_CHUNK_N &&
      (now - sLastChunkTx) >= 150) {
    sendTerrainChunk(sChunkOut++);
    sLastChunkTx = now;
  }
  static uint32_t lastDraw;
  if (now - lastDraw >= 33) {
    lastDraw = now;
    renderFrame();
  }

  yield();
}
