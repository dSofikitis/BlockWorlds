#include "glcompat.h"

#include "texture.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static uint32_t s_rng = 0x1234567u;

static void rng_seed(uint32_t seed) {
    s_rng = seed ? seed : 0xA5A5A5A5u;
}

static uint32_t rng_next(void) {
    s_rng = s_rng * 1664525u + 1013904223u;
    return s_rng;
}

static int rng_noise(int amp) {
    if (amp <= 0) return 0;
    uint32_t span = (uint32_t)(2 * amp + 1);
    return (int)(rng_next() % span) - amp;
}

static int iclampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void px(uint8_t *a, int aw, int gx, int gy,
               int r, int g, int b, int alpha) {
    if (gx < 0 || gy < 0 || gx >= aw || gy >= aw) return;
    size_t idx = ((size_t)gy * (size_t)aw + (size_t)gx) * 4u;
    a[idx + 0] = (uint8_t)iclampi(r, 0, 255);
    a[idx + 1] = (uint8_t)iclampi(g, 0, 255);
    a[idx + 2] = (uint8_t)iclampi(b, 0, 255);
    a[idx + 3] = (uint8_t)iclampi(alpha, 0, 255);
}

static void tile_px(uint8_t *a, int aw, int tx, int ty, int lx, int ly,
                    int r, int g, int b, int alpha) {
    if (lx < 0 || ly < 0 || lx >= ATLAS_TILE_PX || ly >= ATLAS_TILE_PX) return;
    px(a, aw, tx * ATLAS_TILE_PX + lx, ty * ATLAS_TILE_PX + ly, r, g, b, alpha);
}

static void tile_fill(uint8_t *a, int aw, int tx, int ty,
                      int r, int g, int b, int alpha, int noise) {
    for (int ly = 0; ly < ATLAS_TILE_PX; ly++) {
        for (int lx = 0; lx < ATLAS_TILE_PX; lx++) {
            int n = rng_noise(noise);
            tile_px(a, aw, tx, ty, lx, ly, r + n, g + n, b + n, alpha);
        }
    }
}

static void tile_rect(uint8_t *a, int aw, int tx, int ty,
                      int x0, int y0, int x1, int y1,
                      int r, int g, int b, int alpha) {
    for (int ly = y0; ly <= y1; ly++)
        for (int lx = x0; lx <= x1; lx++)
            tile_px(a, aw, tx, ty, lx, ly, r, g, b, alpha);
}

static void tile_outline(uint8_t *a, int aw, int tx, int ty,
                         int x0, int y0, int x1, int y1,
                         int r, int g, int b, int alpha) {
    for (int lx = x0; lx <= x1; lx++) {
        tile_px(a, aw, tx, ty, lx, y0, r, g, b, alpha);
        tile_px(a, aw, tx, ty, lx, y1, r, g, b, alpha);
    }
    for (int ly = y0; ly <= y1; ly++) {
        tile_px(a, aw, tx, ty, x0, ly, r, g, b, alpha);
        tile_px(a, aw, tx, ty, x1, ly, r, g, b, alpha);
    }
}

static void tile_disc(uint8_t *a, int aw, int tx, int ty,
                      int cx, int cy, int rad,
                      int r, int g, int b, int alpha) {
    for (int ly = cy - rad; ly <= cy + rad; ly++) {
        for (int lx = cx - rad; lx <= cx + rad; lx++) {
            int dx = lx - cx;
            int dy = ly - cy;
            if (dx * dx + dy * dy <= rad * rad)
                tile_px(a, aw, tx, ty, lx, ly, r, g, b, alpha);
        }
    }
}

static void tile_line(uint8_t *a, int aw, int tx, int ty,
                      int x0, int y0, int x1, int y1, int width,
                      int r, int g, int b, int alpha) {
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    int wx = x0, wy = y0;
    for (;;) {
        for (int oy = 0; oy < width; oy++)
            for (int ox = 0; ox < width; ox++)
                tile_px(a, aw, tx, ty, wx + ox, wy + oy, r, g, b, alpha);
        if (wx == x1 && wy == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; wx += sx; }
        if (e2 <  dx) { err += dx; wy += sy; }
    }
}

#define BAW ATLAS_SIZE_PX

static void block_torch(uint8_t *a, int tx, int ty) {
    int cx = ATLAS_TILE_PX / 2;
    for (int ly = 7; ly < 15; ly++) {
        tile_px(a, BAW, tx, ty, cx - 1, ly, 96, 64, 28, 255);
        tile_px(a, BAW, tx, ty, cx,     ly, 120, 82, 40, 255);
    }
    tile_disc(a, BAW, tx, ty, cx, 4, 2, 240, 200, 60, 255);
    tile_px(a, BAW, tx, ty, cx - 1, 3, 255, 230, 120, 255);
    tile_px(a, BAW, tx, ty, cx,     3, 255, 245, 200, 255);
    tile_px(a, BAW, tx, ty, cx,     2, 255, 255, 230, 255);
    tile_px(a, BAW, tx, ty, cx,     5, 255, 160, 40,  255);
}

static void block_crafting_top(uint8_t *a, int tx, int ty) {
    tile_fill(a, BAW, tx, ty, 168, 128, 78, 255, 8);
    for (int p = 0; p < ATLAS_TILE_PX; p++) {
        tile_px(a, BAW, tx, ty, p, 5,  92, 64, 34, 255);
        tile_px(a, BAW, tx, ty, p, 10, 92, 64, 34, 255);
        tile_px(a, BAW, tx, ty, 5,  p, 92, 64, 34, 255);
        tile_px(a, BAW, tx, ty, 10, p, 92, 64, 34, 255);
    }
    tile_outline(a, BAW, tx, ty, 0, 0, 15, 15, 110, 78, 44, 255);
}

static void block_crafting_side(uint8_t *a, int tx, int ty) {
    tile_fill(a, BAW, tx, ty, 168, 128, 78, 255, 8);
    tile_rect(a, BAW, tx, ty, 0, 9, 15, 15, 128, 92, 52, 255);
    tile_disc(a, BAW, tx, ty, 8, 5, 3, 190, 190, 196, 255);
    tile_disc(a, BAW, tx, ty, 8, 5, 1, 110, 110, 116, 255);
    tile_px(a, BAW, tx, ty, 4, 5, 210, 210, 214, 255);
    tile_px(a, BAW, tx, ty, 12, 5, 210, 210, 214, 255);
    tile_px(a, BAW, tx, ty, 8, 1, 210, 210, 214, 255);
    tile_px(a, BAW, tx, ty, 8, 9, 210, 210, 214, 255);
    tile_outline(a, BAW, tx, ty, 0, 0, 15, 15, 110, 78, 44, 255);
}

static void block_furnace_side(uint8_t *a, int tx, int ty) {
    tile_fill(a, BAW, tx, ty, 120, 120, 124, 255, 10);
    tile_rect(a, BAW, tx, ty, 4, 8, 11, 13, 30, 28, 30, 255);
    tile_px(a, BAW, tx, ty, 5, 7, 30, 28, 30, 255);
    tile_px(a, BAW, tx, ty, 10, 7, 30, 28, 30, 255);
    tile_rect(a, BAW, tx, ty, 6, 6, 9, 6, 30, 28, 30, 255);
    tile_rect(a, BAW, tx, ty, 5, 12, 10, 13, 220, 110, 30, 255);
    tile_outline(a, BAW, tx, ty, 0, 0, 15, 15, 90, 90, 94, 255);
}

static void block_furnace_top(uint8_t *a, int tx, int ty) {
    tile_fill(a, BAW, tx, ty, 120, 120, 124, 255, 10);
    tile_disc(a, BAW, tx, ty, 8, 8, 4, 60, 60, 64, 255);
    tile_disc(a, BAW, tx, ty, 8, 8, 2, 36, 36, 40, 255);
    tile_outline(a, BAW, tx, ty, 0, 0, 15, 15, 90, 90, 94, 255);
}

static void block_forge(uint8_t *a, int tx, int ty) {
    tile_fill(a, BAW, tx, ty, 64, 64, 70, 255, 8);
    tile_rect(a, BAW, tx, ty, 3, 7, 12, 9, 255, 90, 20, 255);
    tile_rect(a, BAW, tx, ty, 4, 8, 11, 8, 255, 200, 90, 255);
    for (int i = 0; i < 4; i++) {
        int lx = 4 + (int)(rng_next() % 8u);
        int ly = 5 + (int)(rng_next() % 6u);
        tile_px(a, BAW, tx, ty, lx, ly, 255, 160, 60, 255);
    }
    tile_outline(a, BAW, tx, ty, 0, 0, 15, 15, 40, 40, 46, 255);
}

static void block_anvil(uint8_t *a, int tx, int ty) {
    tile_fill(a, BAW, tx, ty, 110, 110, 116, 255, 6);
    tile_rect(a, BAW, tx, ty, 2, 2, 13, 4, 56, 56, 62, 255);
    tile_rect(a, BAW, tx, ty, 5, 5, 10, 8, 56, 56, 62, 255);
    tile_rect(a, BAW, tx, ty, 3, 9, 12, 13, 56, 56, 62, 255);
    tile_px(a, BAW, tx, ty, 1, 3, 56, 56, 62, 255);
    tile_px(a, BAW, tx, ty, 14, 3, 70, 70, 76, 255);
}

static void block_chest(uint8_t *a, int tx, int ty) {
    tile_fill(a, BAW, tx, ty, 150, 104, 54, 255, 8);
    tile_rect(a, BAW, tx, ty, 0, 5, 15, 5, 96, 64, 30, 255);
    tile_rect(a, BAW, tx, ty, 0, 0, 15, 0, 110, 76, 38, 255);
    tile_rect(a, BAW, tx, ty, 7, 5, 8, 8, 70, 70, 76, 255);
    tile_px(a, BAW, tx, ty, 7, 6, 200, 200, 206, 255);
    tile_outline(a, BAW, tx, ty, 0, 0, 15, 15, 92, 62, 30, 255);
}

static void block_chest_top(uint8_t *a, int tx, int ty) {
    tile_fill(a, BAW, tx, ty, 150, 104, 54, 255, 8);
    tile_rect(a, BAW, tx, ty, 0, 7, 15, 8, 96, 64, 30, 255);
    tile_rect(a, BAW, tx, ty, 7, 0, 8, 1, 80, 80, 86, 255);
    tile_outline(a, BAW, tx, ty, 0, 0, 15, 15, 92, 62, 30, 255);
}

static void block_anvil_top(uint8_t *a, int tx, int ty) {
    tile_fill(a, BAW, tx, ty, 96, 96, 102, 255, 5);
    tile_rect(a, BAW, tx, ty, 2, 2, 13, 13, 72, 72, 78, 255);
    tile_outline(a, BAW, tx, ty, 1, 1, 14, 14, 48, 48, 54, 255);
}

static void block_bed_foot(uint8_t *a, int tx, int ty) {
    tile_fill(a, BAW, tx, ty, 196, 48, 48, 255, 8);
    tile_rect(a, BAW, tx, ty, 0, 13, 15, 15, 110, 72, 36, 255);
    tile_rect(a, BAW, tx, ty, 1, 1, 14, 2, 224, 86, 86, 255);
    tile_outline(a, BAW, tx, ty, 0, 0, 15, 12, 150, 32, 32, 255);
}

static void block_bed_head(uint8_t *a, int tx, int ty) {
    tile_fill(a, BAW, tx, ty, 196, 48, 48, 255, 8);
    tile_rect(a, BAW, tx, ty, 0, 13, 15, 15, 110, 72, 36, 255);
    tile_rect(a, BAW, tx, ty, 1, 1, 14, 4, 236, 236, 240, 255);
    tile_rect(a, BAW, tx, ty, 1, 0, 14, 0, 210, 210, 214, 255);
    tile_outline(a, BAW, tx, ty, 0, 0, 15, 12, 150, 32, 32, 255);
}

static void block_farmland(uint8_t *a, int tx, int ty) {
    tile_fill(a, BAW, tx, ty, 92, 60, 34, 255, 10);
    for (int lx = 2; lx < ATLAS_TILE_PX; lx += 4) {
        for (int ly = 0; ly < ATLAS_TILE_PX; ly++)
            tile_px(a, BAW, tx, ty, lx, ly, 70, 46, 26, 255);
    }
    for (int i = 0; i < 6; i++) {
        int lx = (int)(rng_next() % (uint32_t)ATLAS_TILE_PX);
        int ly = (int)(rng_next() % (uint32_t)ATLAS_TILE_PX);
        tile_px(a, BAW, tx, ty, lx, ly, 150, 120, 60, 255);
    }
}

static void block_wheat_crop(uint8_t *a, int tx, int ty) {
    int stalks[3] = { 3, 8, 12 };
    for (int s = 0; s < 3; s++) {
        int lx = stalks[s];
        for (int ly = 4; ly < 16; ly++) {
            int gcol = (ly > 9);
            tile_px(a, BAW, tx, ty, lx,
                    ly, gcol ? 120 : 210, gcol ? 160 : 180,
                    gcol ? 60 : 60, 255);
        }
        tile_px(a, BAW, tx, ty, lx - 1, 4, 226, 196, 70, 255);
        tile_px(a, BAW, tx, ty, lx + 1, 5, 226, 196, 70, 255);
        tile_px(a, BAW, tx, ty, lx,     3, 240, 210, 90, 255);
    }
}

static void block_sapling(uint8_t *a, int tx, int ty) {
    int cx = ATLAS_TILE_PX / 2;
    for (int ly = 9; ly < 15; ly++)
        tile_px(a, BAW, tx, ty, cx, ly, 96, 64, 28, 255);
    tile_disc(a, BAW, tx, ty, cx, 6, 3, 70, 150, 56, 255);
    tile_disc(a, BAW, tx, ty, cx, 6, 2, 92, 176, 70, 255);
    tile_px(a, BAW, tx, ty, cx - 3, 8, 70, 150, 56, 255);
    tile_px(a, BAW, tx, ty, cx + 3, 8, 70, 150, 56, 255);
}

static void block_iron_block(uint8_t *a, int tx, int ty) {
    tile_fill(a, BAW, tx, ty, 196, 196, 200, 255, 6);
    tile_rect(a, BAW, tx, ty, 0, 0, 15, 0, 230, 230, 234, 255);
    tile_rect(a, BAW, tx, ty, 0, 0, 0, 15, 230, 230, 234, 255);
    tile_rect(a, BAW, tx, ty, 0, 15, 15, 15, 150, 150, 154, 255);
    tile_rect(a, BAW, tx, ty, 15, 0, 15, 15, 150, 150, 154, 255);
    tile_outline(a, BAW, tx, ty, 2, 2, 13, 13, 176, 176, 180, 255);
}

static void block_diamond_block(uint8_t *a, int tx, int ty) {
    tile_fill(a, BAW, tx, ty, 78, 206, 200, 255, 8);
    tile_px(a, BAW, tx, ty, 4, 4, 220, 255, 252, 255);
    tile_px(a, BAW, tx, ty, 11, 6, 220, 255, 252, 255);
    tile_px(a, BAW, tx, ty, 7, 11, 220, 255, 252, 255);
    tile_px(a, BAW, tx, ty, 3, 4, 180, 240, 236, 255);
    tile_px(a, BAW, tx, ty, 12, 6, 180, 240, 236, 255);
    tile_outline(a, BAW, tx, ty, 0, 0, 15, 15, 50, 168, 162, 255);
}

static void block_coal_block(uint8_t *a, int tx, int ty) {
    tile_fill(a, BAW, tx, ty, 28, 28, 32, 255, 5);
    for (int i = 0; i < 6; i++) {
        int lx = (int)(rng_next() % (uint32_t)ATLAS_TILE_PX);
        int ly = (int)(rng_next() % (uint32_t)ATLAS_TILE_PX);
        tile_px(a, BAW, tx, ty, lx, ly, 56, 56, 62, 255);
    }
    tile_outline(a, BAW, tx, ty, 0, 0, 15, 15, 16, 16, 20, 255);
}

static void block_cobblestone(uint8_t *a, int tx, int ty) {
    tile_fill(a, BAW, tx, ty, 80, 80, 84, 255, 0);
    for (int i = 0; i < 9; i++) {
        int cx = 2 + (int)(rng_next() % 12u);
        int cy = 2 + (int)(rng_next() % 12u);
        int rad = 1 + (int)(rng_next() % 2u);
        int shade = 116 + (int)(rng_next() % 60u);
        for (int dy = -rad; dy <= rad; dy++)
            for (int dx = -rad; dx <= rad; dx++) {
                if (dx * dx + dy * dy > rad * rad) continue;
                int n = rng_noise(8);
                tile_px(a, BAW, tx, ty, cx + dx, cy + dy,
                        shade + n, shade + n, shade + 4 + n, 255);
            }
    }
}

static void block_wool(uint8_t *a, int tx, int ty) {
    tile_fill(a, BAW, tx, ty, 232, 230, 224, 255, 6);
    for (int i = 0; i < 18; i++) {
        int lx = (int)(rng_next() % (uint32_t)ATLAS_TILE_PX);
        int ly = (int)(rng_next() % (uint32_t)ATLAS_TILE_PX);
        int d = rng_noise(12);
        tile_px(a, BAW, tx, ty, lx, ly, 232 + d, 230 + d, 224 + d, 255);
    }
}

static void leaves_cutout(uint8_t *a, int tx, int ty, int gaps) {
    for (int i = 0; i < gaps; i++) {
        int lx = (int)(rng_next() % (uint32_t)ATLAS_TILE_PX);
        int ly = (int)(rng_next() % (uint32_t)ATLAS_TILE_PX);
        tile_px(a, BAW, tx, ty, lx, ly, 0, 0, 0, 0);
        if (rng_next() % 2u) tile_px(a, BAW, tx, ty, lx + 1, ly, 0, 0, 0, 0);
        if (rng_next() % 2u) tile_px(a, BAW, tx, ty, lx, ly + 1, 0, 0, 0, 0);
        if (rng_next() % 2u) tile_px(a, BAW, tx, ty, lx + 1, ly + 1, 0, 0, 0, 0);
    }
}

static void leaves_clusters(uint8_t *a, int tx, int ty, int count,
                            int dr, int dg, int db) {
    for (int i = 0; i < count; i++) {
        int cx = (int)(rng_next() % (uint32_t)ATLAS_TILE_PX);
        int cy = (int)(rng_next() % (uint32_t)ATLAS_TILE_PX);
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                if (rng_next() % 2u == 0u) continue;
                int lx = cx + dx;
                int ly = cy + dy;
                int gx = tx * ATLAS_TILE_PX + lx;
                int gy = ty * ATLAS_TILE_PX + ly;
                if (gx < 0 || gy < 0 || gx >= BAW || gy >= BAW) continue;
                size_t idx = ((size_t)gy * (size_t)BAW + (size_t)gx) * 4u;
                tile_px(a, BAW, tx, ty, lx, ly,
                        (int)a[idx + 0] + dr, (int)a[idx + 1] + dg,
                        (int)a[idx + 2] + db, (int)a[idx + 3]);
            }
    }
}

static void block_leaves_pine(uint8_t *a, int tx, int ty) {
    tile_fill(a, BAW, tx, ty, 31, 74, 51, 255, 12);
    leaves_clusters(a, tx, ty, 18, -14, -22, -16);
    for (int i = 0; i < 6; i++) {
        int lx = (int)(rng_next() % (uint32_t)ATLAS_TILE_PX);
        int ly = (int)(rng_next() % (uint32_t)ATLAS_TILE_PX);
        tile_px(a, BAW, tx, ty, lx, ly, 232, 240, 236, 255);
    }
    leaves_cutout(a, tx, ty, 12);
}

static void block_leaves_acacia(uint8_t *a, int tx, int ty) {
    tile_fill(a, BAW, tx, ty, 138, 154, 58, 255, 16);
    leaves_clusters(a, tx, ty, 16, -22, -18, -10);
    leaves_clusters(a, tx, ty, 6, 26, 24, 14);
    leaves_cutout(a, tx, ty, 18);
}

static void block_leaves_swamp(uint8_t *a, int tx, int ty) {
    tile_fill(a, BAW, tx, ty, 74, 90, 42, 255, 12);
    leaves_clusters(a, tx, ty, 16, -20, -16, -12);
    leaves_clusters(a, tx, ty, 4, 18, 18, 10);
    leaves_cutout(a, tx, ty, 26);
}

static void block_leaves_jungle(uint8_t *a, int tx, int ty) {
    tile_fill(a, BAW, tx, ty, 31, 122, 42, 255, 18);
    leaves_clusters(a, tx, ty, 18, -20, -30, -16);
    leaves_clusters(a, tx, ty, 6, 22, 34, 18);
    leaves_cutout(a, tx, ty, 14);
}

static void block_tall_grass(uint8_t *a, int tx, int ty) {
    int bases[6] = { 2, 5, 7, 9, 11, 14 };
    for (int s = 0; s < 6; s++) {
        int lx = bases[s];
        int top = 5 + (int)(rng_next() % 4u);
        int sway = (int)(rng_next() % 2u);
        int g = 170 + (int)(rng_next() % 30u);
        for (int ly = 15; ly >= top; ly--) {
            int wob = (ly < top + 4) ? sway : 0;
            int r = (ly < top + 3) ? 111 : 79;
            int b = (ly < top + 3) ? 85 : 68;
            int gg = (ly < top + 3) ? (g + 20) : g;
            tile_px(a, BAW, tx, ty, lx + wob, ly, r, iclampi(gg, 0, 255), b, 255);
        }
    }
}

static void block_brewing_side(uint8_t *a, int tx, int ty) {
    tile_fill(a, ATLAS_SIZE_PX, tx, ty, 96, 96, 102, 255, 14);
    tile_rect(a, ATLAS_SIZE_PX, tx, ty, 7, 1, 8, 8, 70, 66, 70, 255);
    tile_rect(a, ATLAS_SIZE_PX, tx, ty, 3, 9, 12, 14, 60, 58, 62, 255);
    tile_px(a, ATLAS_SIZE_PX, tx, ty, 7, 2, 220, 180, 60, 255);
    tile_px(a, ATLAS_SIZE_PX, tx, ty, 8, 2, 250, 210, 90, 255);
}
static void block_brewing_top(uint8_t *a, int tx, int ty) {
    tile_fill(a, ATLAS_SIZE_PX, tx, ty, 88, 88, 94, 255, 12);
    tile_disc(a, ATLAS_SIZE_PX, tx, ty, 8, 8, 2, 60, 58, 62, 255);
    tile_disc(a, ATLAS_SIZE_PX, tx, ty, 4, 4, 1, 120, 150, 200, 255);
    tile_disc(a, ATLAS_SIZE_PX, tx, ty, 12, 4, 1, 120, 150, 200, 255);
    tile_disc(a, ATLAS_SIZE_PX, tx, ty, 8, 12, 1, 120, 150, 200, 255);
}

void texture_paint_extra_blocks(uint8_t *atlas) {
    rng_seed(0xB10C0001u);
    block_brewing_side(atlas, 6, 2);
    block_brewing_top(atlas,  7, 2);

    block_torch(atlas,          5, 0);
    block_crafting_top(atlas,   6, 0);
    block_crafting_side(atlas,  7, 0);
    block_furnace_side(atlas,   8, 0);
    block_furnace_top(atlas,    9, 0);
    block_forge(atlas,         10, 0);
    block_anvil(atlas,         11, 0);
    block_chest(atlas,         12, 0);
    block_chest_top(atlas,      8, 2);
    block_anvil_top(atlas,      9, 2);
    block_bed_foot(atlas,      13, 0);
    block_bed_head(atlas,      14, 0);
    block_farmland(atlas,      15, 0);
    block_wheat_crop(atlas,     5, 1);
    block_sapling(atlas,        6, 1);
    block_iron_block(atlas,     7, 1);
    block_diamond_block(atlas,  8, 1);
    block_coal_block(atlas,     9, 1);
    block_cobblestone(atlas,   10, 1);
    block_wool(atlas,          11, 1);

    block_leaves_pine(atlas,   12, 1);
    block_leaves_acacia(atlas, 13, 1);
    block_leaves_swamp(atlas,  14, 1);
    block_leaves_jungle(atlas, 15, 1);
    block_tall_grass(atlas,     5, 2);
}

#define IAW ITEM_ATLAS_SIZE_PX

static void item_stick_handle(uint8_t *a, int tx, int ty) {
    tile_line(a, IAW, tx, ty, 11, 14, 5, 8, 2, 110, 74, 36, 255);
    tile_line(a, IAW, tx, ty, 11, 14, 5, 8, 1, 140, 96, 50, 255);
}

static void item_lump(uint8_t *a, int tx, int ty, int r, int g, int b) {
    tile_disc(a, IAW, tx, ty, 8, 9, 4, r, g, b, 255);
    tile_disc(a, IAW, tx, ty, 7, 7, 2,
              iclampi(r + 40, 0, 255), iclampi(g + 40, 0, 255),
              iclampi(b + 40, 0, 255), 255);
    for (int ang = 0; ang < 16; ang++) {
        double t = (double)ang * 3.14159265 / 8.0;
        int lx = 8 + (int)lround(4.6 * cos(t));
        int ly = 9 + (int)lround(4.6 * sin(t));
        tile_px(a, IAW, tx, ty, lx, ly,
                iclampi(r - 50, 0, 255), iclampi(g - 50, 0, 255),
                iclampi(b - 50, 0, 255), 255);
    }
}

static void item_ingot(uint8_t *a, int tx, int ty, int r, int g, int b) {
    tile_rect(a, IAW, tx, ty, 3, 7, 12, 11, r, g, b, 255);
    tile_rect(a, IAW, tx, ty, 4, 6, 11, 6, r, g, b, 255);
    tile_rect(a, IAW, tx, ty, 4, 7, 11, 7,
              iclampi(r + 40, 0, 255), iclampi(g + 40, 0, 255),
              iclampi(b + 40, 0, 255), 255);
    tile_outline(a, IAW, tx, ty, 3, 6, 12, 11,
                 iclampi(r - 60, 0, 255), iclampi(g - 60, 0, 255),
                 iclampi(b - 60, 0, 255), 255);
}

static void item_gem(uint8_t *a, int tx, int ty, int r, int g, int b) {
    for (int ly = 3; ly <= 12; ly++) {
        int half = (ly <= 7) ? (ly - 3 + 2) : (12 - ly + 2);
        tile_rect(a, IAW, tx, ty, 8 - half, ly, 8 + half, ly, r, g, b, 255);
    }
    tile_px(a, IAW, tx, ty, 6, 6, 230, 255, 255, 255);
    tile_px(a, IAW, tx, ty, 7, 5, 230, 255, 255, 255);
}

static void item_meat(uint8_t *a, int tx, int ty, int r, int g, int b) {
    tile_disc(a, IAW, tx, ty, 7, 8, 4, r, g, b, 255);
    tile_disc(a, IAW, tx, ty, 10, 9, 3, r, g, b, 255);
    tile_px(a, IAW, tx, ty, 6, 6,
            iclampi(r + 40, 0, 255), iclampi(g + 40, 0, 255),
            iclampi(b + 40, 0, 255), 255);
}

static void item_drumstick(uint8_t *a, int tx, int ty, int r, int g, int b) {
    tile_disc(a, IAW, tx, ty, 6, 7, 4, r, g, b, 255);
    tile_line(a, IAW, tx, ty, 8, 9, 13, 14, 2, 232, 226, 200, 255);
    tile_px(a, IAW, tx, ty, 13, 14, 245, 240, 220, 255);
    tile_px(a, IAW, tx, ty, 5, 5,
            iclampi(r + 40, 0, 255), iclampi(g + 40, 0, 255),
            iclampi(b + 40, 0, 255), 255);
}

static void tier_color(int tier, int *r, int *g, int *b) {
    switch (tier) {
        case 0: *r = 0x9c; *g = 0x7a; *b = 0x43; break;
        case 1: *r = 0x88; *g = 0x88; *b = 0x88; break;
        case 2: *r = 0xd8; *g = 0xd8; *b = 0xd8; break;
        default:*r = 0x3a; *g = 0xd6; *b = 0xc8; break;
    }
}

static void tool_pickaxe(uint8_t *a, int tx, int ty, int r, int g, int b) {
    item_stick_handle(a, tx, ty);
    tile_rect(a, IAW, tx, ty, 3, 4, 12, 5, r, g, b, 255);
    tile_px(a, IAW, tx, ty, 2, 5, r, g, b, 255);
    tile_px(a, IAW, tx, ty, 13, 5, r, g, b, 255);
    tile_px(a, IAW, tx, ty, 3, 3, r, g, b, 255);
    tile_px(a, IAW, tx, ty, 12, 3, r, g, b, 255);
    tile_outline(a, IAW, tx, ty, 3, 4, 12, 5,
                 iclampi(r - 50, 0, 255), iclampi(g - 50, 0, 255),
                 iclampi(b - 50, 0, 255), 255);
}

static void tool_axe(uint8_t *a, int tx, int ty, int r, int g, int b) {
    item_stick_handle(a, tx, ty);
    tile_rect(a, IAW, tx, ty, 7, 3, 12, 7, r, g, b, 255);
    tile_px(a, IAW, tx, ty, 6, 4, r, g, b, 255);
    tile_px(a, IAW, tx, ty, 6, 5, r, g, b, 255);
    tile_outline(a, IAW, tx, ty, 7, 3, 12, 7,
                 iclampi(r - 50, 0, 255), iclampi(g - 50, 0, 255),
                 iclampi(b - 50, 0, 255), 255);
}

static void tool_shovel(uint8_t *a, int tx, int ty, int r, int g, int b) {
    item_stick_handle(a, tx, ty);
    tile_rect(a, IAW, tx, ty, 4, 3, 8, 7, r, g, b, 255);
    tile_px(a, IAW, tx, ty, 5, 8, r, g, b, 255);
    tile_px(a, IAW, tx, ty, 7, 8, r, g, b, 255);
    tile_outline(a, IAW, tx, ty, 4, 3, 8, 7,
                 iclampi(r - 50, 0, 255), iclampi(g - 50, 0, 255),
                 iclampi(b - 50, 0, 255), 255);
}

static void tool_sword(uint8_t *a, int tx, int ty, int r, int g, int b) {
    tile_rect(a, IAW, tx, ty, 7, 1, 8, 10, r, g, b, 255);
    tile_px(a, IAW, tx, ty, 7, 1,
            iclampi(r + 50, 0, 255), iclampi(g + 50, 0, 255),
            iclampi(b + 50, 0, 255), 255);
    tile_rect(a, IAW, tx, ty, 5, 11, 10, 11, 110, 74, 36, 255);
    tile_rect(a, IAW, tx, ty, 7, 12, 8, 14, 110, 74, 36, 255);
    tile_outline(a, IAW, tx, ty, 7, 1, 8, 10,
                 iclampi(r - 50, 0, 255), iclampi(g - 50, 0, 255),
                 iclampi(b - 50, 0, 255), 255);
}

static void tool_hoe(uint8_t *a, int tx, int ty, int r, int g, int b) {
    item_stick_handle(a, tx, ty);
    tile_rect(a, IAW, tx, ty, 5, 3, 12, 4, r, g, b, 255);
    tile_rect(a, IAW, tx, ty, 11, 3, 12, 7, r, g, b, 255);
    tile_outline(a, IAW, tx, ty, 5, 3, 12, 4,
                 iclampi(r - 50, 0, 255), iclampi(g - 50, 0, 255),
                 iclampi(b - 50, 0, 255), 255);
}

static void tool_dispatch(uint8_t *a, int tx, int ty, int klass, int tier) {
    int r, g, b;
    tier_color(tier, &r, &g, &b);
    switch (klass) {
        case 0: tool_pickaxe(a, tx, ty, r, g, b); break;
        case 1: tool_axe(a, tx, ty, r, g, b);     break;
        case 2: tool_shovel(a, tx, ty, r, g, b);  break;
        case 3: tool_sword(a, tx, ty, r, g, b);   break;
        default:tool_hoe(a, tx, ty, r, g, b);     break;
    }
}

static void armor_color(int mat, int *r, int *g, int *b) {
    switch (mat) {
        case 0: *r = 0x8a; *g = 0x5a; *b = 0x2b; break;
        case 1: *r = 0xd8; *g = 0xd8; *b = 0xd8; break;
        default:*r = 0x3a; *g = 0xd6; *b = 0xc8; break;
    }
}

static void armor_helmet(uint8_t *a, int tx, int ty, int r, int g, int b) {
    tile_disc(a, IAW, tx, ty, 8, 8, 5, r, g, b, 255);
    tile_rect(a, IAW, tx, ty, 3, 8, 12, 12, r, g, b, 255);
    tile_rect(a, IAW, tx, ty, 6, 8, 9, 11, 0, 0, 0, 0);
    tile_outline(a, IAW, tx, ty, 3, 3, 12, 12,
                 iclampi(r - 60, 0, 255), iclampi(g - 60, 0, 255),
                 iclampi(b - 60, 0, 255), 255);
}

static void armor_chest(uint8_t *a, int tx, int ty, int r, int g, int b) {
    tile_rect(a, IAW, tx, ty, 3, 3, 12, 13, r, g, b, 255);
    tile_rect(a, IAW, tx, ty, 6, 3, 9, 4, 0, 0, 0, 0);
    tile_px(a, IAW, tx, ty, 4, 5,
            iclampi(r + 40, 0, 255), iclampi(g + 40, 0, 255),
            iclampi(b + 40, 0, 255), 255);
    tile_outline(a, IAW, tx, ty, 3, 3, 12, 13,
                 iclampi(r - 60, 0, 255), iclampi(g - 60, 0, 255),
                 iclampi(b - 60, 0, 255), 255);
}

static void armor_legs(uint8_t *a, int tx, int ty, int r, int g, int b) {
    tile_rect(a, IAW, tx, ty, 3, 2, 12, 4, r, g, b, 255);
    tile_rect(a, IAW, tx, ty, 3, 5, 6, 13, r, g, b, 255);
    tile_rect(a, IAW, tx, ty, 9, 5, 12, 13, r, g, b, 255);
    tile_outline(a, IAW, tx, ty, 3, 2, 12, 4,
                 iclampi(r - 60, 0, 255), iclampi(g - 60, 0, 255),
                 iclampi(b - 60, 0, 255), 255);
}

static void armor_boots(uint8_t *a, int tx, int ty, int r, int g, int b) {
    tile_rect(a, IAW, tx, ty, 3, 6, 6, 13, r, g, b, 255);
    tile_rect(a, IAW, tx, ty, 9, 6, 12, 13, r, g, b, 255);
    tile_rect(a, IAW, tx, ty, 3, 12, 7, 13, r, g, b, 255);
    tile_rect(a, IAW, tx, ty, 9, 12, 13, 13, r, g, b, 255);
    tile_outline(a, IAW, tx, ty, 3, 6, 6, 13,
                 iclampi(r - 60, 0, 255), iclampi(g - 60, 0, 255),
                 iclampi(b - 60, 0, 255), 255);
    tile_outline(a, IAW, tx, ty, 9, 6, 12, 13,
                 iclampi(r - 60, 0, 255), iclampi(g - 60, 0, 255),
                 iclampi(b - 60, 0, 255), 255);
}

static void armor_dispatch(uint8_t *a, int tx, int ty, int slot, int mat) {
    int r, g, b;
    armor_color(mat, &r, &g, &b);
    switch (slot) {
        case 0: armor_helmet(a, tx, ty, r, g, b); break;
        case 1: armor_chest(a, tx, ty, r, g, b);  break;
        case 2: armor_legs(a, tx, ty, r, g, b);   break;
        default:armor_boots(a, tx, ty, r, g, b);  break;
    }
}

static void item_stick(uint8_t *a, int tx, int ty) {
    tile_line(a, IAW, tx, ty, 12, 13, 4, 4, 2, 120, 82, 40, 255);
    tile_line(a, IAW, tx, ty, 12, 13, 4, 4, 1, 150, 104, 54, 255);
}

static void item_feather(uint8_t *a, int tx, int ty) {
    tile_line(a, IAW, tx, ty, 11, 13, 5, 4, 1, 200, 200, 206, 255);
    for (int ly = 4; ly <= 12; ly++) {
        int w = (ly - 3) / 2;
        tile_rect(a, IAW, tx, ty, 11 - (ly - 4) - w, ly,
                  11 - (ly - 4), ly, 236, 236, 240, 255);
    }
    tile_px(a, IAW, tx, ty, 5, 4, 255, 255, 255, 255);
}

static void item_bone(uint8_t *a, int tx, int ty) {
    tile_line(a, IAW, tx, ty, 4, 11, 11, 4, 2, 236, 232, 216, 255);
    tile_rect(a, IAW, tx, ty, 3, 3, 4, 5, 245, 242, 228, 255);
    tile_rect(a, IAW, tx, ty, 11, 10, 12, 12, 245, 242, 228, 255);
    tile_rect(a, IAW, tx, ty, 11, 2, 13, 4, 245, 242, 228, 255);
    tile_rect(a, IAW, tx, ty, 2, 11, 4, 13, 245, 242, 228, 255);
}

static void item_string(uint8_t *a, int tx, int ty) {
    for (int ly = 2; ly < 14; ly++) {
        int lx = 8 + (int)lround(3.0 * sin((double)ly * 0.9));
        tile_px(a, IAW, tx, ty, lx, ly, 222, 222, 226, 255);
        tile_px(a, IAW, tx, ty, lx + 1, ly, 200, 200, 206, 255);
    }
}

static void item_powder(uint8_t *a, int tx, int ty) {
    tile_disc(a, IAW, tx, ty, 8, 11, 4, 60, 60, 66, 255);
    for (int i = 0; i < 8; i++) {
        int lx = 4 + (int)(rng_next() % 8u);
        int ly = 6 + (int)(rng_next() % 6u);
        tile_px(a, IAW, tx, ty, lx, ly, 90, 90, 96, 255);
    }
}

static void item_wheat(uint8_t *a, int tx, int ty) {
    for (int s = -1; s <= 1; s++) {
        int base = 8 + s * 3;
        for (int ly = 3; ly < 14; ly++)
            tile_px(a, IAW, tx, ty, base, ly, 210, 178, 70, 255);
        tile_px(a, IAW, tx, ty, base - 1, 4, 232, 200, 90, 255);
        tile_px(a, IAW, tx, ty, base + 1, 5, 232, 200, 90, 255);
        tile_px(a, IAW, tx, ty, base, 2, 240, 214, 110, 255);
    }
}

static void item_seeds(uint8_t *a, int tx, int ty) {
    int pts[5][2] = { {6, 7}, {9, 6}, {7, 10}, {10, 9}, {8, 8} };
    for (int i = 0; i < 5; i++) {
        tile_px(a, IAW, tx, ty, pts[i][0], pts[i][1], 120, 92, 40, 255);
        tile_px(a, IAW, tx, ty, pts[i][0] + 1, pts[i][1], 150, 118, 56, 255);
    }
}

static void item_flint(uint8_t *a, int tx, int ty) {
    int pts[6][2] = { {5, 9}, {8, 5}, {11, 8}, {10, 12}, {6, 12}, {5, 9} };
    for (int i = 0; i < 5; i++)
        tile_line(a, IAW, tx, ty, pts[i][0], pts[i][1],
                  pts[i + 1][0], pts[i + 1][1], 1, 60, 60, 66, 255);
    tile_disc(a, IAW, tx, ty, 8, 9, 2, 80, 80, 86, 255);
    tile_px(a, IAW, tx, ty, 7, 7, 150, 150, 156, 255);
}

static void item_arrow(uint8_t *a, int tx, int ty) {
    tile_line(a, IAW, tx, ty, 11, 13, 4, 4, 1, 120, 82, 40, 255);
    tile_px(a, IAW, tx, ty, 3, 3, 200, 200, 206, 255);
    tile_px(a, IAW, tx, ty, 4, 3, 200, 200, 206, 255);
    tile_px(a, IAW, tx, ty, 3, 4, 200, 200, 206, 255);
    tile_px(a, IAW, tx, ty, 2, 4, 170, 170, 176, 255);
    tile_px(a, IAW, tx, ty, 12, 13, 240, 240, 244, 255);
    tile_px(a, IAW, tx, ty, 12, 11, 240, 240, 244, 255);
    tile_px(a, IAW, tx, ty, 13, 12, 240, 240, 244, 255);
}

static void item_bowl(uint8_t *a, int tx, int ty) {
    for (int ly = 7; ly <= 12; ly++) {
        int half = 6 - (12 - ly);
        tile_rect(a, IAW, tx, ty, 8 - half, ly, 8 + half, ly, 138, 96, 48, 255);
    }
    tile_rect(a, IAW, tx, ty, 2, 7, 13, 7, 168, 120, 62, 255);
    tile_rect(a, IAW, tx, ty, 4, 8, 11, 9, 96, 64, 30, 255);
}

static void item_leather(uint8_t *a, int tx, int ty) {
    tile_rect(a, IAW, tx, ty, 3, 4, 12, 12, 138, 90, 44, 255);
    tile_outline(a, IAW, tx, ty, 3, 4, 12, 12, 92, 58, 28, 255);
    for (int i = 0; i < 5; i++) {
        int lx = 4 + (int)(rng_next() % 8u);
        int ly = 5 + (int)(rng_next() % 7u);
        tile_px(a, IAW, tx, ty, lx, ly, 158, 108, 56, 255);
    }
}

static void item_spider_eye(uint8_t *a, int tx, int ty) {
    tile_disc(a, IAW, tx, ty, 8, 8, 5, 180, 30, 30, 255);
    tile_disc(a, IAW, tx, ty, 8, 8, 2, 240, 220, 60, 255);
    tile_px(a, IAW, tx, ty, 8, 8, 20, 20, 20, 255);
    tile_px(a, IAW, tx, ty, 6, 6, 240, 120, 120, 255);
}

static void item_rotten_flesh(uint8_t *a, int tx, int ty) {
    tile_disc(a, IAW, tx, ty, 8, 8, 4, 110, 100, 50, 255);
    tile_disc(a, IAW, tx, ty, 10, 9, 3, 96, 110, 56, 255);
    tile_px(a, IAW, tx, ty, 6, 6, 140, 130, 70, 255);
    tile_px(a, IAW, tx, ty, 11, 7, 120, 140, 70, 255);
}

static void item_bed(uint8_t *a, int tx, int ty) {
    tile_rect(a, IAW, tx, ty, 2, 7, 13, 12, 196, 48, 48, 255);
    tile_rect(a, IAW, tx, ty, 2, 12, 13, 13, 110, 72, 36, 255);
    tile_rect(a, IAW, tx, ty, 2, 6, 5, 8, 236, 236, 240, 255);
    tile_outline(a, IAW, tx, ty, 2, 6, 13, 13, 150, 32, 32, 255);
}

static void item_apple(uint8_t *a, int tx, int ty) {
    tile_disc(a, IAW, tx, ty, 8, 9, 5, 208, 40, 40, 255);
    tile_px(a, IAW, tx, ty, 6, 6, 240, 120, 120, 255);
    tile_rect(a, IAW, tx, ty, 8, 2, 8, 4, 100, 66, 30, 255);
    tile_rect(a, IAW, tx, ty, 9, 3, 11, 4, 80, 170, 70, 255);
}

static void item_bread(uint8_t *a, int tx, int ty) {
    tile_disc(a, IAW, tx, ty, 8, 9, 5, 196, 146, 70, 255);
    tile_rect(a, IAW, tx, ty, 4, 7, 12, 11, 210, 162, 84, 255);
    for (int i = 0; i < 3; i++)
        tile_px(a, IAW, tx, ty, 6 + i * 2, 7, 150, 104, 50, 255);
    tile_outline(a, IAW, tx, ty, 3, 6, 12, 12, 150, 104, 50, 255);
}

static void item_charcoal(uint8_t *a, int tx, int ty) {
    item_lump(a, tx, ty, 70, 70, 76);
}

static void item_coal(uint8_t *a, int tx, int ty) {
    item_lump(a, tx, ty, 32, 32, 36);
}

static void item_raw_iron(uint8_t *a, int tx, int ty) {
    item_lump(a, tx, ty, 198, 138, 84);
}

static void item_bow(uint8_t *a, int tx, int ty) {
    for (int ly = 2; ly <= 13; ly++) {
        double t = (double)(ly - 2) / 11.0;
        int lx = 4 + (int)lround(6.0 * sin(t * 3.14159265));
        tile_px(a, IAW, tx, ty, lx, ly, 120, 82, 40, 255);
        tile_px(a, IAW, tx, ty, lx + 1, ly, 150, 104, 54, 255);
    }
    tile_line(a, IAW, tx, ty, 4, 2, 4, 13, 1, 232, 232, 236, 255);
}

static void item_blob(uint8_t *a, int tx, int ty, int r, int g, int b) {
    tile_disc(a, IAW, tx, ty, 8, 9, 4, r, g, b, 255);
    tile_disc(a, IAW, tx, ty, 7, 8, 2, iclampi(r + 28, 0, 255), iclampi(g + 28, 0, 255), iclampi(b + 28, 0, 255), 255);
}
static void item_rod(uint8_t *a, int tx, int ty, int r, int g, int b) {
    tile_rect(a, IAW, tx, ty, 6, 3, 9, 13, r, g, b, 255);
    tile_rect(a, IAW, tx, ty, 7, 3, 8, 13, iclampi(r + 30, 0, 255), iclampi(g + 30, 0, 255), b, 255);
}
static void item_powder_c(uint8_t *a, int tx, int ty, int r, int g, int b) {
    tile_disc(a, IAW, tx, ty, 8, 11, 4, r, g, b, 255);
    for (int i = 0; i < 8; i++) {
        int lx = 4 + (int)(rng_next() % 8u), ly = 6 + (int)(rng_next() % 6u);
        tile_px(a, IAW, tx, ty, lx, ly, iclampi(r + 30, 0, 255), iclampi(g + 30, 0, 255), iclampi(b + 30, 0, 255), 255);
    }
}
static void item_bottle(uint8_t *a, int tx, int ty, int lr, int lg, int lb, int liquid, int splash) {
    tile_rect(a, IAW, tx, ty, 7, 2, 8, 4, 205, 222, 228, 255);
    tile_disc(a, IAW, tx, ty, 8, 10, 4, 196, 216, 224, 210);
    if (liquid) tile_disc(a, IAW, tx, ty, 8, 11, 3, lr, lg, lb, 255);
    tile_px(a, IAW, tx, ty, 6, 8, 235, 245, 248, 255);
    if (splash) { tile_px(a, IAW, tx, ty, 5, 5, 255, 255, 255, 255); tile_px(a, IAW, tx, ty, 11, 5, 255, 255, 255, 255); }
}

static void paint_brewing_items(uint8_t *a) {
    item_bottle(a, 3, 1, 0, 0, 0, 0, 0);
    item_bottle(a, 4, 1, 60, 120, 220, 1, 0);
    item_blob(a, 5, 1, 150, 40, 52);
    item_rod(a, 6, 1, 224, 162, 40);
    item_powder_c(a, 7, 1, 232, 174, 48);
    item_blob(a, 8, 1, 96, 150, 78);
    item_blob(a, 9, 1, 226, 120, 44);
    item_blob(a, 10, 1, 120, 200, 86);
    item_blob(a, 11, 1, 150, 210, 208);
    item_powder_c(a, 12, 1, 236, 236, 240);
    item_blob(a, 13, 1, 224, 202, 96);
    item_bottle(a, 14, 1, 178, 150, 168, 1, 0);
    item_powder_c(a, 15, 1, 206, 22, 22);

    static const int POT[11][3] = {
        {240,80,140},{120,200,230},{200,60,40},{240,60,80},{100,180,60},{230,150,40},
        {60,140,220},{110,20,34},{90,130,140},{110,140,110},{140,90,200},
    };
    for (int i = 0; i < 11; i++) {
        item_bottle(a, i, 4, POT[i][0], POT[i][1], POT[i][2], 1, 0);
        item_bottle(a, i, 5, POT[i][0], POT[i][1], POT[i][2], 1, 1);
    }
}

static void paint_items(uint8_t *a) {
    item_stick(a, 0, 0);
    item_coal(a, 1, 0);
    item_charcoal(a, 2, 0);
    item_raw_iron(a, 3, 0);
    item_ingot(a, 4, 0, 0xd8, 0xd8, 0xd8);
    item_gem(a, 5, 0, 0x3a, 0xd6, 0xc8);
    item_leather(a, 6, 0);
    item_feather(a, 7, 0);
    item_bone(a, 8, 0);
    item_string(a, 9, 0);
    item_powder(a, 10, 0);
    item_wheat(a, 11, 0);
    item_seeds(a, 12, 0);
    item_flint(a, 13, 0);
    item_arrow(a, 14, 0);
    item_bowl(a, 15, 0);

    item_spider_eye(a, 0, 1);
    item_rotten_flesh(a, 1, 1);
    item_bed(a, 2, 1);

    item_apple(a, 0, 2);
    item_bread(a, 1, 2);
    item_meat(a, 2, 2, 230, 130, 140);
    item_meat(a, 3, 2, 150, 96, 64);
    item_meat(a, 4, 2, 170, 50, 50);
    item_meat(a, 5, 2, 120, 78, 48);
    item_drumstick(a, 6, 2, 234, 176, 168);
    item_drumstick(a, 7, 2, 206, 158, 70);
    item_meat(a, 8, 2, 220, 110, 120);
    item_meat(a, 9, 2, 140, 90, 56);

    for (int tier = 0; tier < 4; tier++)
        for (int klass = 0; klass < 5; klass++)
            tool_dispatch(a, klass, 3 + tier, klass, tier);
    item_bow(a, 5, 3);

    for (int mat = 0; mat < 3; mat++)
        for (int slot = 0; slot < 4; slot++)
            armor_dispatch(a, slot, 7 + mat, slot, mat);

    paint_brewing_items(a);
}

unsigned int texture_create_item_atlas(void) {
    size_t bytes = (size_t)ITEM_ATLAS_SIZE_PX * (size_t)ITEM_ATLAS_SIZE_PX * 4u;
    uint8_t *buf = malloc(bytes);
    if (!buf) return 0;
    memset(buf, 0, bytes);

    rng_seed(0x17E70001u);
    paint_items(buf);

    unsigned int tex =
        texture_upload_rgba(buf, ITEM_ATLAS_SIZE_PX, ITEM_ATLAS_SIZE_PX);
    free(buf);
    return tex;
}

#define MAW MOB_ATLAS_SIZE_PX

static void mob_eyes(uint8_t *a, int fx, int fy, int er, int eg, int eb) {
    tile_rect(a, MAW, fx, fy, 3, 5, 5, 7, er, eg, eb, 255);
    tile_rect(a, MAW, fx, fy, 10, 5, 12, 7, er, eg, eb, 255);
}

static void paint_mob_skins(uint8_t *a) {
    static const int BODY[8][3] = {
        {238,150,170}, {110, 80, 62}, {235,235,228}, {226,224,216},
        { 70,118, 70}, {214,212,196}, { 74,160, 72}, { 54, 44, 42},
    };
    for (int sp = 0; sp < 8; sp++) {
        int br = BODY[sp][0], bg = BODY[sp][1], bb = BODY[sp][2];
        int lr = (br * 4) / 5, lg = (bg * 4) / 5, lb = (bb * 4) / 5;
        tile_fill(a, MAW, 0, sp, br, bg, bb, 255, 14);
        tile_fill(a, MAW, 2, sp, lr, lg, lb, 255, 10);
        tile_fill(a, MAW, 1, sp, br, bg, bb, 255, 8);

        switch (sp) {
            case 0:
                tile_rect(a, MAW, 1, sp, 5, 9, 10, 13, 224, 130, 150, 255);
                tile_px(a, MAW, 1, sp, 6, 11, 120, 60, 80, 255);
                tile_px(a, MAW, 1, sp, 9, 11, 120, 60, 80, 255);
                mob_eyes(a, 1, sp, 30, 20, 20);
                break;
            case 1:
                tile_rect(a, MAW, 0, sp, 2, 3, 6, 8, 235,235,228, 255);
                tile_rect(a, MAW, 0, sp, 9, 8, 13, 12, 235,235,228, 255);
                tile_rect(a, MAW, 1, sp, 4, 9, 11, 13, 220,200,196, 255);
                mob_eyes(a, 1, sp, 20, 15, 12);
                break;
            case 2:
                tile_fill(a, MAW, 3, sp, 240, 178, 44, 255, 6);
                tile_rect(a, MAW, 3, sp, 0, 10, 15, 15, 214, 150, 30, 255);
                tile_rect(a, MAW, 1, sp, 6, 9, 9, 11, 236,150, 40, 255);
                tile_rect(a, MAW, 1, sp, 6, 3, 9, 4, 210, 50, 50, 255);
                mob_eyes(a, 1, sp, 25, 20, 20);
                break;
            case 3:
                tile_fill(a, MAW, 0, sp, br, bg, bb, 255, 26);
                tile_rect(a, MAW, 1, sp, 3, 4, 12, 13, 210,186,160, 255);
                mob_eyes(a, 1, sp, 30, 24, 20);
                break;
            case 4:
                tile_rect(a, MAW, 0, sp, 2, 11, 13, 14, 60, 60,120, 255);
                tile_rect(a, MAW, 1, sp, 3, 6, 5, 8, 12, 24, 12, 255);
                tile_rect(a, MAW, 1, sp, 10, 6, 12, 8, 12, 24, 12, 255);
                tile_rect(a, MAW, 1, sp, 5, 11, 10, 11, 30, 40, 30, 255);
                break;
            case 5:
                tile_rect(a, MAW, 1, sp, 3, 5, 5, 8, 20, 20, 20, 255);
                tile_rect(a, MAW, 1, sp, 10, 5, 12, 8, 20, 20, 20, 255);
                for (int x = 4; x <= 11; x += 2) tile_px(a, MAW, 1, sp, x, 12, 60,60,55, 255);
                for (int y = 3; y <= 12; y += 3) tile_line(a, MAW, 0, sp, 3, y, 12, y, 1, 180,178,164, 255);
                break;
            case 6:
                for (int ty = 0; ty < ATLAS_TILE_PX; ty++)
                    for (int tx = 0; tx < ATLAS_TILE_PX; tx++)
                        if (((tx ^ ty) & 2)) tile_px(a, MAW, 0, sp, tx, ty, 56,132,54, 255);
                tile_rect(a, MAW, 1, sp, 3, 4, 5, 7, 15, 15, 15, 255);
                tile_rect(a, MAW, 1, sp, 10, 4, 12, 7, 15, 15, 15, 255);
                tile_rect(a, MAW, 1, sp, 6, 7, 9, 13, 15, 15, 15, 255);
                tile_rect(a, MAW, 1, sp, 4, 10, 5, 13, 15, 15, 15, 255);
                tile_rect(a, MAW, 1, sp, 10, 10, 11, 13, 15, 15, 15, 255);
                break;
            case 7:
                tile_disc(a, MAW, 0, sp, 8, 8, 5, 40, 32, 30, 255);
                tile_px(a, MAW, 1, sp, 4, 6, 180, 20, 20, 255);
                tile_px(a, MAW, 1, sp, 6, 5, 180, 20, 20, 255);
                tile_px(a, MAW, 1, sp, 9, 5, 180, 20, 20, 255);
                tile_px(a, MAW, 1, sp, 11, 6, 180, 20, 20, 255);
                tile_px(a, MAW, 1, sp, 5, 8, 150, 16, 16, 255);
                tile_px(a, MAW, 1, sp, 10, 8, 150, 16, 16, 255);
                break;
            default: break;
        }
    }

    tile_fill(a, MAW, 0, 8, 64, 96, 184, 255, 12);
    tile_fill(a, MAW, 2, 8, 60, 64, 120, 255, 10);
    tile_fill(a, MAW, 1, 8, 226, 174, 140, 255, 8);
    tile_rect(a, MAW, 1, 8, 3, 9, 12, 12, 120, 78, 40, 255);
    mob_eyes(a, 1, 8, 40, 40, 70);
    tile_rect(a, MAW, 1, 8, 6, 11, 9, 12, 150, 100, 90, 255);
}

unsigned int texture_create_mob_atlas(void) {
    size_t bytes = (size_t)MOB_ATLAS_SIZE_PX * (size_t)MOB_ATLAS_SIZE_PX * 4u;
    uint8_t *buf = malloc(bytes);
    if (!buf) return 0;
    memset(buf, 0, bytes);
    rng_seed(0x90B5C0DEu);
    paint_mob_skins(buf);
    unsigned int tex = texture_upload_rgba(buf, MOB_ATLAS_SIZE_PX, MOB_ATLAS_SIZE_PX);
    free(buf);
    return tex;
}

#define HAW HUD_ATLAS_SIZE_PX

static void hud_heart(uint8_t *a, int tx, int ty,
                      int r, int g, int b, int r2, int g2, int b2) {
    static const int span[12][2] = {
        {3, 5}, {2, 6}, {2, 13}, {2, 13}, {2, 13}, {3, 12},
        {4, 11}, {5, 10}, {6, 9}, {7, 8}, {7, 8}, {-1, -1}
    };
    static const int lobe[2][2] = { {10, 12}, {9, 13} };
    for (int row = 0; row < 12; row++) {
        if (span[row][0] < 0) continue;
        for (int lx = span[row][0]; lx <= span[row][1]; lx++) {
            int useright = (lx > 7);
            tile_px(a, HAW, tx, ty, lx, row + 2,
                    useright ? r2 : r, useright ? g2 : g,
                    useright ? b2 : b, 255);
        }
        if (row < 2) {
            for (int lx = lobe[row][0]; lx <= lobe[row][1]; lx++)
                tile_px(a, HAW, tx, ty, lx, row + 2, r2, g2, b2, 255);
        }
    }
    tile_px(a, HAW, tx, ty, 4, 4, 255, 200, 200, 255);
}

static void hud_heart_outline(uint8_t *a, int tx, int ty, int r, int g, int b) {
    hud_heart(a, tx, ty, r, g, b, r, g, b);
}

static void hud_drumstick(uint8_t *a, int tx, int ty, int variant) {
    int r = 150, g = 96, b = 48;
    if (variant == 2) { r = 70; g = 60; b = 50; }
    tile_disc(a, HAW, tx, ty, 6, 6, 4, r, g, b, 255);
    tile_line(a, HAW, tx, ty, 8, 8, 13, 13, 2,
              variant == 2 ? 90 : 232, variant == 2 ? 84 : 226,
              variant == 2 ? 70 : 200, 255);
    if (variant == 1) {
        tile_rect(a, HAW, tx, ty, 8, 0, 15, 15, 0, 0, 0, 0);
        tile_disc(a, HAW, tx, ty, 6, 6, 4, r, g, b, 255);
        tile_rect(a, HAW, tx, ty, 8, 0, 15, 15, 0, 0, 0, 0);
    }
}

static void hud_chestplate(uint8_t *a, int tx, int ty, int variant) {
    int r = 200, g = 200, b = 206;
    if (variant == 2) { r = 70; g = 70; b = 76; }
    tile_rect(a, HAW, tx, ty, 3, 4, 12, 12, r, g, b, 255);
    tile_rect(a, HAW, tx, ty, 6, 4, 9, 5, 0, 0, 0, 0);
    tile_outline(a, HAW, tx, ty, 3, 4, 12, 12,
                 iclampi(r - 60, 0, 255), iclampi(g - 60, 0, 255),
                 iclampi(b - 60, 0, 255), 255);
    if (variant == 1)
        tile_rect(a, HAW, tx, ty, 8, 0, 15, 15, 0, 0, 0, 0);
}

static void hud_bubble(uint8_t *a, int tx, int ty, int variant) {
    int rad = (variant == 0) ? 4 : 2;
    int alpha = (variant == 0) ? 255 : 160;
    tile_disc(a, HAW, tx, ty, 8, 8, rad, 150, 200, 240, alpha);
    tile_disc(a, HAW, tx, ty, 8, 8, rad - 1, 200, 230, 250, alpha);
    tile_px(a, HAW, tx, ty, 6, 6, 240, 250, 255, alpha);
}

static void hud_badge(uint8_t *a, int tx, int ty, int r, int g, int b) {
    tile_disc(a, HAW, tx, ty, 8, 8, 6, r, g, b, 255);
    for (int ang = 0; ang < 24; ang++) {
        double t = (double)ang * 3.14159265 / 12.0;
        int lx = 8 + (int)lround(6.0 * cos(t));
        int ly = 8 + (int)lround(6.0 * sin(t));
        tile_px(a, HAW, tx, ty, lx, ly,
                iclampi(r - 60, 0, 255), iclampi(g - 60, 0, 255),
                iclampi(b - 60, 0, 255), 255);
    }
}

static void hud_glyph_cross(uint8_t *a, int tx, int ty, int r, int g, int b) {
    tile_rect(a, HAW, tx, ty, 7, 4, 8, 11, r, g, b, 255);
    tile_rect(a, HAW, tx, ty, 4, 7, 11, 8, r, g, b, 255);
}

static void hud_glyph_boot(uint8_t *a, int tx, int ty, int r, int g, int b) {
    tile_rect(a, HAW, tx, ty, 6, 4, 8, 10, r, g, b, 255);
    tile_rect(a, HAW, tx, ty, 6, 10, 11, 11, r, g, b, 255);
}

static void hud_glyph_flame(uint8_t *a, int tx, int ty) {
    tile_disc(a, HAW, tx, ty, 8, 9, 3, 240, 120, 30, 255);
    tile_px(a, HAW, tx, ty, 8, 4, 255, 220, 90, 255);
    tile_px(a, HAW, tx, ty, 8, 5, 255, 180, 60, 255);
    tile_px(a, HAW, tx, ty, 8, 8, 255, 230, 140, 255);
}

static void hud_glyph_shield(uint8_t *a, int tx, int ty) {
    tile_rect(a, HAW, tx, ty, 5, 4, 10, 9, 70, 110, 220, 255);
    tile_rect(a, HAW, tx, ty, 6, 10, 9, 11, 70, 110, 220, 255);
    tile_px(a, HAW, tx, ty, 7, 12, 70, 110, 220, 255);
    tile_px(a, HAW, tx, ty, 8, 12, 70, 110, 220, 255);
    tile_px(a, HAW, tx, ty, 7, 6, 200, 220, 255, 255);
}

static void hud_star(uint8_t *a, int tx, int ty, int r, int g, int b) {
    tile_rect(a, HAW, tx, ty, 7, 2, 8, 13, r, g, b, 255);
    tile_rect(a, HAW, tx, ty, 2, 7, 13, 8, r, g, b, 255);
    tile_line(a, HAW, tx, ty, 3, 3, 12, 12, 1, r, g, b, 255);
    tile_line(a, HAW, tx, ty, 12, 3, 3, 12, 1, r, g, b, 255);
    tile_px(a, HAW, tx, ty, 7, 7, 255, 255, 255, 255);
}

static void fx_shard(uint8_t *a, int tx, int ty) {
    tile_rect(a, HAW, tx, ty, 6, 6, 9, 9, 170, 170, 176, 255);
    tile_px(a, HAW, tx, ty, 6, 6, 210, 210, 214, 255);
}

static void fx_soft_disc(uint8_t *a, int tx, int ty,
                         int r, int g, int b, int rad, int alpha) {
    for (int ly = 8 - rad; ly <= 8 + rad; ly++)
        for (int lx = 8 - rad; lx <= 8 + rad; lx++) {
            int dx = lx - 8, dy = ly - 8;
            int d2 = dx * dx + dy * dy;
            if (d2 > rad * rad) continue;
            int af = alpha - (d2 * alpha) / (rad * rad + 1);
            tile_px(a, HAW, tx, ty, lx, ly, r, g, b, af);
        }
}

static void fx_crit(uint8_t *a, int tx, int ty) {
    tile_rect(a, HAW, tx, ty, 7, 4, 8, 11, 255, 255, 255, 255);
    tile_rect(a, HAW, tx, ty, 4, 7, 11, 8, 255, 255, 255, 255);
}

static void fx_bubble_ring(uint8_t *a, int tx, int ty) {
    for (int ang = 0; ang < 28; ang++) {
        double t = (double)ang * 3.14159265 / 14.0;
        int lx = 8 + (int)lround(4.0 * cos(t));
        int ly = 8 + (int)lround(4.0 * sin(t));
        tile_px(a, HAW, tx, ty, lx, ly, 120, 190, 240, 230);
    }
    tile_px(a, HAW, tx, ty, 6, 6, 220, 240, 255, 230);
}

static void paint_hud(uint8_t *a) {
    hud_heart(a, 0, 0, 220, 40, 40, 220, 40, 40);
    hud_heart(a, 1, 0, 220, 40, 40, 60, 30, 30);
    hud_heart_outline(a, 2, 0, 70, 30, 30);
    hud_heart(a, 3, 0, 150, 190, 50, 150, 190, 50);

    hud_drumstick(a, 0, 1, 0);
    hud_drumstick(a, 1, 1, 1);
    hud_drumstick(a, 2, 1, 2);
    hud_bubble(a, 3, 1, 0);
    hud_bubble(a, 4, 1, 1);
    tile_disc(a, HAW, 5, 1, 8, 8, 3, 90, 210, 90, 255);
    tile_px(a, HAW, 5, 1, 6, 6, 200, 255, 200, 255);

    hud_chestplate(a, 0, 2, 0);
    hud_chestplate(a, 1, 2, 1);
    hud_chestplate(a, 2, 2, 2);

    hud_badge(a, 0, 3, 240, 140, 200);
    tile_px(a, HAW, 0, 3, 8, 8, 255, 255, 255, 255);
    hud_badge(a, 1, 3, 90, 190, 80);
    tile_disc(a, HAW, 1, 3, 8, 8, 2, 200, 240, 160, 255);
    hud_badge(a, 2, 3, 230, 140, 40);
    tile_rect(a, HAW, 2, 3, 6, 7, 10, 10, 180, 90, 20, 255);
    hud_badge(a, 3, 3, 140, 140, 146);
    tile_line(a, HAW, 3, 3, 6, 5, 9, 11, 1, 60, 60, 66, 255);
    hud_badge(a, 4, 3, 70, 200, 210);
    hud_glyph_boot(a, 4, 3, 30, 90, 100);
    hud_badge(a, 5, 3, 150, 110, 60);
    hud_glyph_boot(a, 5, 3, 90, 60, 30);
    hud_glyph_shield(a, 6, 3);

    hud_badge(a, 0, 4, 230, 130, 40);
    hud_glyph_flame(a, 0, 4);
    hud_badge(a, 1, 4, 60, 130, 220);
    tile_disc(a, HAW, 1, 4, 8, 8, 2, 200, 230, 255, 255);
    hud_badge(a, 2, 4, 150, 96, 48);
    tile_line(a, HAW, 2, 4, 6, 6, 11, 11, 2, 232, 226, 200, 255);
    hud_badge(a, 3, 4, 220, 60, 60);
    hud_glyph_cross(a, 3, 4, 255, 230, 230);
    hud_badge(a, 4, 4, 90, 40, 120);
    hud_glyph_cross(a, 4, 4, 40, 16, 60);

    tile_outline(a, HAW, 0, 5, 1, 1, 14, 14, 150, 150, 156, 255);
    tile_outline(a, HAW, 0, 5, 2, 2, 13, 13, 110, 110, 116, 255);
    tile_px(a, HAW, 0, 5, 1, 1, 0, 0, 0, 0);
    tile_px(a, HAW, 0, 5, 14, 1, 0, 0, 0, 0);
    tile_px(a, HAW, 0, 5, 1, 14, 0, 0, 0, 0);
    tile_px(a, HAW, 0, 5, 14, 14, 0, 0, 0, 0);
    hud_star(a, 1, 5, 240, 210, 60);

    fx_shard(a, 0, 6);
    fx_soft_disc(a, 1, 6, 200, 170, 110, 5, 220);
    fx_soft_disc(a, 2, 6, 130, 190, 240, 4, 230);
    fx_crit(a, 3, 6);
    fx_soft_disc(a, 4, 6, 130, 130, 136, 6, 200);
    hud_glyph_flame(a, 5, 6);
    fx_bubble_ring(a, 6, 6);
    hud_star(a, 7, 6, 255, 255, 255);
}

unsigned int texture_create_hud_atlas(void) {
    size_t bytes = (size_t)HUD_ATLAS_SIZE_PX * (size_t)HUD_ATLAS_SIZE_PX * 4u;
    uint8_t *buf = malloc(bytes);
    if (!buf) return 0;
    memset(buf, 0, bytes);

    rng_seed(0x4D000001u);
    paint_hud(buf);

    unsigned int tex =
        texture_upload_rgba(buf, HUD_ATLAS_SIZE_PX, HUD_ATLAS_SIZE_PX);
    free(buf);
    return tex;
}
