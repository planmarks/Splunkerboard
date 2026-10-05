#pragma once
#include "config.h"
#include "terrain.h"

struct Player {
  uint8_t mac[6];
  uint8_t rxMac[6];
  bool present;
  bool ready;
  bool alive;
  bool connected;
  uint8_t hp;
  int16_t x, y;
  int8_t facing;     // -1 left +1 right
  int16_t aim;       // 0..359 deg, 0=right 90=up 180=left 270=down
  int8_t velY;
  int8_t velX;
  uint8_t jumpsLeft;
  uint8_t weapon;
  uint8_t ammoG, ammoA, ammoT, ammoB, ammoM, ammoBat, ammoJ, ammoH, ammoD, ammoS, ammoP, ammoDyn, ammoMo;
  uint8_t lives;
  uint32_t lastSeen;
  uint16_t lastSeq;
  bool seqInit;
  uint8_t knockFrames;
  int8_t knockDir;
  uint8_t act;
  uint8_t charge;
  uint8_t cursorX;
  uint8_t menuSel;
  uint32_t actAt;
  uint8_t held;
  bool dying;
  bool bursting;
  bool jetting;
  uint32_t dyingAt;
  uint32_t burstAt;
};

struct Proj {
  bool active;
  uint8_t type;
  uint8_t owner;
  int32_t x, y, vx, vy; // 8.8 fixed
  uint16_t fuseMs;
  uint8_t grace;
  uint8_t homing;
};

struct Particle {
  bool on;
  int16_t x, y, vx, vy;
  uint8_t life;
  uint8_t splash;
};

struct Mine {
  bool on;
  uint8_t owner;
  uint8_t kind; // 0 mine, 1 dynamite
  uint8_t x, y;
  uint32_t armAt;
};

struct Sentry {
  bool on;
  uint8_t owner;
  uint8_t x, y;
  uint8_t hp;
  uint8_t shots;
  int8_t facing;
  int16_t aim; // 0=right, 90=up; never swings through the base
  uint8_t lock;
  uint8_t turnShots;
  uint32_t armAt;
  uint32_t lastShotAt;
  uint32_t turnReadyAt;
};

struct FoundHost {
  uint8_t mac[6];
  uint32_t session;
  uint8_t nPlayers;
  uint8_t mapKind;
  uint8_t live;
  uint32_t seen;
};

struct OverlayMsg {
  char line1[16];
  char line2[16];
  uint32_t until;
  uint8_t subtle;
};

struct Match {
  GameMode mode;
  PlayPhase phase;
  bool isHost;
  bool practice;
  uint8_t selfId;
  uint8_t nPlayers;
  uint8_t turn;
  uint8_t winner;
  uint32_t session;
  uint32_t mapSeed;
  uint32_t gameSeed;
  int8_t wind;
  uint16_t turnLeft;
  uint8_t charge;
  uint8_t cursorX;
  uint8_t menuSel;
  uint8_t waterY;
  uint8_t roundN;
  uint8_t turnsThisRound;
  uint32_t phaseAt;
  uint32_t turnBannerUntil;
  uint8_t turnBannerId;
  uint32_t lastSnapSend;
  uint32_t lastInputSend;
  uint32_t lastHb;
  uint32_t lastDiscoCheck;
  uint16_t txSeq;
  uint16_t hostSeqSeen;
  bool hostSeqInit;
  bool pausedByHost;
  bool sudden;
  uint8_t explodeX, explodeY, explodeR;
  uint32_t explodeUntil;
  uint8_t recoilFrames;
  OverlayMsg msg;
  Player p[MAX_PLAYERS];
  Proj proj[MAX_PROJ];
  Particle part[MAX_PARTICLES];
  Terrain terrain;
  uint8_t selfMac[6];
  uint32_t bootAt;
  uint32_t titleBlink;
  uint8_t terrainWantChunk;
  uint32_t lastChunkReq;
  uint8_t zoom;          // 1..3, local view only
  uint8_t heldButtons;
  uint32_t turnEndsAt;
  uint8_t mapKind;
  uint8_t turnOpt;
  uint16_t turnMs;
  uint8_t lobbySel;
  uint8_t molliHit;
  uint8_t pauseSel;
  bool deathHold;
  uint32_t turnArrowUntil;
  int8_t dmgAmt[MAX_PLAYERS];
  int16_t dmgX[MAX_PLAYERS], dmgY[MAX_PLAYERS];
  uint32_t dmgUntil[MAX_PLAYERS];
  uint32_t dmgHangUntil;
  uint8_t homeTarget;
  bool homeLocked;
  Mine mines[MAX_MINES];
  Sentry sentries[MAX_SENTRY];
  bool live;
  uint32_t matchAt;
  uint32_t liveRiseAt;
  uint8_t mpSel;
  uint8_t titleSel;
  uint8_t cfgStep;
  bool setupHost;
  uint8_t seekSel;
  uint8_t nFound;
  FoundHost found[MAX_FOUND];
  bool joining;
  bool autoJoin;
  uint8_t joinMac[6];
  uint32_t joinSession;
  uint16_t pktOk;
  uint8_t lobbyWarn;
  uint32_t lobbyWarnUntil;
  bool lobbyQuitAsk;
  bool setupFromRematch;
  bool cheat;
  uint8_t livesOpt;
  uint8_t camFocus;
  uint32_t camFocusUntil;
  int16_t camFocusX, camFocusY;
};

void gameBegin();
void gameLoop();
const Match& gameMatch();
uint32_t gameFreeHeap();
