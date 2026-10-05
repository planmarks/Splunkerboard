#pragma once
#include "config.h"

struct Terrain {
  uint8_t bits[TERRAIN_BYTES];
  uint8_t waterY;
};

void terrainClear(Terrain& t);
void terrainSet(Terrain& t, int x, int y, bool solid);
bool terrainSolid(const Terrain& t, int x, int y);
void terrainGenerate(Terrain& t, uint32_t seed, uint8_t kind);
void terrainCrater(Terrain& t, int cx, int cy, int r);
int terrainStandY(const Terrain& t, int x);
bool terrainGrounded(const Terrain& t, int x, int y);
uint16_t terrainCrc(const Terrain& t);
void terrainCopy(Terrain& dst, const Terrain& src);
