#pragma once
#include "game.h"

void renderFrame();
void spawnParticles(int x, int y, int n);
void spawnSplash(int x, int y);
void spawnJetPuff(int x, int y, int dir);
void spawnSparks(int x, int y, int n);
void spawnMuzzle(int x, int y);
void spawnExplosionBurst(int x, int y, int r);
void spawnBlood(int x, int y, int dir);
void noteFireKick(uint8_t id);
void notePlaceAnim(uint8_t id, uint8_t kind);
void noteThrowAnim(uint8_t id);
void tickParticles();
