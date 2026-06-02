#include "glcompat.h"

#include "texture.h"
#include "coreskin.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int iclamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void put_px(uint8_t *atlas, int tx, int ty, int px, int py,
                   int r, int g, int b) {
    if (px < 0 || px >= ATLAS_TILE_PX || py < 0 || py >= ATLAS_TILE_PX) return;
    int x = tx * ATLAS_TILE_PX + px;
    int y = ty * ATLAS_TILE_PX + py;
    int idx = (y * ATLAS_SIZE_PX + x) * 4;
    atlas[idx + 0] = (uint8_t)iclamp(r, 0, 255);
    atlas[idx + 1] = (uint8_t)iclamp(g, 0, 255);
    atlas[idx + 2] = (uint8_t)iclamp(b, 0, 255);
    atlas[idx + 3] = 255;
}

static void shift_px(uint8_t *atlas, int tx, int ty, int px, int py,
                     int dr, int dg, int db) {
    if (px < 0 || px >= ATLAS_TILE_PX || py < 0 || py >= ATLAS_TILE_PX) return;
    int x = tx * ATLAS_TILE_PX + px;
    int y = ty * ATLAS_TILE_PX + py;
    int idx = (y * ATLAS_SIZE_PX + x) * 4;
    atlas[idx + 0] = (uint8_t)iclamp((int)atlas[idx + 0] + dr, 0, 255);
    atlas[idx + 1] = (uint8_t)iclamp((int)atlas[idx + 1] + dg, 0, 255);
    atlas[idx + 2] = (uint8_t)iclamp((int)atlas[idx + 2] + db, 0, 255);
}

static void clear_px(uint8_t *atlas, int tx, int ty, int px, int py) {
    if (px < 0 || px >= ATLAS_TILE_PX || py < 0 || py >= ATLAS_TILE_PX) return;
    int x = tx * ATLAS_TILE_PX + px;
    int y = ty * ATLAS_TILE_PX + py;
    int idx = (y * ATLAS_SIZE_PX + x) * 4;
    atlas[idx + 3] = 0;
}

static void fill_tile(uint8_t *atlas, int tx, int ty,
                      int r, int g, int b, int noise_amp) {
    for (int py = 0; py < ATLAS_TILE_PX; py++) {
        for (int px = 0; px < ATLAS_TILE_PX; px++) {
            int n = noise_amp > 0 ? (rand() % (2 * noise_amp + 1)) - noise_amp : 0;
            put_px(atlas, tx, ty, px, py, r + n, g + n, b + n);
        }
    }
}

static void tile_stone(uint8_t *atlas, int tx, int ty) {
    fill_tile(atlas, tx, ty, 130, 130, 130, 12);
    for (int i = 0; i < 4; i++) {
        int px = rand() % ATLAS_TILE_PX;
        int py = rand() % ATLAS_TILE_PX;
        int n  = 1 + rand() % 3;
        for (int dy = 0; dy < n; dy++)
            for (int dx = 0; dx < n; dx++)
                shift_px(atlas, tx, ty, px + dx, py + dy, -40, -40, -40);
    }
}

static void tile_dirt(uint8_t *atlas, int tx, int ty) {
    fill_tile(atlas, tx, ty, 135, 95, 50, 14);
    for (int i = 0; i < 10; i++) {
        int px = rand() % ATLAS_TILE_PX;
        int py = rand() % ATLAS_TILE_PX;
        shift_px(atlas, tx, ty, px, py, -30, -25, -20);
    }
}

static void tile_grass_top(uint8_t *atlas, int tx, int ty) {
    fill_tile(atlas, tx, ty, 90, 175, 80, 14);
    for (int i = 0; i < 8; i++) {
        int px = rand() % ATLAS_TILE_PX;
        int py = rand() % (ATLAS_TILE_PX - 2);
        shift_px(atlas, tx, ty, px, py,     -25, -10, -25);
        shift_px(atlas, tx, ty, px, py + 1, -15,   0, -15);
    }
}

static void tile_grass_side(uint8_t *atlas, int tx, int ty) {
    fill_tile(atlas, tx, ty, 135, 95, 50, 14);
    for (int i = 0; i < 5; i++) {
        int px = rand() % ATLAS_TILE_PX;
        int py = rand() % (ATLAS_TILE_PX - 5);
        shift_px(atlas, tx, ty, px, py, -30, -25, -20);
    }
    int strip_h = 4;
    for (int py = ATLAS_TILE_PX - strip_h; py < ATLAS_TILE_PX; py++) {
        for (int px = 0; px < ATLAS_TILE_PX; px++) {
            int n = (rand() % 29) - 14;
            put_px(atlas, tx, ty, px, py, 90 + n, 175 + n, 80 + n);
        }
    }
    for (int i = 0; i < 5; i++) {
        int px = rand() % ATLAS_TILE_PX;
        int blade_len = 1 + rand() % 3;
        for (int dy = 0; dy < blade_len; dy++) {
            int py = ATLAS_TILE_PX - strip_h - 1 - dy;
            put_px(atlas, tx, ty, px, py, 80, 150, 70);
        }
    }
}

static void tile_water(uint8_t *atlas, int tx, int ty) {
    const float base_r = 36.0f, base_g = 96.0f, base_b = 182.0f;
    for (int py = 0; py < ATLAS_TILE_PX; py++) {
        for (int px = 0; px < ATLAS_TILE_PX; px++) {
            float u = (float)px / (float)ATLAS_TILE_PX;
            float v = (float)py / (float)ATLAS_TILE_PX;
            float w =
                0.55f * sinf(6.2831853f * (2.0f * u + 0.3f * v)) +
                0.45f * sinf(6.2831853f * (1.0f * v - 0.2f * u + 0.25f));
            int   bright = (int)(w * 8.0f);
            int   tint_b = (int)(w * 5.0f);
            put_px(atlas, tx, ty, px, py,
                   (int)base_r + bright,
                   (int)base_g + bright,
                   (int)base_b + bright + tint_b);
        }
    }
}

static void tile_gravel(uint8_t *atlas, int tx, int ty) {
    fill_tile(atlas, tx, ty, 145, 145, 135, 24);
    for (int i = 0; i < 12; i++) {
        int px    = rand() % ATLAS_TILE_PX;
        int py    = rand() % ATLAS_TILE_PX;
        int delta = (rand() % 60) - 30;
        shift_px(atlas, tx, ty, px, py, delta, delta, delta);
    }
}

static void tile_deepstone(uint8_t *atlas, int tx, int ty) {
    fill_tile(atlas, tx, ty, 50, 50, 65, 10);
    for (int i = 0; i < 2; i++) {
        int px = rand() % ATLAS_TILE_PX;
        int start_y = rand() % (ATLAS_TILE_PX - 6);
        int len = 3 + rand() % 5;
        for (int dy = 0; dy < len; dy++)
            shift_px(atlas, tx, ty, px, start_y + dy, 20, 20, 25);
    }
    for (int i = 0; i < 4; i++) {
        int px = rand() % ATLAS_TILE_PX;
        int py = rand() % ATLAS_TILE_PX;
        put_px(atlas, tx, ty, px, py, 100, 90, 110);
    }
}

static void tile_sand(uint8_t *atlas, int tx, int ty) {
    fill_tile(atlas, tx, ty, 225, 205, 140, 8);
    for (int i = 0; i < 8; i++) {
        int px = rand() % ATLAS_TILE_PX;
        int py = rand() % ATLAS_TILE_PX;
        shift_px(atlas, tx, ty, px, py, -15, -15, -10);
    }
}

static void tile_snow_top(uint8_t *atlas, int tx, int ty) {
    fill_tile(atlas, tx, ty, 235, 240, 245, 6);
    for (int i = 0; i < 5; i++) {
        int px = rand() % ATLAS_TILE_PX;
        int py = rand() % ATLAS_TILE_PX;
        shift_px(atlas, tx, ty, px, py, -8, -8, -4);
    }
}

static void tile_snow_side(uint8_t *atlas, int tx, int ty) {
    fill_tile(atlas, tx, ty, 135, 95, 50, 14);
    int strip_h = 5;
    for (int py = ATLAS_TILE_PX - strip_h; py < ATLAS_TILE_PX; py++) {
        for (int px = 0; px < ATLAS_TILE_PX; px++) {
            int n = (rand() % 11) - 5;
            put_px(atlas, tx, ty, px, py, 235 + n, 240 + n, 245 + n);
        }
    }
    for (int i = 0; i < 4; i++) {
        int px = rand() % ATLAS_TILE_PX;
        for (int dy = 0; dy < 2; dy++) {
            int py = ATLAS_TILE_PX - strip_h - 1 - dy;
            put_px(atlas, tx, ty, px, py, 220, 225, 230);
        }
    }
}

static void tile_wood_top(uint8_t *atlas, int tx, int ty) {
    fill_tile(atlas, tx, ty, 145, 105, 60, 8);
    int cx = ATLAS_TILE_PX / 2;
    int cy = ATLAS_TILE_PX / 2;
    for (int py = 0; py < ATLAS_TILE_PX; py++) {
        for (int px = 0; px < ATLAS_TILE_PX; px++) {
            int dx = px - cx;
            int dy = py - cy;
            int r2 = dx*dx + dy*dy;
            if ((r2 >= 8 && r2 <= 11) || (r2 >= 30 && r2 <= 36)) {
                shift_px(atlas, tx, ty, px, py, -30, -25, -20);
            }
        }
    }
}

static void tile_wood_side(uint8_t *atlas, int tx, int ty) {
    fill_tile(atlas, tx, ty, 145, 105, 60, 8);
    for (int py = 0; py < ATLAS_TILE_PX; py++) {
        for (int px = 0; px < ATLAS_TILE_PX; px++) {
            int mod = px % 4;
            if (mod == 0 || mod == 3) {
                int n = (rand() % 9) - 4;
                shift_px(atlas, tx, ty, px, py, -20 + n, -15 + n, -10 + n);
            }
        }
    }
}

static void tile_leaves(uint8_t *atlas, int tx, int ty) {
    fill_tile(atlas, tx, ty, 50, 110, 40, 15);
    for (int i = 0; i < 14; i++) {
        int px = rand() % ATLAS_TILE_PX;
        int py = rand() % ATLAS_TILE_PX;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                if (rand() % 2)
                    shift_px(atlas, tx, ty, px + dx, py + dy, -20, -15, -15);
            }
    }
    for (int i = 0; i < 4; i++) {
        int px = rand() % ATLAS_TILE_PX;
        int py = rand() % ATLAS_TILE_PX;
        shift_px(atlas, tx, ty, px, py, 20, 25, 15);
    }
    for (int i = 0; i < 20; i++) {
        int px = rand() % ATLAS_TILE_PX;
        int py = rand() % ATLAS_TILE_PX;
        clear_px(atlas, tx, ty, px, py);
        if (rand() % 2) clear_px(atlas, tx, ty, px + 1, py);
        if (rand() % 2) clear_px(atlas, tx, ty, px, py + 1);
        if (rand() % 2) clear_px(atlas, tx, ty, px + 1, py + 1);
    }
}

static void tile_coal_ore(uint8_t *atlas, int tx, int ty) {
    fill_tile(atlas, tx, ty, 130, 130, 130, 12);
    for (int i = 0; i < 4; i++) {
        int cx = 3 + rand() % (ATLAS_TILE_PX - 6);
        int cy = 3 + rand() % (ATLAS_TILE_PX - 6);
        int r = 2 + rand() % 2;
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                int d2 = dx*dx + dy*dy;
                if (d2 > r*r) continue;
                int n = (rand() % 21) - 10;
                put_px(atlas, tx, ty, cx + dx, cy + dy, 25 + n, 25 + n, 28 + n);
            }
    }
}

static void tile_iron_ore(uint8_t *atlas, int tx, int ty) {
    fill_tile(atlas, tx, ty, 130, 130, 130, 12);
    for (int i = 0; i < 3; i++) {
        int cx = 3 + rand() % (ATLAS_TILE_PX - 6);
        int cy = 3 + rand() % (ATLAS_TILE_PX - 6);
        int r = 2 + rand() % 2;
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                int d2 = dx*dx + dy*dy;
                if (d2 > r*r) continue;
                int n = (rand() % 15) - 7;
                if (d2 > (r-1)*(r-1)) {
                    put_px(atlas, tx, ty, cx+dx, cy+dy, 130 + n, 75 + n, 35 + n);
                } else {
                    put_px(atlas, tx, ty, cx+dx, cy+dy, 210 + n, 140 + n, 75 + n);
                }
            }
    }
}

static void tile_glowstone(uint8_t *atlas, int tx, int ty) {
    fill_tile(atlas, tx, ty, 235, 200, 95, 10);
    for (int i = 0; i < 5; i++) {
        int cx = 2 + rand() % (ATLAS_TILE_PX - 4);
        int cy = 2 + rand() % (ATLAS_TILE_PX - 4);
        int r = 1 + rand() % 2;
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                if (dx*dx + dy*dy > r*r) continue;
                int n = (rand() % 11) - 5;
                put_px(atlas, tx, ty, cx + dx, cy + dy,
                       255, 245 + n / 2, 200 + n);
            }
    }
    for (int i = 0; i < 4; i++) {
        int px = rand() % ATLAS_TILE_PX;
        int py = rand() % ATLAS_TILE_PX;
        shift_px(atlas, tx, ty, px, py, -45, -35, -20);
    }
}

static void tile_diamond_ore(uint8_t *atlas, int tx, int ty) {
    fill_tile(atlas, tx, ty, 130, 130, 130, 12);
    for (int i = 0; i < 3; i++) {
        int cx = 3 + rand() % (ATLAS_TILE_PX - 6);
        int cy = 3 + rand() % (ATLAS_TILE_PX - 6);
        int r = 2;
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                int man = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
                if (man > r) continue;
                int n = (rand() % 15) - 7;
                put_px(atlas, tx, ty, cx+dx, cy+dy, 95 + n, 200 + n/2, 235 + n/2);
            }
        put_px(atlas, tx, ty, cx, cy, 235, 255, 255);
    }
}

static unsigned int upload_rgba(const uint8_t *data, int w, int h) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

static void tile_planks(uint8_t *atlas, int tx, int ty) {
    fill_tile(atlas, tx, ty, 168, 128, 78, 8);
    for (int py = 0; py < ATLAS_TILE_PX; py++) {
        for (int px = 0; px < ATLAS_TILE_PX; px++) {
            if (py % 4 == 3) shift_px(atlas, tx, ty, px, py, -38, -32, -24);
            int n = (px * 7 + py * 13) % 6;
            if (n == 0) shift_px(atlas, tx, ty, px, py, -12, -10, -7);
        }
    }
    for (int py = 0; py < ATLAS_TILE_PX; py++) {
        int seam = ((py / 4) % 2 == 0) ? 7 : 0;
        shift_px(atlas, tx, ty, seam, py, -34, -28, -20);
    }
}

static void tile_quartz(uint8_t *atlas, int tx, int ty) {
    fill_tile(atlas, tx, ty, 236, 233, 224, 5);
    for (int i = 0; i < 7; i++) {
        int px = rand() % ATLAS_TILE_PX;
        int py = rand() % ATLAS_TILE_PX;
        shift_px(atlas, tx, ty, px, py, -10, -10, -12);
    }
}

static void tile_concrete(uint8_t *atlas, int tx, int ty) {
    fill_tile(atlas, tx, ty, 140, 141, 144, 6);
    for (int i = 0; i < 6; i++) {
        int px = rand() % ATLAS_TILE_PX;
        int py = rand() % ATLAS_TILE_PX;
        shift_px(atlas, tx, ty, px, py, -14, -14, -14);
    }
}

static void tile_glass(uint8_t *atlas, int tx, int ty) {
    fill_tile(atlas, tx, ty, 206, 226, 236, 4);
    for (int p = 0; p < ATLAS_TILE_PX; p++) {
        put_px(atlas, tx, ty, p, 0, 176, 204, 220);
        put_px(atlas, tx, ty, p, ATLAS_TILE_PX - 1, 176, 204, 220);
        put_px(atlas, tx, ty, 0, p, 176, 204, 220);
        put_px(atlas, tx, ty, ATLAS_TILE_PX - 1, p, 176, 204, 220);
    }
    for (int p = 3; p < ATLAS_TILE_PX - 3; p++) {
        put_px(atlas, tx, ty, p, p, 238, 247, 251);
    }
}

static unsigned int create_procedural_atlas(void) {
    size_t bytes = (size_t)ATLAS_SIZE_PX * ATLAS_SIZE_PX * 4;
    uint8_t *atlas = malloc(bytes);
    if (!atlas) return 0;
    memset(atlas, 0, bytes);

    srand(1);

    tile_stone(atlas,      0, 0);
    tile_dirt(atlas,       1, 0);
    tile_grass_top(atlas,  2, 0);
    tile_grass_side(atlas, 3, 0);
    tile_water(atlas,      0, 1);
    tile_gravel(atlas,     1, 1);
    tile_deepstone(atlas,  2, 1);
    tile_sand(atlas,       3, 1);
    tile_snow_top(atlas,   0, 2);
    tile_snow_side(atlas,  1, 2);
    tile_wood_top(atlas,   2, 2);
    tile_wood_side(atlas,  3, 2);
    tile_leaves(atlas,       0, 3);
    tile_coal_ore(atlas,     1, 3);
    tile_iron_ore(atlas,     2, 3);
    tile_diamond_ore(atlas,  3, 3);
    tile_glowstone(atlas,    4, 0);
    tile_planks(atlas,       4, 1);
    tile_quartz(atlas,       4, 2);
    tile_concrete(atlas,     4, 3);
    tile_glass(atlas,        0, 4);

    unsigned int tex = upload_rgba(atlas, ATLAS_SIZE_PX, ATLAS_SIZE_PX);
    free(atlas);
    return tex;
}

static unsigned int load_atlas_from_file(const char *path) {
    int w = 0, h = 0;
    uint8_t *data = coreskin_load_png(path, &w, &h, 1);
    if (!data) {
        fprintf(stderr, "texture: failed to decode '%s'\n", path);
        return 0;
    }
    if (w != ATLAS_SIZE_PX || h != ATLAS_SIZE_PX) {
        fprintf(stderr, "texture: atlas '%s' has size %dx%d, expected %dx%d\n",
                path, w, h, ATLAS_SIZE_PX, ATLAS_SIZE_PX);
        free(data);
        return 0;
    }
    unsigned int tex = upload_rgba(data, w, h);
    free(data);
    return tex;
}

unsigned int texture_create_atlas(const char *atlas_source) {
    if (atlas_source && atlas_source[0] != '\0') {
        return load_atlas_from_file(atlas_source);
    }
    return create_procedural_atlas();
}