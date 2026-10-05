#include "terrain.h"

void terrainClear(Terrain& t) {
  memset(t.bits, 0, TERRAIN_BYTES);
  t.waterY = (uint8_t)(WORLD_H - WATER_H_DEFAULT);
}

void terrainSet(Terrain& t, int x, int y, bool solid) {
  if ((unsigned)x >= WORLD_W || (unsigned)y >= WORLD_H) return;
  uint8_t* p = &t.bits[x + (y >> 3) * WORLD_W];
  uint8_t m = (uint8_t)(1 << (y & 7));
  if (solid) *p |= m;
  else *p &= (uint8_t)~m;
}

bool terrainSolid(const Terrain& t, int x, int y) {
  if (x < 0 || x >= WORLD_W) return true;
  if (y < 0) return false;
  if (y >= WORLD_H) return false;
  return (t.bits[x + (y >> 3) * WORLD_W] & (1 << (y & 7))) != 0;
}

static void fillCol(Terrain& t, int x, int y0, int y1) {
  if (y0 > y1) return;
  for (int y = y0; y < y1 && y < t.waterY; y++) terrainSet(t, x, y, true);
}

static void smooth(int16_t* h, int n) {
  int16_t tmp[WORLD_W];
  tmp[0] = h[0];
  tmp[n - 1] = h[n - 1];
  for (int x = 1; x < n - 1; x++) tmp[x] = (int16_t)((h[x - 1] + h[x] * 2 + h[x + 1]) / 4);
  memcpy(h, tmp, (size_t)n * sizeof(int16_t));
}

static void stampRing(Terrain& t, int cx, int cy, int r) {
  int r2 = r * r;
  int inner = (r > 1) ? (r - 1) * (r - 1) : 0;
  for (int dy = -r; dy <= r; dy++) {
    for (int dx = -r; dx <= r; dx++) {
      int d = dx * dx + dy * dy;
      if (d > r2 || d < inner) continue;
      int y = cy + dy;
      if (y >= t.waterY) continue;
      terrainSet(t, cx + dx, y, true);
    }
  }
}

static void stampTree(Terrain& t, int x, int gy) {
  if (gy < 18 || gy >= t.waterY - 2) return;
  int h = 8 + (x * 3 + gy) % 5;
  if (gy - h < WORLD_TOP + 6) h = gy - (WORLD_TOP + 6);
  if (h < 6) return;
  for (int i = 0; i < h; i++) {
    terrainSet(t, x, gy - 1 - i, true);
    terrainSet(t, x + 1, gy - 1 - i, true);
  }
  int top = gy - h;
  stampRing(t, x, top, 4);
  stampRing(t, x, top - 1, 3);
  terrainSet(t, x, top, true);
  if ((x + gy) & 1) terrainSet(t, x + 1, top + 1, true);
  else terrainSet(t, x - 1, top + 1, true);
}

static void stampBush(Terrain& t, int x, int gy) {
  if (gy < 10 || gy >= t.waterY) return;
  stampRing(t, x, gy - 2, 2);
  terrainSet(t, x, gy - 1, true);
  terrainSet(t, x - 1, gy - 1, true);
  terrainSet(t, x + 1, gy - 1, true);
}

static void stampRock(Terrain& t, int x, int gy) {
  if (gy < 8 || gy >= t.waterY) return;
  terrainSet(t, x, gy - 1, true);
  terrainSet(t, x - 1, gy - 1, true);
  terrainSet(t, x + 1, gy - 1, true);
  terrainSet(t, x, gy - 2, true);
  terrainSet(t, x - 2, gy, true);
  terrainSet(t, x + 2, gy, true);
}

static void stampPillar(Terrain& t, int x, int y0, int y1) {
  if (y0 > y1) return;
  for (int y = y0; y <= y1 && y < t.waterY; y++) {
    terrainSet(t, x, y, true);
    terrainSet(t, x + 1, y, true);
  }
}

static int surfaceY(const Terrain& t, int x) {
  for (int y = t.waterY - 2; y > WORLD_TOP + 4; y--) {
    if (!terrainSolid(t, x, y)) continue;
    if (terrainSolid(t, x, y - 1) || terrainSolid(t, x, y - 2)) continue;
    int depth = 0;
    for (int k = 0; k < 6 && y + k < t.waterY; k++) {
      if (terrainSolid(t, x, y + k)) depth++;
    }
    if (depth < 4) continue;
    if (!terrainSolid(t, x - 1, y) && !terrainSolid(t, x + 1, y) &&
        !terrainSolid(t, x - 1, y + 1) && !terrainSolid(t, x + 1, y + 1))
      continue;
    return y;
  }
  return -1;
}

static void stampSurfaceDecor(Terrain& t, Rng& rng, bool trees) {
  int nBush = rng.range(4, 8);
  for (int i = 0; i < nBush; i++) {
    int x = rng.range(10, WORLD_W - 12);
    int gy = surfaceY(t, x);
    if (gy < 0 || gy > t.waterY - 12) continue;
    stampBush(t, x, gy);
  }
  int nRock = rng.range(3, 6);
  for (int i = 0; i < nRock; i++) {
    int x = rng.range(12, WORLD_W - 14);
    int gy = surfaceY(t, x);
    if (gy < 0 || gy > t.waterY - 12) continue;
    stampRock(t, x, gy);
  }
  if (!trees) return;
  int nTree = rng.range(4, 7);
  int last = -40;
  for (int i = 0; i < nTree; i++) {
    int x = rng.range(16, WORLD_W - 18);
    if (x > last - 18 && x < last + 18) continue;
    int gy = surfaceY(t, x);
    if (gy < 0 || gy > t.waterY - 14) continue;
    stampTree(t, x, gy);
    last = x;
  }
}

static void addHill(int16_t* h, int cx, int hw, int peak, int waterY, Rng& rng, bool plateau) {
  int plat = plateau ? (hw * rng.range(2, 5) / 10) : 0;
  if (plat < 8) plat = 0;
  for (int x = 0; x < WORLD_W; x++) {
    int d = x - cx;
    if (d < 0) d = -d;
    if (d >= hw) continue;
    int rise;
    if (plat && d <= plat) rise = peak;
    else if (plat) rise = (peak * (hw - d)) / (hw - plat);
    else {
      int t01 = hw - d;
      rise = (peak * t01 * t01) / (hw * hw);
    }
    int top = waterY - rise + rng.range(-2, 3);
    if (top < h[x]) h[x] = (int16_t)top;
  }
}

static void carveCove(int16_t* h, int x0, int w, int waterY, int depth) {
  for (int x = x0; x < x0 + w && x < WORLD_W; x++) {
    if (x < 0) continue;
    int edge = x - x0;
    if (edge > w - 1 - edge) edge = w - 1 - edge;
    int cut = depth;
    if (edge < 5) cut = depth * edge / 5;
    int ny = h[x] + cut;
    if (ny > waterY) ny = waterY;
    h[x] = (int16_t)ny;
  }
}

static void genIsland(Terrain& t, Rng& rng) {
  int16_t h[WORLD_W];
  for (int x = 0; x < WORLD_W; x++) h[x] = t.waterY;
  int style = rng.range(0, 4);
  if (style == 0) {
    addHill(h, rng.range(100, 156), rng.range(88, 120), rng.range(50, 72), t.waterY, rng, false);
  } else if (style == 1) {
    addHill(h, rng.range(90, 130), rng.range(70, 100), rng.range(48, 70), t.waterY, rng, true);
    addHill(h, rng.range(140, 190), rng.range(40, 70), rng.range(28, 48), t.waterY, rng, false);
  } else if (style == 2) {
    addHill(h, rng.range(70, 110), rng.range(50, 72), rng.range(36, 56), t.waterY, rng, true);
    addHill(h, rng.range(118, 148), rng.range(36, 55), rng.range(44, 68), t.waterY, rng, false);
    addHill(h, rng.range(160, 200), rng.range(48, 70), rng.range(32, 52), t.waterY, rng, true);
  } else {
    addHill(h, rng.range(110, 150), rng.range(100, 128), rng.range(40, 58), t.waterY, rng, true);
    addHill(h, rng.range(80, 110), rng.range(28, 44), rng.range(20, 36), t.waterY, rng, false);
    addHill(h, rng.range(160, 200), rng.range(28, 44), rng.range(18, 34), t.waterY, rng, false);
  }
  if ((rng.next() & 1) && style != 0) {
    int coveW = rng.range(14, 28);
    int coveX = rng.range(40, WORLD_W - 50 - coveW);
    carveCove(h, coveX, coveW, t.waterY, rng.range(10, 22));
  }
  smooth(h, WORLD_W);
  smooth(h, WORLD_W);
  for (int x = 0; x < WORLD_W; x++) {
    h[x] = wm_clamp(h[x], (int16_t)(WORLD_TOP + 8), (int16_t)t.waterY);
    if (h[x] < t.waterY - 8) fillCol(t, x, h[x], t.waterY);
  }
  if ((rng.next() & 3) != 0) {
    int rx = rng.range(16, 48);
    if (rng.next() & 1) rx = rng.range(WORLD_W - 48, WORLD_W - 16);
    addHill(h, rx, rng.range(10, 18), rng.range(10, 18), t.waterY, rng, false);
    for (int x = rx - 18; x <= rx + 18; x++) {
      if (x < 0 || x >= WORLD_W) continue;
      h[x] = wm_clamp(h[x], (int16_t)(WORLD_TOP + 8), (int16_t)t.waterY);
      if (h[x] < t.waterY - 6) fillCol(t, x, h[x], t.waterY);
    }
  }
}

static void genIslands(Terrain& t, Rng& rng) {
  int16_t h[WORLD_W];
  for (int x = 0; x < WORLD_W; x++) h[x] = t.waterY;
  int n = (rng.next() & 3) == 0 ? 3 : 2;
  if (n == 2) {
    addHill(h, rng.range(48, 72), rng.range(36, 52), rng.range(44, 66), t.waterY, rng, true);
    addHill(h, rng.range(184, 214), rng.range(36, 52), rng.range(38, 60), t.waterY, rng, true);
  } else {
    addHill(h, rng.range(40, 58), rng.range(28, 40), rng.range(36, 54), t.waterY, rng, true);
    addHill(h, rng.range(118, 140), rng.range(24, 38), rng.range(28, 48), t.waterY, rng, false);
    addHill(h, rng.range(196, 220), rng.range(28, 40), rng.range(36, 54), t.waterY, rng, true);
  }
  if (rng.next() & 1) {
    int sx = rng.range(96, 160);
    addHill(h, sx, rng.range(8, 14), rng.range(8, 16), t.waterY, rng, false);
  }
  smooth(h, WORLD_W);
  for (int x = 0; x < WORLD_W; x++) {
    h[x] = wm_clamp(h[x], (int16_t)(WORLD_TOP + 10), (int16_t)t.waterY);
    if (h[x] < t.waterY - 8) fillCol(t, x, h[x], t.waterY);
  }
}

static void genCavern(Terrain& t, Rng& rng) {
  int16_t ceilH[WORLD_W];
  int16_t floorH[WORLD_W];
  int16_t c = 20;
  int16_t f = 70;
  for (int x = 0; x < WORLD_W; x++) {
    c = (int16_t)(c + rng.range(-2, 3));
    f = (int16_t)(f + rng.range(-2, 3));
    if ((x % 23) == 0) {
      c = (int16_t)(c + rng.range(-5, 6));
      f = (int16_t)(f + rng.range(-5, 6));
    }
    c = wm_clamp(c, (int16_t)(WORLD_TOP + 2), 34);
    f = wm_clamp(f, 56, (int16_t)(t.waterY - 12));
    if (f - c < 22) f = (int16_t)(c + 22);
    ceilH[x] = c;
    floorH[x] = f;
  }
  for (int p = 0; p < 2; p++) {
    smooth(ceilH, WORLD_W);
    smooth(floorH, WORLD_W);
  }
  for (int x = 0; x < WORLD_W; x++) {
    if (floorH[x] - ceilH[x] < 18) floorH[x] = (int16_t)(ceilH[x] + 18);
    fillCol(t, x, WORLD_TOP, ceilH[x] + 1);
    fillCol(t, x, floorH[x], t.waterY);
  }
  int spikes = rng.range(8, 14);
  for (int i = 0; i < spikes; i++) {
    int x = rng.range(10, WORLD_W - 10);
    int len = rng.range(4, 12);
    if (rng.next() & 1) {
      for (int k = 0; k < len; k++) {
        if (ceilH[x] + 2 + k >= floorH[x] - 8) break;
        terrainSet(t, x, ceilH[x] + 1 + k, true);
        if (k) terrainSet(t, x + ((k & 1) ? 1 : -1), ceilH[x] + k, true);
      }
    } else {
      for (int k = 0; k < len; k++) {
        int y = floorH[x] - 1 - k;
        if (y <= ceilH[x] + 8) break;
        terrainSet(t, x, y, true);
      }
    }
  }
  int pillars = rng.range(2, 5);
  for (int i = 0; i < pillars; i++) {
    int x = rng.range(20, WORLD_W - 20);
    stampPillar(t, x, ceilH[x], floorH[x]);
  }
}

static void clearLow(Terrain& t, int x, int y0) {
  for (int y = y0; y < t.waterY; y++) terrainSet(t, x, y, false);
}

static void terrainSanitize(Terrain& t) {
  for (int pass = 0; pass < 3; pass++) {
    for (int x = 1; x < WORLD_W - 1; x++) {
      int y = surfaceY(t, x);
      int yl = surfaceY(t, x - 1);
      int yr = surfaceY(t, x + 1);
      if (y < 0) {
        if (yl >= 0 && yr >= 0) {
          int top = yl < yr ? yl : yr;
          fillCol(t, x, top, t.waterY);
        }
        continue;
      }
      if (yl >= 0 && yr >= 0) {
        int nmin = yl < yr ? yl : yr;
        if (y > nmin + 3) fillCol(t, x, nmin, t.waterY);
      } else if (yl >= 0 && yr < 0 && y > yl + 4) {
        fillCol(t, x, yl, t.waterY);
      } else if (yr >= 0 && yl < 0 && y > yr + 4) {
        fillCol(t, x, yr, t.waterY);
      }
    }
  }
  for (int x = 0; x < WORLD_W; x++) {
    int y = surfaceY(t, x);
    if (y >= 0 && y >= t.waterY - 7) clearLow(t, x, t.waterY - 10);
  }
  int x = 0;
  while (x < WORLD_W) {
    int y = surfaceY(t, x);
    if (y < 0) {
      x++;
      continue;
    }
    int x1 = x + 1;
    while (x1 < WORLD_W && surfaceY(t, x1) >= 0) x1++;
    if (x1 - x <= 6) {
      for (int i = x; i < x1; i++) {
        int sy = surfaceY(t, i);
        if (sy >= 0) clearLow(t, i, sy);
      }
    }
    x = x1;
  }
}

static void stampVine(Terrain& t, int x, int y0, int len) {
  for (int i = 0; i < len; i++) {
    int y = y0 + i;
    if (y >= t.waterY - 8) break;
    if ((i & 1) == 0) terrainSet(t, x, y, true);
    if ((i % 3) == 1) terrainSet(t, x + ((i & 2) ? 1 : -1), y, true);
  }
}

static void stampGirder(Terrain& t, int x0, int x1, int y) {
  if (x0 > x1) { int tmp = x0; x0 = x1; x1 = tmp; }
  for (int x = x0; x <= x1; x++) {
    terrainSet(t, x, y, true);
    terrainSet(t, x, y + 1, true);
    if ((x & 3) == 0) terrainSet(t, x, y + 2, true);
  }
}

static void stampCliff(Terrain& t, int x0, int x1, int top, int bot) {
  if (x0 > x1) { int tmp = x0; x0 = x1; x1 = tmp; }
  for (int x = x0; x <= x1 && x < WORLD_W; x++) {
    if (x < 0) continue;
    fillCol(t, x, top, bot);
  }
}

static void stampArch(Terrain& t, int cx, int top, int gap, int thick) {
  int left = cx - gap / 2 - thick;
  int right = cx + gap / 2;
  stampCliff(t, left, left + thick - 1, top, t.waterY);
  stampCliff(t, right, right + thick - 1, top, t.waterY);
  stampGirder(t, left, right + thick - 1, top);
  stampGirder(t, left + 1, right + thick - 2, top + 1);
}

static void stampFloat(Terrain& t, int x0, int x1, int y, int thick) {
  if (x0 > x1) { int tmp = x0; x0 = x1; x1 = tmp; }
  for (int x = x0; x <= x1 && x < WORLD_W; x++) {
    if (x < 0) continue;
    for (int k = 0; k < thick; k++) {
      int yy = y + k;
      if (yy >= t.waterY) break;
      terrainSet(t, x, yy, true);
    }
  }
}

static void stampLandmarks(Terrain& t, Rng& rng, uint8_t kind) {
  if (kind == MAP_CAVERN) {
    int vines = rng.range(6, 11);
    for (int i = 0; i < vines; i++) {
      int x = rng.range(12, WORLD_W - 12);
      int top = -1;
      for (int y = WORLD_TOP + 2; y < 40; y++) {
        if (terrainSolid(t, x, y) && !terrainSolid(t, x, y + 1)) {
          top = y + 1;
          break;
        }
      }
      if (top < 0) continue;
      stampVine(t, x, top, rng.range(6, 14));
    }
    int rocks = rng.range(4, 8);
    for (int i = 0; i < rocks; i++) {
      int x = rng.range(14, WORLD_W - 14);
      int gy = surfaceY(t, x);
      if (gy > 0) stampRock(t, x, gy);
    }
    stampSurfaceDecor(t, rng, false);
    int roots = rng.range(3, 6);
    for (int i = 0; i < roots; i++) {
      int x = rng.range(12, WORLD_W - 12);
      int gy = surfaceY(t, x);
      if (gy > 0 && gy <= t.waterY - 12) stampBush(t, x, gy);
    }
    return;
  }
  int feat = rng.range(0, 7);
  if (feat == 0) {
    int cx = rng.range(70, WORLD_W - 70);
    int top = rng.range(18, 32);
    int wall = rng.range(4, 8);
    stampCliff(t, cx, cx + wall, top, t.waterY);
    stampRock(t, cx + wall / 2, top);
    int gx0 = wm_clamp((int16_t)(cx - rng.range(18, 28)), 8, WORLD_W - 20);
    int gx1 = wm_clamp((int16_t)(cx + wall + rng.range(8, 16)), gx0 + 10, WORLD_W - 8);
    stampGirder(t, gx0, gx1, top + rng.range(8, 14));
  } else if (feat == 1) {
    int cx = rng.range(80, WORLD_W - 80);
    stampArch(t, cx, rng.range(20, 34), rng.range(10, 18), rng.range(3, 5));
  } else if (feat == 2) {
    int mx = rng.range(36, 90);
    stampCliff(t, mx, mx + rng.range(14, 26), rng.range(22, 36), t.waterY);
    int mx2 = rng.range(140, 200);
    stampCliff(t, mx2, mx2 + rng.range(12, 22), rng.range(24, 40), t.waterY);
  } else if (feat == 3) {
    int tx = rng.range(60, WORLD_W - 70);
    int top = rng.range(16, 28);
    stampCliff(t, tx, tx + rng.range(3, 5), top, t.waterY);
    stampCliff(t, tx + rng.range(6, 10), tx + rng.range(14, 18), top + rng.range(6, 12), t.waterY);
  } else if (feat == 4) {
    int y = rng.range(22, 36);
    int x0 = rng.range(40, 90);
    int x1 = x0 + rng.range(18, 36);
    stampFloat(t, x0, x1, y, rng.range(3, 5));
    if (rng.next() & 1) {
      int gy = surfaceY(t, (x0 + x1) / 2);
      if (gy > y + 8) stampGirder(t, x0 + 2, x1 - 2, (y + gy) / 2);
    }
  } else if (feat == 5) {
    int cx = rng.range(80, WORLD_W - 80);
    int gy = surfaceY(t, cx);
    if (gy > 20) {
      terrainCrater(t, cx, gy - 2, rng.range(6, 10));
      stampRock(t, cx - 6, gy);
      stampRock(t, cx + 6, gy);
    }
  }
  if (kind == MAP_ISLANDS) {
    int cx2 = (rng.next() & 1) ? rng.range(40, 80) : rng.range(170, 210);
    int gy = surfaceY(t, cx2);
    if (gy > 18 && (rng.next() & 1))
      stampArch(t, cx2, gy - rng.range(10, 16), rng.range(8, 14), 3);
    else if (gy > 16)
      stampFloat(t, cx2 - 8, cx2 + 8, gy - rng.range(12, 20), 3);
  } else if (feat == 6) {
    int mesa = rng.range(20, 34);
    int mx = rng.range(30, 80);
    stampCliff(t, mx, mx + rng.range(16, 24), mesa, t.waterY);
  }
  int nRock = rng.range(3, 7);
  for (int i = 0; i < nRock; i++) {
    int x = rng.range(12, WORLD_W - 14);
    int gy = surfaceY(t, x);
    if (gy > 0) stampRock(t, x, gy);
  }
  stampSurfaceDecor(t, rng, true);
}

static void settleToWater(Terrain& t) {
  for (int x = 0; x < WORLD_W; x++) {
    int lowest = -1;
    int solids = 0;
    for (int y = 0; y < t.waterY; y++) {
      if (!terrainSolid(t, x, y)) continue;
      lowest = y;
      solids++;
    }
    if (lowest < 0) continue;
    int gap = (int)t.waterY - 1 - lowest;
    if (gap >= 1 && gap <= 12 && solids >= 10) fillCol(t, x, lowest + 1, t.waterY);
  }
}

void terrainGenerate(Terrain& t, uint32_t seed, uint8_t kind) {
  terrainClear(t);
  Rng rng(seed ^ 0xA5A5u);
  if (kind == MAP_CAVERN) genCavern(t, rng);
  else if (kind == MAP_ISLANDS) genIslands(t, rng);
  else genIsland(t, rng);
  terrainSanitize(t);
  stampLandmarks(t, rng, kind);
  settleToWater(t);
}

void terrainCrater(Terrain& t, int cx, int cy, int r) {
  if (r < 1) r = 1;
  int r2 = r * r;
  for (int dy = -r; dy <= r; dy++) {
    for (int dx = -r; dx <= r; dx++) {
      if (dx * dx + dy * dy <= r2) {
        int y = cy + dy;
        if (y >= WORLD_TOP) terrainSet(t, cx + dx, y, false);
      }
    }
  }
}

int terrainStandY(const Terrain& t, int x) {
  x = wm_clamp((int16_t)x, 1, WORLD_W - 2);
  int found = -1;
  for (int y = WORLD_TOP + 3; y < t.waterY - 1; y++) {
    if (!terrainSolid(t, x, y) && !terrainSolid(t, x, y - 1) && !terrainSolid(t, x, y - 2) &&
        terrainSolid(t, x, y + 1))
      found = y;
  }
  return found;
}

bool terrainGrounded(const Terrain& t, int x, int y) {
  return terrainSolid(t, x, y + 1);
}

uint16_t terrainCrc(const Terrain& t) {
  return fnv16(t.bits, TERRAIN_BYTES);
}

void terrainCopy(Terrain& dst, const Terrain& src) {
  dst = src;
}
