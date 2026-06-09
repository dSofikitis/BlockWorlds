#include "glcompat.h"

#include "world.h"
#include "mesh.h"
#include "chunk_mesher.h"
#include "falling.h"
#include "registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

#define CHUNK_BITS 4

#define MESH_WORKERS       4
#define GEN_WORKERS        3
#define MESH_QUEUE_CAP     8192
#define MESH_UPLOAD_BUDGET 48
#define GEN_FINALIZE_BUDGET 24

#define SALT_HEIGHT     0x00000000u
#define SALT_TEMP       0x00C0FFEEu
#define SALT_HUMID      0xBA5EBA11u
#define SALT_TREE       0x07E5EE15u
#define SALT_CAVE       0xCAFEC0DEu
#define SALT_VEIN       0x0EE0EE05u
#define SALT_GRASS      0x6A55C0DEu
#define SALT_ROCK       0x70CC0BBu
#define SALT_CONT       0xC0117A11u
#define SALT_EROSION    0xE5051077u
#define SALT_RIVER      0x5217E125u
#define SALT_WARP_X     0x3A1FC0DEu
#define SALT_WARP_Z     0x9E3779B1u

#define WORLD_BUCKETS  4096

#define CHUNK_CAP      2400

typedef struct chunk_node_s {
    int cx, cy, cz;
    chunk_t data;
    unsigned int vao, vbo, ebo;
    int idx_count;
    int opaque_idx_count;
    int glass_idx_count;
    int has_gpu;
    int dirty;
    int queued;
    int generating;
    int has_content;
    int cmin_x, cmin_y, cmin_z;
    int cmax_x, cmax_y, cmax_z;
    int      dormant;
    uint64_t last_touched;
    struct chunk_node_s *next;
} chunk_node_t;

typedef struct { int x, y, z; } pos3_t;

typedef struct {
    int32_t x, y, z;
    block_t block;
} delta_t;

#define OVERLAY_THRESHOLD (CHUNK_VOLUME / 2)

typedef struct chunk_edit_s {
    int      cx, cy, cz;
    int      delta_count;
    chunk_t *overlay;
    struct chunk_edit_s *next;
} chunk_edit_t;

struct world_s {
    uint32_t seed;
    char     name[WORLD_NAME_MAX];
    int      gamemode;
    int      allow_commands;
    uint8_t  difficulty, mob_spawning, keep_inventory, natural_regen, pvp, fall_damage,
             hunger_enabled, daylight_cycle, mob_griefing, weather_state;
    float    spawn_rate, smelt_mult;
    float    dawn_time;
    user_perm_t users[WORLD_MAX_USERS];
    int32_t  spawn_x, spawn_y, spawn_z;
    int      radius;
    int      center_cx, center_cz;
    int      initialized;
    chunk_node_t *buckets[WORLD_BUCKETS];

    delta_t *deltas;
    int      delta_count;
    int      delta_cap;

    uint32_t *delta_hash;
    int       delta_hash_cap;

    chunk_edit_t *edit_buckets[WORLD_BUCKETS];

    falling_system_t *falling;

#define WATER_QUEUE_CAP 65536
    pos3_t water_q[WATER_QUEUE_CAP];
    int    water_q_head;
    int    water_q_tail;

    _Atomic float sun_dx, sun_dy, sun_dz;

    int      chunk_count;
    uint64_t seq;

    pthread_rwlock_t map_lock;
    pthread_mutex_t  job_mutex;
    pthread_cond_t   job_cv;
    pthread_mutex_t  res_mutex;
    pthread_t        workers[MESH_WORKERS];
    int              worker_count;
    int              threads_quit;
    struct { int cx, cy, cz; }                 job_ring[MESH_QUEUE_CAP];
    int              job_head, job_tail;
    struct { int cx, cy, cz; mesh_buf_t mesh; } res_ring[MESH_QUEUE_CAP];
    int              res_head, res_tail;
    int              pending;

    pthread_mutex_t  gen_job_mutex;
    pthread_cond_t   gen_job_cv;
    pthread_mutex_t  gen_res_mutex;
    pthread_t        gen_workers[GEN_WORKERS];
    int              gen_worker_count;
    struct { int cx, cy, cz; }              gen_job_ring[MESH_QUEUE_CAP];
    int              gen_job_head, gen_job_tail;
    struct { int cx, cy, cz; chunk_t *data; } gen_res_ring[MESH_QUEUE_CAP];
    int              gen_res_head, gen_res_tail;
    int              gen_pending;
};

void world_set_falling_system(world_t *w, falling_system_t *fs) { w->falling = fs; }

static int  gen_enqueue(world_t *w, int cx, int cy, int cz);
static int  collect_generated(world_t *w, int budget);

static uint32_t hash2(uint32_t seed, int x, int z) {
    uint32_t h = (uint32_t)x * 0x9E3779B9u + seed;
    h ^= (uint32_t)z * 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

static uint32_t hash3(uint32_t seed, int x, int y, int z) {
    uint32_t h = (uint32_t)x * 0x9E3779B9u + seed;
    h ^= (uint32_t)y * 0xC2B2AE35u;
    h ^= (uint32_t)z * 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0x9E3779B9u;
    h ^= h >> 16;
    return h;
}

static int floor_div(int a, int b) {
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q -= 1;
    return q;
}

static int iabs(int v) { return v < 0 ? -v : v; }

static uint32_t bucket_index(int cx, int cy, int cz) {
    uint32_t h = (uint32_t)cx * 0x9E3779B9u;
    h ^= (uint32_t)cy * 0xC2B2AE35u;
    h ^= (uint32_t)cz * 0x85EBCA6Bu;
    h ^= h >> 16;
    return h & (WORLD_BUCKETS - 1);
}

static chunk_node_t *find_chunk(const world_t *w, int cx, int cy, int cz) {
    uint32_t b = bucket_index(cx, cy, cz);
    for (chunk_node_t *n = w->buckets[b]; n; n = n->next) {
        if (n->cx == cx && n->cy == cy && n->cz == cz) return n;
    }
    return NULL;
}

static chunk_node_t *insert_chunk(world_t *w, int cx, int cy, int cz) {
    chunk_node_t *n = calloc(1, sizeof *n);
    if (!n) return NULL;
    n->cx = cx; n->cy = cy; n->cz = cz;
    uint32_t b = bucket_index(cx, cy, cz);
    n->next = w->buckets[b];
    w->buckets[b] = n;
    w->chunk_count++;
    return n;
}

static chunk_edit_t *edit_chunk_find(const world_t *w, int cx, int cy, int cz) {
    uint32_t b = bucket_index(cx, cy, cz);
    for (chunk_edit_t *e = w->edit_buckets[b]; e; e = e->next) {
        if (e->cx == cx && e->cy == cy && e->cz == cz) return e;
    }
    return NULL;
}

static chunk_edit_t *edit_chunk_get_or_create(world_t *w, int cx, int cy, int cz) {
    chunk_edit_t *e = edit_chunk_find(w, cx, cy, cz);
    if (e) return e;
    e = calloc(1, sizeof *e);
    if (!e) { fprintf(stderr, "world: OOM allocating chunk edit record\n"); abort(); }
    e->cx = cx; e->cy = cy; e->cz = cz;
    uint32_t b = bucket_index(cx, cy, cz);
    e->next = w->edit_buckets[b];
    w->edit_buckets[b] = e;
    return e;
}

static void chunk_release_gpu(chunk_node_t *n) {
    if (n->has_gpu) {
        glDeleteVertexArrays(1, &n->vao);
        glDeleteBuffers(1, &n->vbo);
        glDeleteBuffers(1, &n->ebo);
        n->vao = n->vbo = n->ebo = 0;
        n->has_gpu = 0;
        n->idx_count = 0;
        n->opaque_idx_count = 0;
        n->glass_idx_count = 0;
    }
}

static void free_chunk_node(chunk_node_t *n) {
    chunk_release_gpu(n);
    free(n);
}

static void mark_chunk_dormant(world_t *w, int cx, int cy, int cz) {
    chunk_node_t *n = find_chunk(w, cx, cy, cz);
    if (!n || n->dormant) return;
    chunk_release_gpu(n);
    n->dormant = 1;
    n->dirty = 0;
    n->last_touched = ++w->seq;
}

static void evict_oldest_dormant(world_t *w) {
    uint64_t best_seq = UINT64_MAX;
    chunk_node_t  *best       = NULL;
    chunk_node_t **best_link  = NULL;
    for (int i = 0; i < WORLD_BUCKETS; i++) {
        chunk_node_t **link = &w->buckets[i];
        while (*link) {
            chunk_node_t *n = *link;
            if (n->dormant && n->last_touched < best_seq) {
                best_seq  = n->last_touched;
                best      = n;
                best_link = link;
            }
            link = &n->next;
        }
    }
    if (best && best_link) {
        *best_link = best->next;
        free_chunk_node(best);
        w->chunk_count--;
    }
}

static void enforce_chunk_cap(world_t *w) {
    while (w->chunk_count > CHUNK_CAP) {
        int before = w->chunk_count;
        evict_oldest_dormant(w);
        if (w->chunk_count == before) break;
    }
}

static int large_noise(uint32_t seed, int wx, int wz, uint32_t salt, int cell_size) {
    int cx_low  = floor_div(wx, cell_size) * cell_size;
    int cz_low  = floor_div(wz, cell_size) * cell_size;
    int cx_high = cx_low + cell_size;
    int cz_high = cz_low + cell_size;

    int h00 = (int)(hash2(seed + salt, cx_low,  cz_low ) & 0xFFu);
    int h10 = (int)(hash2(seed + salt, cx_high, cz_low ) & 0xFFu);
    int h01 = (int)(hash2(seed + salt, cx_low,  cz_high) & 0xFFu);
    int h11 = (int)(hash2(seed + salt, cx_high, cz_high) & 0xFFu);

    float tx = (float)(wx - cx_low) / (float)cell_size;
    float tz = (float)(wz - cz_low) / (float)cell_size;
    float h0 = (1.0f - tx) * (float)h00 + tx * (float)h10;
    float h1 = (1.0f - tx) * (float)h01 + tx * (float)h11;
    return (int)((1.0f - tz) * h0 + tz * h1);
}

biome_t world_biome_at(uint32_t seed, int wx, int wz) {
    int t = large_noise(seed, wx, wz, SALT_TEMP,  96);
    int h = large_noise(seed, wx, wz, SALT_HUMID, 96);
    if (t < 66)  return BIOME_SNOW;
    if (t > 196) return (h > 150) ? BIOME_JUNGLE : BIOME_DESERT;
    if (h > 206) return BIOME_SWAMP;
    if (h > 150) return BIOME_FOREST;
    if (h < 52)  return BIOME_ROCKY;
    return BIOME_PLAINS;
}

typedef struct {
    block_t surface;
    block_t subsurface;
    int     height_offset;
    int     tree_density;
    block_t leaf;
    int     tree_kind;
    int     grass_density;
} biome_def_t;

static const biome_def_t BIOMES[] = {
    [BIOME_PLAINS] = {BLOCK_GRASS, BLOCK_DIRT,  0,   3, BLOCK_LEAVES,        0, 55},
    [BIOME_FOREST] = {BLOCK_GRASS, BLOCK_DIRT,  4,  34, BLOCK_LEAVES,        0, 40},
    [BIOME_DESERT] = {BLOCK_SAND,  BLOCK_SAND,  2,   3, BLOCK_LEAVES_ACACIA, 2,  0},
    [BIOME_SNOW]   = {BLOCK_SNOW,  BLOCK_DIRT, 12,   9, BLOCK_LEAVES_PINE,   1, 12},
    [BIOME_SWAMP]  = {BLOCK_GRASS, BLOCK_DIRT, -1,  20, BLOCK_LEAVES_SWAMP,  3, 60},
    [BIOME_JUNGLE] = {BLOCK_GRASS, BLOCK_DIRT,  3,  64, BLOCK_LEAVES_JUNGLE, 4, 75},
    [BIOME_ROCKY]  = {BLOCK_STONE, BLOCK_STONE, 20,   0, BLOCK_LEAVES,        0, 30},
};

static float smooth01(float t) { return t * t * (3.0f - 2.0f * t); }

static float vnoise(uint32_t seed, uint32_t salt, int wx, int wz, int cell) {
    int x0 = floor_div(wx, cell) * cell;
    int z0 = floor_div(wz, cell) * cell;
    int x1 = x0 + cell, z1 = z0 + cell;
    float tx = smooth01((float)(wx - x0) / (float)cell);
    float tz = smooth01((float)(wz - z0) / (float)cell);
    const float inv = 1.0f / 65535.0f;
    float h00 = (float)(hash2(seed + salt, x0, z0) & 0xFFFFu) * inv;
    float h10 = (float)(hash2(seed + salt, x1, z0) & 0xFFFFu) * inv;
    float h01 = (float)(hash2(seed + salt, x0, z1) & 0xFFFFu) * inv;
    float h11 = (float)(hash2(seed + salt, x1, z1) & 0xFFFFu) * inv;
    float a = h00 + (h10 - h00) * tx;
    float b = h01 + (h11 - h01) * tx;
    return a + (b - a) * tz;
}

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float lerpf(float a, float b, float t) { return a + (b - a) * t; }

static void domain_warp(uint32_t seed, int wx, int wz, float amp, int cell,
                        int *ox, int *oz) {
    float dx = (vnoise(seed, SALT_WARP_X, wx, wz, cell) - 0.5f) * 2.0f * amp;
    float dz = (vnoise(seed, SALT_WARP_Z, wx, wz, cell) - 0.5f) * 2.0f * amp;
    *ox = wx + (int)dx;
    *oz = wz + (int)dz;
}

static float height_fbm(uint32_t seed, int wx, int wz) {
    float total = 0.0f, amp = 1.0f, norm = 0.0f;
    int cell = 320;
    for (int o = 0; o < 5; o++) {
        total += vnoise(seed, SALT_HEIGHT + (uint32_t)o * 1311u, wx, wz, cell) * amp;
        norm  += amp;
        amp   *= 0.5f;
        if (cell > 24) cell /= 2;
    }
    return total / norm;
}

static float landmass(uint32_t seed, int sx, int sz) {
    float c1 = vnoise(seed, SALT_CONT,         sx, sz, 560);
    float c2 = vnoise(seed, SALT_CONT + 5557u, sx, sz, 256);
    return c1 * 0.7f + c2 * 0.3f;
}

static float river_value(uint32_t seed, int wx, int wz) {
    int sx, sz;
    domain_warp(seed, wx, wz, 34.0f, 200, &sx, &sz);
    float n = vnoise(seed, SALT_RIVER, sx, sz, 360);
    return 1.0f - fabsf(n - 0.5f) * 2.0f;
}

static int blended_biome_offset(uint32_t seed, int wx, int wz) {
    float sum = 0.0f, wsum = 0.0f;
    for (int dz = -12; dz <= 12; dz += 6) {
        for (int dx = -12; dx <= 12; dx += 6) {
            float dist = (float)(abs(dx) + abs(dz));
            float wgt  = 1.0f / (1.0f + dist * 0.12f);
            sum  += (float)BIOMES[world_biome_at(seed, wx + dx, wz + dz)].height_offset * wgt;
            wsum += wgt;
        }
    }
    return (int)(sum / wsum + (sum < 0.0f ? -0.5f : 0.5f));
}

int world_height_at(uint32_t seed, int wx, int wz) {
    int sx, sz;
    domain_warp(seed, wx, wz, 40.0f, 384, &sx, &sz);

    float cont    = landmass(seed, sx, sz);
    float land    = smooth01(clampf((cont - 0.30f) / 0.16f, 0.0f, 1.0f));
    float detail  = height_fbm(seed, sx, sz);
    float erosion = vnoise(seed, SALT_EROSION, sx, sz, 512);

    float ocean_floor = (float)SEA_LEVEL - 9.0f + detail * 6.0f;
    float land_amp    = 8.0f + erosion * 34.0f;
    float land_h      = (float)SEA_LEVEL + 2.0f + smooth01(detail) * land_amp;
    int   h = (int)(ocean_floor + (land_h - ocean_floor) * land + 0.5f);

    h += (int)((float)blended_biome_offset(seed, wx, wz) * land);

    {
        float rv = river_value(seed, wx, wz);
        if (land > 0.55f && rv > 0.86f && h > SEA_LEVEL - 2) {
            float t = smooth01(clampf((rv - 0.86f) / 0.14f, 0.0f, 1.0f));
            float lowland = smooth01(clampf((float)(SEA_LEVEL + 8 - h) / 8.0f, 0.0f, 1.0f));
            t *= lowland;
            h = (int)lerpf((float)h, (float)(SEA_LEVEL - 2), t);
        }
    }

    if (h < 1) h = 1;
    if (h >= WORLD_HEIGHT) h = WORLD_HEIGHT - 1;
    return h;
}

static int cave_noise(uint32_t seed, int wx, int wy, int wz) {
    int cell = 24;
    int x_lo = floor_div(wx, cell) * cell;
    int y_lo = floor_div(wy, cell) * cell;
    int z_lo = floor_div(wz, cell) * cell;
    int x_hi = x_lo + cell;
    int y_hi = y_lo + cell;
    int z_hi = z_lo + cell;

    float v000 = (float)(hash3(seed + SALT_CAVE, x_lo, y_lo, z_lo) & 0xFFu);
    float v100 = (float)(hash3(seed + SALT_CAVE, x_hi, y_lo, z_lo) & 0xFFu);
    float v010 = (float)(hash3(seed + SALT_CAVE, x_lo, y_hi, z_lo) & 0xFFu);
    float v110 = (float)(hash3(seed + SALT_CAVE, x_hi, y_hi, z_lo) & 0xFFu);
    float v001 = (float)(hash3(seed + SALT_CAVE, x_lo, y_lo, z_hi) & 0xFFu);
    float v101 = (float)(hash3(seed + SALT_CAVE, x_hi, y_lo, z_hi) & 0xFFu);
    float v011 = (float)(hash3(seed + SALT_CAVE, x_lo, y_hi, z_hi) & 0xFFu);
    float v111 = (float)(hash3(seed + SALT_CAVE, x_hi, y_hi, z_hi) & 0xFFu);

    float tx = (float)(wx - x_lo) / (float)cell;
    float ty = (float)(wy - y_lo) / (float)cell;
    float tz = (float)(wz - z_lo) / (float)cell;

    float c00 = lerpf(v000, v100, tx);
    float c10 = lerpf(v010, v110, tx);
    float c01 = lerpf(v001, v101, tx);
    float c11 = lerpf(v011, v111, tx);
    float c0  = lerpf(c00, c10, ty);
    float c1  = lerpf(c01, c11, ty);
    return (int)lerpf(c0, c1, tz);
}

static int is_cave(uint32_t seed, int wx, int wy, int wz) {
    if (wy <= 1) return 0;
    if (wy >= 55) return 0;
    return cave_noise(seed, wx, wy, wz) > 190;
}

static block_t gen_block_col(uint32_t seed, int wx, int wy, int wz, int h, biome_t bm) {
    if (wy == 0) return BLOCK_DEEPSTONE;

    if (wy <= h) {
        int underwater = (h < SEA_LEVEL);
        int beach = (h <= SEA_LEVEL + 2);
        int sandy = underwater ||
                    (beach && bm != BIOME_SNOW && bm != BIOME_SWAMP && bm != BIOME_ROCKY);
        if (wy == h) {
            if (sandy) return BLOCK_SAND;
            if (bm == BIOME_ROCKY) {
                if (!underwater && (hash2(seed + SALT_ROCK, wx, wz) & 0xFFu) < 90) return BLOCK_GRASS;
                return BLOCK_STONE;
            }
            return BIOMES[bm].surface;
        }
        if (wy >= h - 3) {
            if (sandy) return BLOCK_SAND;
            return BIOMES[bm].subsurface;
        }
        if (is_cave(seed, wx, wy, wz)) return BLOCK_AIR;
        return BLOCK_STONE;
    }

    if (wy <= SEA_LEVEL) {
        if (wy == SEA_LEVEL && bm == BIOME_SNOW && h < SEA_LEVEL - 1) return BLOCK_ICE;
        return ((SEA_LEVEL - h) <= 3) ? BLOCK_WATER_SHALLOW : BLOCK_WATER;
    }
    return BLOCK_AIR;
}

uint32_t world_seed(const world_t *w) { return w->seed; }

void world_set_meta(world_t *w, const char *name, int gamemode, int allow_commands) {
    for (int i = 0; i < WORLD_NAME_MAX; i++) w->name[i] = '\0';
    if (name) {
        for (int i = 0; i < WORLD_NAME_MAX - 1 && name[i]; i++) w->name[i] = name[i];
    }
    w->gamemode = gamemode;
    w->allow_commands = allow_commands;
}
void world_set_gamemode(world_t *w, int gamemode) { w->gamemode = gamemode; }
const char *world_name(const world_t *w)  { return w->name; }
int         world_gamemode(const world_t *w) { return w->gamemode; }
int         world_allow_commands(const world_t *w) { return w->allow_commands; }

void world_set_difficulty(world_t *w, int d) { if (d < 0) d = 0; if (d > 3) d = 3; w->difficulty = (uint8_t)d; }
int  world_difficulty(const world_t *w)      { return w->difficulty; }
void world_set_spawn_rate(world_t *w, float r) { if (r < 0.0f) r = 0.0f; if (r > 8.0f) r = 8.0f; w->spawn_rate = r; }
float world_spawn_rate(const world_t *w)     { return w->spawn_rate; }
void world_set_dawn_time(world_t *w, float t) { if (t < 0.0f) t = 0.0f; if (t >= 1.0f) t = 0.999f; w->dawn_time = t; }
float world_dawn_time(const world_t *w)      { return w->dawn_time; }

static int user_find(const world_t *w, const char *user) {
    for (int i = 0; i < WORLD_MAX_USERS; i++)
        if (w->users[i].used && strncmp(w->users[i].name, user, USER_NAME_MAX) == 0) return i;
    return -1;
}
void world_perm_set(world_t *w, const char *user, int cmd_idx, int on) {
    if (!user || !user[0]) return;
    int idx = user_find(w, user);
    if (idx < 0) {
        for (int i = 0; i < WORLD_MAX_USERS; i++) if (!w->users[i].used) { idx = i; break; }
        if (idx < 0) return;
        memset(&w->users[idx], 0, sizeof w->users[idx]);
        for (int k = 0; k < USER_NAME_MAX - 1 && user[k]; k++) w->users[idx].name[k] = user[k];
        w->users[idx].used = 1;
        w->users[idx].allow_mask = w->allow_commands ? 0xFFFFFFFFu : 0u;
    }
    if (cmd_idx < 0) {
        w->users[idx].allow_mask = on ? 0xFFFFFFFFu : 0u;
    } else if (cmd_idx < 32) {
        if (on) w->users[idx].allow_mask |= (1u << cmd_idx);
        else    w->users[idx].allow_mask &= ~(1u << cmd_idx);
    }
}
int world_perm_allowed(const world_t *w, const char *user, int cmd_idx) {
    int idx = (user && user[0]) ? user_find(w, user) : -1;
    if (idx < 0) return w->allow_commands;
    if (cmd_idx < 0 || cmd_idx >= 32) return w->users[idx].allow_mask != 0u;
    return (int)((w->users[idx].allow_mask >> cmd_idx) & 1u);
}
void world_perm_clear(world_t *w, const char *user) {
    int idx = user_find(w, user);
    if (idx >= 0) memset(&w->users[idx], 0, sizeof w->users[idx]);
}
void world_set_smelt_mult(world_t *w, float m) { if (m < 0.05f) m = 0.05f; if (m > 20.0f) m = 20.0f; w->smelt_mult = m; }
float world_smelt_mult(const world_t *w)     { return w->smelt_mult; }
void world_set_spawn(world_t *w, int x, int y, int z) { w->spawn_x = x; w->spawn_y = y; w->spawn_z = z; }
void world_get_spawn(const world_t *w, int *x, int *y, int *z) { if (x) *x = w->spawn_x; if (y) *y = w->spawn_y; if (z) *z = w->spawn_z; }
int  world_keep_inventory(const world_t *w)  { return w->keep_inventory; }
int  world_natural_regen(const world_t *w)   { return w->natural_regen; }
int  world_pvp(const world_t *w)             { return w->pvp; }
int  world_fall_damage(const world_t *w)     { return w->fall_damage; }
int  world_hunger(const world_t *w)          { return w->hunger_enabled; }
int  world_mob_spawning(const world_t *w)    { return w->mob_spawning; }
int  world_mob_griefing(const world_t *w)    { return w->mob_griefing; }
int  world_daylight_cycle(const world_t *w)  { return w->daylight_cycle; }
void world_set_weather_state(world_t *w, int s) { w->weather_state = (uint8_t)s; }
int  world_weather_state(const world_t *w)   { return w->weather_state; }

void world_set_gamerule(world_t *w, gamerule_t g, int on) {
    uint8_t v = on ? 1 : 0;
    switch (g) {
        case GR_MOB_SPAWNING:   w->mob_spawning   = v; break;
        case GR_KEEP_INVENTORY: w->keep_inventory = v; break;
        case GR_NATURAL_REGEN:  w->natural_regen  = v; break;
        case GR_PVP:            w->pvp            = v; break;
        case GR_FALL_DAMAGE:    w->fall_damage    = v; break;
        case GR_HUNGER:         w->hunger_enabled = v; break;
        case GR_DAYLIGHT_CYCLE: w->daylight_cycle = v; break;
        case GR_MOB_GRIEFING:   w->mob_griefing   = v; break;
        default: break;
    }
}
int world_gamerule(const world_t *w, gamerule_t g) {
    switch (g) {
        case GR_MOB_SPAWNING:   return w->mob_spawning;
        case GR_KEEP_INVENTORY: return w->keep_inventory;
        case GR_NATURAL_REGEN:  return w->natural_regen;
        case GR_PVP:            return w->pvp;
        case GR_FALL_DAMAGE:    return w->fall_damage;
        case GR_HUNGER:         return w->hunger_enabled;
        case GR_DAYLIGHT_CYCLE: return w->daylight_cycle;
        case GR_MOB_GRIEFING:   return w->mob_griefing;
        default: return 0;
    }
}
void world_init_settings(world_t *w, int difficulty, int mob_spawning, float spawn_rate, int keep_inventory) {
    world_set_difficulty(w, difficulty);
    w->mob_spawning   = mob_spawning ? 1 : 0;
    world_set_spawn_rate(w, spawn_rate);
    w->keep_inventory = keep_inventory ? 1 : 0;
}

void  world_set_sun_dir(world_t *w, float x, float y, float z) {
    atomic_store_explicit(&w->sun_dx, x, memory_order_relaxed);
    atomic_store_explicit(&w->sun_dy, y, memory_order_relaxed);
    atomic_store_explicit(&w->sun_dz, z, memory_order_relaxed);
}
float world_sun_dir_x(const world_t *w) { return atomic_load_explicit(&w->sun_dx, memory_order_relaxed); }
float world_sun_dir_y(const world_t *w) { return atomic_load_explicit(&w->sun_dy, memory_order_relaxed); }
float world_sun_dir_z(const world_t *w) { return atomic_load_explicit(&w->sun_dz, memory_order_relaxed); }

block_t world_get_block(const world_t *w, int x, int y, int z) {
    if (y < 0 || y >= WORLD_HEIGHT) return BLOCK_AIR;
    int cx = x >> CHUNK_BITS;
    int cy = y >> CHUNK_BITS;
    int cz = z >> CHUNK_BITS;
    chunk_node_t *n = find_chunk(w, cx, cy, cz);
    if (!n) return BLOCK_AIR;
    return chunk_get(&n->data, x & (CHUNK_DIM - 1),
                     y & (CHUNK_DIM - 1),
                     z & (CHUNK_DIM - 1));
}

static void world_set_block_gen(world_t *w, int x, int y, int z, block_t b) {
    if (y < 0 || y >= WORLD_HEIGHT) return;
    int cx = x >> CHUNK_BITS;
    int cy = y >> CHUNK_BITS;
    int cz = z >> CHUNK_BITS;
    chunk_node_t *n = find_chunk(w, cx, cy, cz);
    if (!n) return;
    chunk_set(&n->data, x & (CHUNK_DIM - 1),
              y & (CHUNK_DIM - 1),
              z & (CHUNK_DIM - 1), b);
}

static uint32_t delta_coord_hash(int x, int y, int z) {
    return hash3(0x51ED1CEu, x, y, z);
}

static void delta_hash_insert_raw(world_t *w, int x, int y, int z, int idx) {
    uint32_t mask = (uint32_t)w->delta_hash_cap - 1u;
    uint32_t s = delta_coord_hash(x, y, z) & mask;
    while (w->delta_hash[s] != 0) s = (s + 1u) & mask;
    w->delta_hash[s] = (uint32_t)idx + 1u;
}

static void delta_hash_rebuild(world_t *w, int new_cap) {
    uint32_t *tbl = calloc((size_t)new_cap, sizeof *tbl);
    if (!tbl) { fprintf(stderr, "world: OOM growing delta index\n"); abort(); }
    free(w->delta_hash);
    w->delta_hash     = tbl;
    w->delta_hash_cap = new_cap;
    for (int i = 0; i < w->delta_count; i++)
        delta_hash_insert_raw(w, w->deltas[i].x, w->deltas[i].y, w->deltas[i].z, i);
}

static int delta_find(const world_t *w, int x, int y, int z) {
    if (!w->delta_hash) return -1;
    uint32_t mask = (uint32_t)w->delta_hash_cap - 1u;
    uint32_t s = delta_coord_hash(x, y, z) & mask;
    while (w->delta_hash[s] != 0) {
        int idx = (int)w->delta_hash[s] - 1;
        const delta_t *d = &w->deltas[idx];
        if (d->x == x && d->y == y && d->z == z) return idx;
        s = (s + 1u) & mask;
    }
    return -1;
}

static int has_delta(const world_t *w, int x, int y, int z) {
    return delta_find(w, x, y, z) >= 0;
}

static void record_delta(world_t *w, int x, int y, int z, block_t b) {
    int existing = delta_find(w, x, y, z);
    if (existing >= 0) {
        w->deltas[existing].block = b;
        return;
    }
    if (w->delta_count >= w->delta_cap) {
        int new_cap = w->delta_cap ? w->delta_cap * 2 : 128;
        delta_t *new_d = realloc(w->deltas, (size_t)new_cap * sizeof *new_d);
        if (!new_d) {
            fprintf(stderr, "world: out of memory growing delta array\n");
            return;
        }
        w->deltas    = new_d;
        w->delta_cap = new_cap;
    }
    int idx = w->delta_count;
    w->deltas[idx] = (delta_t){ (int32_t)x, (int32_t)y, (int32_t)z, b };
    w->delta_count++;
    if (!w->delta_hash || w->delta_count * 2 > w->delta_hash_cap) {
        int nc = w->delta_hash_cap ? w->delta_hash_cap * 2 : 256;
        while (nc < w->delta_count * 2) nc *= 2;
        delta_hash_rebuild(w, nc);
    } else {
        delta_hash_insert_raw(w, x, y, z, idx);
    }
    chunk_edit_t *ce = edit_chunk_get_or_create(w, x >> CHUNK_BITS, y >> CHUNK_BITS, z >> CHUNK_BITS);
    ce->delta_count++;
}

static int chunk_is_overlaid(const world_t *w, int x, int y, int z) {
    chunk_edit_t *e = edit_chunk_find(w, x >> CHUNK_BITS, y >> CHUNK_BITS, z >> CHUNK_BITS);
    return e && e->overlay;
}

static void gen_write_keep_player(world_t *w, int x, int y, int z, block_t b) {
    if (has_delta(w, x, y, z)) return;
    if (chunk_is_overlaid(w, x, y, z)) return;
    chunk_node_t *n = find_chunk(w, x >> CHUNK_BITS, y >> CHUNK_BITS, z >> CHUNK_BITS);
    if (n && n->generating) return;
    world_set_block_gen(w, x, y, z, b);
}

static void mark_dirty_around(world_t *w, int x, int y, int z) {
    int cx = x >> CHUNK_BITS, cy = y >> CHUNK_BITS, cz = z >> CHUNK_BITS;
    chunk_node_t *n = find_chunk(w, cx, cy, cz);
    if (n) n->dirty = 1;
    int lx = x & (CHUNK_DIM - 1);
    int ly = y & (CHUNK_DIM - 1);
    int lz = z & (CHUNK_DIM - 1);
    if (lx == 0)               { n = find_chunk(w, cx - 1, cy, cz); if (n) n->dirty = 1; }
    if (lx == CHUNK_DIM - 1)   { n = find_chunk(w, cx + 1, cy, cz); if (n) n->dirty = 1; }
    if (lz == 0)               { n = find_chunk(w, cx, cy, cz - 1); if (n) n->dirty = 1; }
    if (lz == CHUNK_DIM - 1)   { n = find_chunk(w, cx, cy, cz + 1); if (n) n->dirty = 1; }
    if (ly == 0)               { n = find_chunk(w, cx, cy - 1, cz); if (n) n->dirty = 1; }
    if (ly == CHUNK_DIM - 1)   { n = find_chunk(w, cx, cy + 1, cz); if (n) n->dirty = 1; }
}

static void convert_chunk_to_overlay(world_t *w, chunk_edit_t *ce) {
    chunk_node_t *n = find_chunk(w, ce->cx, ce->cy, ce->cz);
    if (!n) return;
    chunk_t *snap = malloc(sizeof *snap);
    if (!snap) { fprintf(stderr, "world: OOM creating chunk overlay\n"); return; }
    *snap = n->data;
    ce->overlay     = snap;
    ce->delta_count = 0;

    int x0 = ce->cx * CHUNK_DIM, x1 = x0 + CHUNK_DIM;
    int y0 = ce->cy * CHUNK_DIM, y1 = y0 + CHUNK_DIM;
    int z0 = ce->cz * CHUNK_DIM, z1 = z0 + CHUNK_DIM;
    int kept = 0;
    for (int i = 0; i < w->delta_count; i++) {
        delta_t d = w->deltas[i];
        int inside = d.x >= x0 && d.x < x1 && d.y >= y0 && d.y < y1 && d.z >= z0 && d.z < z1;
        if (!inside) w->deltas[kept++] = d;
    }
    w->delta_count = kept;
    delta_hash_rebuild(w, w->delta_hash_cap);
}

static void persist_edit(world_t *w, int x, int y, int z, block_t b) {
    chunk_edit_t *ce = edit_chunk_get_or_create(w, x >> CHUNK_BITS, y >> CHUNK_BITS, z >> CHUNK_BITS);
    if (ce->overlay) {
        chunk_set(ce->overlay, x & (CHUNK_DIM - 1), y & (CHUNK_DIM - 1), z & (CHUNK_DIM - 1), b);
        return;
    }
    record_delta(w, x, y, z, b);
    if (ce->delta_count > OVERLAY_THRESHOLD) convert_chunk_to_overlay(w, ce);
}

static void apply_edit(world_t *w, int x, int y, int z, block_t b) {
    world_set_block_gen(w, x, y, z, b);
    persist_edit(w, x, y, z, b);
    mark_dirty_around(w, x, y, z);
}

static void cascade_sand(world_t *w, int x, int y, int z) {
    if (!w->falling) return;
    int sy = y + 1;
    while (sy < WORLD_HEIGHT && world_get_block(w, x, sy, z) == BLOCK_SAND) {
        falling_spawn(w->falling, x, sy, z, BLOCK_SAND);
        apply_edit(w, x, sy, z, BLOCK_AIR);
        sy++;
    }
}

static int has_water_source(const world_t *w, int x, int y, int z) {
    if (block_is_water(world_get_block(w, x, y + 1, z))) return 1;
    if (block_is_water(world_get_block(w, x + 1, y, z))) return 1;
    if (block_is_water(world_get_block(w, x - 1, y, z))) return 1;
    if (block_is_water(world_get_block(w, x, y, z + 1))) return 1;
    if (block_is_water(world_get_block(w, x, y, z - 1))) return 1;
    return 0;
}

static void water_enqueue(world_t *w, int x, int y, int z) {
    if (y < 0 || y > SEA_LEVEL) return;
    int next = (w->water_q_tail + 1) % WATER_QUEUE_CAP;
    if (next == w->water_q_head) return;
    w->water_q[w->water_q_tail] = (pos3_t){x, y, z};
    w->water_q_tail = next;
}

static void water_fill(world_t *w, int x, int y, int z) {
    block_t b = BLOCK_WATER;
    if (world_get_block(w, x + 1, y, z) == BLOCK_WATER_SHALLOW ||
        world_get_block(w, x - 1, y, z) == BLOCK_WATER_SHALLOW ||
        world_get_block(w, x, y, z + 1) == BLOCK_WATER_SHALLOW ||
        world_get_block(w, x, y, z - 1) == BLOCK_WATER_SHALLOW ||
        world_get_block(w, x, y + 1, z) == BLOCK_WATER_SHALLOW)
        b = BLOCK_WATER_SHALLOW;
    world_set_block_gen(w, x, y, z, b);
    mark_dirty_around(w, x, y, z);
}

void world_tick_water(world_t *w, int max_cells) {
    if (w->water_q_head == w->water_q_tail) return;
    pthread_rwlock_wrlock(&w->map_lock);
    int processed = 0;
    while (processed < max_cells && w->water_q_head != w->water_q_tail) {
        pos3_t p = w->water_q[w->water_q_head];
        w->water_q_head = (w->water_q_head + 1) % WATER_QUEUE_CAP;
        processed++;

        if (p.y < 0 || p.y >= WORLD_HEIGHT) continue;
        if (world_get_block(w, p.x, p.y, p.z) != BLOCK_AIR) continue;
        if (!has_water_source(w, p.x, p.y, p.z)) continue;
        if (chunk_is_overlaid(w, p.x, p.y, p.z)) continue;
        water_fill(w, p.x, p.y, p.z);

        if (world_get_block(w, p.x, p.y - 1, p.z) == BLOCK_AIR) water_enqueue(w, p.x, p.y - 1, p.z);
        if (world_get_block(w, p.x + 1, p.y, p.z) == BLOCK_AIR) water_enqueue(w, p.x + 1, p.y, p.z);
        if (world_get_block(w, p.x - 1, p.y, p.z) == BLOCK_AIR) water_enqueue(w, p.x - 1, p.y, p.z);
        if (world_get_block(w, p.x, p.y, p.z + 1) == BLOCK_AIR) water_enqueue(w, p.x, p.y, p.z + 1);
        if (world_get_block(w, p.x, p.y, p.z - 1) == BLOCK_AIR) water_enqueue(w, p.x, p.y, p.z - 1);
    }
    pthread_rwlock_unlock(&w->map_lock);
}

static void enqueue_water_candidates_in_chunk(world_t *w, int cx, int cy, int cz) {
    int chunk_top_y = (cy + 1) * CHUNK_DIM - 1;
    if (chunk_top_y < 0 || cy * CHUNK_DIM > SEA_LEVEL) return;
    if (chunk_is_overlaid(w, cx * CHUNK_DIM, cy * CHUNK_DIM, cz * CHUNK_DIM)) return;
    int x0 = cx * CHUNK_DIM, x1 = x0 + CHUNK_DIM;
    int y0 = cy * CHUNK_DIM, y1 = y0 + CHUNK_DIM;
    if (y1 > SEA_LEVEL + 1) y1 = SEA_LEVEL + 1;
    int z0 = cz * CHUNK_DIM, z1 = z0 + CHUNK_DIM;
    for (int y = y0; y < y1; y++)
        for (int z = z0; z < z1; z++)
            for (int x = x0; x < x1; x++) {
                if (world_get_block(w, x, y, z) != BLOCK_AIR) continue;
                if (has_water_source(w, x, y, z)) water_enqueue(w, x, y, z);
            }
}

static void cascade_physics(world_t *w, int x, int y, int z) {
    block_t now = world_get_block(w, x, y, z);

    if (now == BLOCK_AIR) {
        cascade_sand(w, x, y, z);
        water_enqueue(w, x, y, z);
    } else if (now == BLOCK_SAND) {
        if (y > 0 && world_get_block(w, x, y - 1, z) == BLOCK_AIR && w->falling) {
            falling_spawn(w->falling, x, y, z, BLOCK_SAND);
            apply_edit(w, x, y, z, BLOCK_AIR);
            water_enqueue(w, x, y, z);
        }
    }
}

void world_set_block(world_t *w, int x, int y, int z, block_t b) {
    if (y < 0 || y >= WORLD_HEIGHT) return;
    pthread_rwlock_wrlock(&w->map_lock);
    block_t prev = world_get_block(w, x, y, z);
    apply_edit(w, x, y, z, b);
    if (b == BLOCK_GLOWSTONE || prev == BLOCK_GLOWSTONE) {
        int cx = x >> CHUNK_BITS, cy = y >> CHUNK_BITS, cz = z >> CHUNK_BITS;
        for (int dcz = -1; dcz <= 1; dcz++)
            for (int dcy = -1; dcy <= 1; dcy++)
                for (int dcx = -1; dcx <= 1; dcx++) {
                    chunk_node_t *n = find_chunk(w, cx + dcx, cy + dcy, cz + dcz);
                    if (n) n->dirty = 1;
                }
    }
    cascade_physics(w, x, y, z);
    pthread_rwlock_unlock(&w->map_lock);
}

#define LIGHTVOL_RADIUS 10

static int lightvol_passes(block_t b) {
    return block_render_class(b) != RCLASS_SOLID;
}

void world_build_light_volume(world_t *w, int ox, int oy, int oz, int dim, uint8_t *out) {
    if (dim <= 0 || dim > WORLD_LIGHTVOL_MAX_DIM || !out) return;

    static uint8_t *blk = NULL;
    static uint8_t *lvl = NULL;
    static int     *q   = NULL;
    if (!blk) {
        size_t maxvol = (size_t)WORLD_LIGHTVOL_MAX_DIM * WORLD_LIGHTVOL_MAX_DIM * WORLD_LIGHTVOL_MAX_DIM;
        blk = malloc(maxvol);
        lvl = malloc(maxvol);
        q   = malloc(maxvol * sizeof *q);
        if (!blk || !lvl || !q) { free(blk); free(lvl); free(q); blk = lvl = NULL; q = NULL; return; }
    }

    int vol = dim * dim * dim;
    memset(blk, 0, (size_t)vol);
    memset(lvl, 0, (size_t)vol);
    memset(out, 0, (size_t)vol);

    pthread_rwlock_rdlock(&w->map_lock);
    int cx0 = ox >> CHUNK_BITS, cx1 = (ox + dim - 1) >> CHUNK_BITS;
    int cy0 = oy >> CHUNK_BITS, cy1 = (oy + dim - 1) >> CHUNK_BITS;
    int cz0 = oz >> CHUNK_BITS, cz1 = (oz + dim - 1) >> CHUNK_BITS;
    for (int ccx = cx0; ccx <= cx1; ccx++)
        for (int ccy = cy0; ccy <= cy1; ccy++)
            for (int ccz = cz0; ccz <= cz1; ccz++) {
                chunk_node_t *n = find_chunk(w, ccx, ccy, ccz);
                if (!n) continue;
                int bx = ccx * CHUNK_DIM, by = ccy * CHUNK_DIM, bz = ccz * CHUNK_DIM;
                for (int lx = 0; lx < CHUNK_DIM; lx++) {
                    int gx = bx + lx - ox; if (gx < 0 || gx >= dim) continue;
                    for (int ly = 0; ly < CHUNK_DIM; ly++) {
                        int gy = by + ly - oy; if (gy < 0 || gy >= dim) continue;
                        for (int lz = 0; lz < CHUNK_DIM; lz++) {
                            int gz = bz + lz - oz; if (gz < 0 || gz >= dim) continue;
                            blk[(gz * dim + gy) * dim + gx] = chunk_get(&n->data, lx, ly, lz);
                        }
                    }
                }
            }
    pthread_rwlock_unlock(&w->map_lock);

    int qh = 0, qt = 0;
    for (int i = 0; i < vol; i++)
        if (block_light_emission(blk[i]) > 0) { lvl[i] = LIGHTVOL_RADIUS; q[qt++] = i; }
    if (qt == 0) return;

    static const int OFF[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    while (qh < qt) {
        int idx = q[qh++];
        int gx = idx % dim;
        int gy = (idx / dim) % dim;
        int gz = idx / (dim * dim);
        int cur = lvl[idx];
        if (cur <= 1) continue;
        for (int k = 0; k < 6; k++) {
            int nx = gx + OFF[k][0], ny = gy + OFF[k][1], nz = gz + OFF[k][2];
            if (nx < 0 || ny < 0 || nz < 0 || nx >= dim || ny >= dim || nz >= dim) continue;
            int nidx = (nz * dim + ny) * dim + nx;
            if (!lightvol_passes(blk[nidx])) continue;
            if (lvl[nidx] < cur - 1) {
                lvl[nidx] = (uint8_t)(cur - 1);
                q[qt++] = nidx;
            }
        }
    }

    for (int i = 0; i < vol; i++) {
        int v = lvl[i];
        if (v <= 0) continue;
        float dist = (float)(LIGHTVOL_RADIUS - v) / (float)LIGHTVOL_RADIUS;
        float f = 1.0f - dist * dist;
        int iv = (int)(f * 255.0f + 0.5f);
        out[i] = (uint8_t)(iv < 0 ? 0 : (iv > 255 ? 255 : iv));
    }
}

int world_block_light_at(const world_t *w, int x, int y, int z) {
    const int R = 7;
    int best = 0;
    for (int dy = -R; dy <= R; dy++)
        for (int dz = -R; dz <= R; dz++)
            for (int dx = -R; dx <= R; dx++) {
                int e = block_light_emission(world_get_block(w, x + dx, y + dy, z + dz));
                if (e <= 0) continue;
                int lv = e - (abs(dx) + abs(dy) + abs(dz));
                if (lv > best) best = lv;
            }
    return best > 15 ? 15 : best;
}

static void apply_deltas_to_chunk(const world_t *w, chunk_node_t *n) {
    int x0 = n->cx * CHUNK_DIM, x1 = x0 + CHUNK_DIM;
    int y0 = n->cy * CHUNK_DIM, y1 = y0 + CHUNK_DIM;
    int z0 = n->cz * CHUNK_DIM, z1 = z0 + CHUNK_DIM;
    for (int i = 0; i < w->delta_count; i++) {
        const delta_t *d = &w->deltas[i];
        if (d->x >= x0 && d->x < x1 &&
            d->y >= y0 && d->y < y1 &&
            d->z >= z0 && d->z < z1) {
            chunk_set(&n->data, d->x - x0, d->y - y0, d->z - z0, d->block);
        }
    }
}

static void gen_chunk_into(uint32_t seed, int cx, int cy, int cz, chunk_t *out) {
    chunk_init(out);
    int origin_x = cx * CHUNK_DIM;
    int origin_y = cy * CHUNK_DIM;
    int origin_z = cz * CHUNK_DIM;
    int     colh[CHUNK_DIM][CHUNK_DIM];
    biome_t colb[CHUNK_DIM][CHUNK_DIM];
    for (int z = 0; z < CHUNK_DIM; z++)
        for (int x = 0; x < CHUNK_DIM; x++) {
            colh[z][x] = world_height_at(seed, origin_x + x, origin_z + z);
            colb[z][x] = world_biome_at(seed, origin_x + x, origin_z + z);
        }
    for (int y = 0; y < CHUNK_DIM; y++)
        for (int z = 0; z < CHUNK_DIM; z++)
            for (int x = 0; x < CHUNK_DIM; x++)
                chunk_set(out, x, y, z,
                          gen_block_col(seed, origin_x + x, origin_y + y, origin_z + z,
                                        colh[z][x], colb[z][x]));
}

static void mesh_attribs(void) {
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(mesh_vertex_t), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(mesh_vertex_t), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(mesh_vertex_t), (void *)(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(mesh_vertex_t), (void *)(7 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(mesh_vertex_t), (void *)(10 * sizeof(float)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(mesh_vertex_t), (void *)(11 * sizeof(float)));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(mesh_vertex_t), (void *)(12 * sizeof(float)));
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, sizeof(mesh_vertex_t), (void *)(13 * sizeof(float)));
    glEnableVertexAttribArray(7);
}

static void upload_chunk_mesh(chunk_node_t *n, const mesh_buf_t *mesh) {
    if (!n->has_gpu) {
        glGenVertexArrays(1, &n->vao);
        glGenBuffers(1, &n->vbo);
        glGenBuffers(1, &n->ebo);
        n->has_gpu = 1;
    }
    glBindVertexArray(n->vao);
    glBindBuffer(GL_ARRAY_BUFFER, n->vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)((size_t)mesh->vert_count * sizeof(mesh_vertex_t)),
                 mesh->verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, n->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 (GLsizeiptr)((size_t)mesh->idx_count * sizeof(unsigned int)),
                 mesh->indices, GL_STATIC_DRAW);
    mesh_attribs();
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    n->idx_count        = mesh->idx_count;
    n->opaque_idx_count = mesh->opaque_idx_count;
    n->glass_idx_count  = mesh->glass_idx_count;
}

static void compute_content_bounds(chunk_node_t *n) {
    int minx = CHUNK_DIM, miny = CHUNK_DIM, minz = CHUNK_DIM;
    int maxx = -1, maxy = -1, maxz = -1;
    for (int y = 0; y < CHUNK_DIM; y++)
        for (int z = 0; z < CHUNK_DIM; z++)
            for (int x = 0; x < CHUNK_DIM; x++) {
                if (chunk_get(&n->data, x, y, z) == BLOCK_AIR) continue;
                if (x < minx) minx = x;  if (x > maxx) maxx = x;
                if (y < miny) miny = y;  if (y > maxy) maxy = y;
                if (z < minz) minz = z;  if (z > maxz) maxz = z;
            }
    if (maxx < 0) { n->has_content = 0; return; }
    n->has_content = 1;
    n->cmin_x = minx; n->cmin_y = miny; n->cmin_z = minz;
    n->cmax_x = maxx; n->cmax_y = maxy; n->cmax_z = maxz;
}

static void mesh_chunk(world_t *w, chunk_node_t *n) {
    mesh_buf_t mesh;
    mesh_init(&mesh);
    chunk_to_mesh(w, n->cx, n->cy, n->cz, &mesh);
    upload_chunk_mesh(n, &mesh);
    mesh_free(&mesh);
    compute_content_bounds(n);
}

void world_remesh_chunk(world_t *w, int cx, int cy, int cz) {
    chunk_node_t *n = find_chunk(w, cx, cy, cz);
    if (n && !n->dormant) n->dirty = 1;
}

void world_mark_all_dirty(world_t *w) {
    for (int i = 0; i < WORLD_BUCKETS; i++) {
        for (chunk_node_t *n = w->buckets[i]; n; n = n->next) {
            if (!n->dormant && !n->generating) n->dirty = 1;
        }
    }
}

static void *mesh_worker(void *arg) {
    world_t *w = (world_t *)arg;
    for (;;) {
        pthread_mutex_lock(&w->job_mutex);
        while (w->job_head == w->job_tail && !w->threads_quit)
            pthread_cond_wait(&w->job_cv, &w->job_mutex);
        if (w->job_head == w->job_tail && w->threads_quit) {
            pthread_mutex_unlock(&w->job_mutex);
            return NULL;
        }
        int cx = w->job_ring[w->job_head].cx;
        int cy = w->job_ring[w->job_head].cy;
        int cz = w->job_ring[w->job_head].cz;
        w->job_head = (w->job_head + 1) % MESH_QUEUE_CAP;
        pthread_mutex_unlock(&w->job_mutex);

        mesh_buf_t mesh;
        mesh_init(&mesh);
        pthread_rwlock_rdlock(&w->map_lock);
        if (find_chunk(w, cx, cy, cz)) chunk_to_mesh(w, cx, cy, cz, &mesh);
        pthread_rwlock_unlock(&w->map_lock);

        pthread_mutex_lock(&w->res_mutex);
        w->res_ring[w->res_tail].cx   = cx;
        w->res_ring[w->res_tail].cy   = cy;
        w->res_ring[w->res_tail].cz   = cz;
        w->res_ring[w->res_tail].mesh = mesh;
        w->res_tail = (w->res_tail + 1) % MESH_QUEUE_CAP;
        pthread_mutex_unlock(&w->res_mutex);
    }
}

static void *gen_worker(void *arg) {
    world_t *w = (world_t *)arg;
    for (;;) {
        pthread_mutex_lock(&w->gen_job_mutex);
        while (w->gen_job_head == w->gen_job_tail && !w->threads_quit)
            pthread_cond_wait(&w->gen_job_cv, &w->gen_job_mutex);
        if (w->gen_job_head == w->gen_job_tail && w->threads_quit) {
            pthread_mutex_unlock(&w->gen_job_mutex);
            return NULL;
        }
        int cx = w->gen_job_ring[w->gen_job_head].cx;
        int cy = w->gen_job_ring[w->gen_job_head].cy;
        int cz = w->gen_job_ring[w->gen_job_head].cz;
        w->gen_job_head = (w->gen_job_head + 1) % MESH_QUEUE_CAP;
        pthread_mutex_unlock(&w->gen_job_mutex);

        chunk_t *data = malloc(sizeof *data);
        if (data) gen_chunk_into(w->seed, cx, cy, cz, data);

        pthread_mutex_lock(&w->gen_res_mutex);
        w->gen_res_ring[w->gen_res_tail].cx   = cx;
        w->gen_res_ring[w->gen_res_tail].cy   = cy;
        w->gen_res_ring[w->gen_res_tail].cz   = cz;
        w->gen_res_ring[w->gen_res_tail].data = data;
        w->gen_res_tail = (w->gen_res_tail + 1) % MESH_QUEUE_CAP;
        pthread_mutex_unlock(&w->gen_res_mutex);
    }
}

static void enqueue_dirty_chunks(world_t *w) {
    int woke = 0;
    for (int i = 0; i < WORLD_BUCKETS; i++) {
        for (chunk_node_t *n = w->buckets[i]; n; n = n->next) {
            if (!n->dirty || n->dormant || n->queued || n->generating) continue;
            if (w->pending >= MESH_QUEUE_CAP - 1) goto done;
            n->queued = 1;
            n->dirty  = 0;
            pthread_mutex_lock(&w->job_mutex);
            w->job_ring[w->job_tail].cx = n->cx;
            w->job_ring[w->job_tail].cy = n->cy;
            w->job_ring[w->job_tail].cz = n->cz;
            w->job_tail = (w->job_tail + 1) % MESH_QUEUE_CAP;
            pthread_mutex_unlock(&w->job_mutex);
            w->pending++;
            woke = 1;
        }
    }
done:
    if (woke) pthread_cond_broadcast(&w->job_cv);
}

static int gen_enqueue(world_t *w, int cx, int cy, int cz) {
    if (w->gen_pending >= MESH_QUEUE_CAP - 1) return 0;
    pthread_mutex_lock(&w->gen_job_mutex);
    w->gen_job_ring[w->gen_job_tail].cx = cx;
    w->gen_job_ring[w->gen_job_tail].cy = cy;
    w->gen_job_ring[w->gen_job_tail].cz = cz;
    w->gen_job_tail = (w->gen_job_tail + 1) % MESH_QUEUE_CAP;
    pthread_mutex_unlock(&w->gen_job_mutex);
    w->gen_pending++;
    pthread_cond_signal(&w->gen_job_cv);
    return 1;
}

static int collect_meshes(world_t *w, int budget) {
    int taken = 0;
    while (taken < budget) {
        pthread_mutex_lock(&w->res_mutex);
        if (w->res_head == w->res_tail) { pthread_mutex_unlock(&w->res_mutex); break; }
        int cx = w->res_ring[w->res_head].cx;
        int cy = w->res_ring[w->res_head].cy;
        int cz = w->res_ring[w->res_head].cz;
        mesh_buf_t mesh = w->res_ring[w->res_head].mesh;
        w->res_head = (w->res_head + 1) % MESH_QUEUE_CAP;
        pthread_mutex_unlock(&w->res_mutex);

        w->pending--;
        chunk_node_t *n = find_chunk(w, cx, cy, cz);
        if (n) {
            if (!n->dormant) {
                upload_chunk_mesh(n, &mesh);
                compute_content_bounds(n);
            }
            n->queued = 0;
        }
        mesh_free(&mesh);
        taken++;
    }
    return taken;
}

static void mesh_all_dirty(world_t *w) {
    for (int i = 0; i < WORLD_BUCKETS; i++)
        for (chunk_node_t *n = w->buckets[i]; n; n = n->next)
            if (n->dirty && !n->dormant && !n->generating) { mesh_chunk(w, n); n->dirty = 0; }
}

void world_pump_meshing(world_t *w) {
    if (w->gen_worker_count > 0) collect_generated(w, GEN_FINALIZE_BUDGET);
    if (w->worker_count == 0) { mesh_all_dirty(w); return; }
    enqueue_dirty_chunks(w);
    collect_meshes(w, MESH_UPLOAD_BUDGET);
}

void world_wait_idle(world_t *w) {
    if (w->worker_count == 0 && w->gen_worker_count == 0) { mesh_all_dirty(w); return; }
    for (;;) {
        if (w->gen_worker_count > 0) collect_generated(w, MESH_QUEUE_CAP);
        int progressed = 0;
        if (w->worker_count > 0) {
            enqueue_dirty_chunks(w);
            progressed = collect_meshes(w, MESH_QUEUE_CAP);
        } else {
            mesh_all_dirty(w);
        }
        if (w->gen_pending == 0 && w->pending == 0) break;
        if (progressed == 0) {
            struct timespec ts = { 0, 1000000 };
            nanosleep(&ts, NULL);
        }
    }
}

int world_work_remaining(const world_t *w) {
    int remaining = w->gen_pending + w->pending;
    for (int i = 0; i < WORLD_BUCKETS; i++)
        for (const chunk_node_t *n = w->buckets[i]; n; n = n->next)
            if (n->dirty && !n->dormant && !n->generating && !n->queued) remaining++;
    return remaining;
}

static void world_start_workers(world_t *w) {
    pthread_rwlock_init(&w->map_lock, NULL);
    pthread_mutex_init(&w->job_mutex, NULL);
    pthread_mutex_init(&w->res_mutex, NULL);
    pthread_cond_init(&w->job_cv, NULL);
    pthread_mutex_init(&w->gen_job_mutex, NULL);
    pthread_mutex_init(&w->gen_res_mutex, NULL);
    pthread_cond_init(&w->gen_job_cv, NULL);
    w->threads_quit = 0;
    w->worker_count = 0;
    for (int i = 0; i < MESH_WORKERS; i++) {
        if (pthread_create(&w->workers[i], NULL, mesh_worker, w) != 0) break;
        w->worker_count++;
    }
    if (w->worker_count == 0)
        fprintf(stderr, "world: no mesh worker threads; meshing on the main thread\n");
    w->gen_worker_count = 0;
    for (int i = 0; i < GEN_WORKERS; i++) {
        if (pthread_create(&w->gen_workers[i], NULL, gen_worker, w) != 0) break;
        w->gen_worker_count++;
    }
    if (w->gen_worker_count == 0)
        fprintf(stderr, "world: no gen worker threads; generation on the main thread\n");
}

static void world_stop_workers(world_t *w) {
    pthread_mutex_lock(&w->job_mutex);
    w->threads_quit = 1;
    pthread_cond_broadcast(&w->job_cv);
    pthread_mutex_unlock(&w->job_mutex);
    pthread_mutex_lock(&w->gen_job_mutex);
    w->threads_quit = 1;
    pthread_cond_broadcast(&w->gen_job_cv);
    pthread_mutex_unlock(&w->gen_job_mutex);
    for (int i = 0; i < w->worker_count; i++) pthread_join(w->workers[i], NULL);
    for (int i = 0; i < w->gen_worker_count; i++) pthread_join(w->gen_workers[i], NULL);
    while (w->res_head != w->res_tail) {
        mesh_buf_t m = w->res_ring[w->res_head].mesh;
        w->res_head = (w->res_head + 1) % MESH_QUEUE_CAP;
        mesh_free(&m);
    }
    while (w->gen_res_head != w->gen_res_tail) {
        free(w->gen_res_ring[w->gen_res_head].data);
        w->gen_res_head = (w->gen_res_head + 1) % MESH_QUEUE_CAP;
    }
    pthread_cond_destroy(&w->job_cv);
    pthread_mutex_destroy(&w->job_mutex);
    pthread_mutex_destroy(&w->res_mutex);
    pthread_cond_destroy(&w->gen_job_cv);
    pthread_mutex_destroy(&w->gen_job_mutex);
    pthread_mutex_destroy(&w->gen_res_mutex);
    pthread_rwlock_destroy(&w->map_lock);
}

static void stamp_vein(world_t *w, int cx, int cy, int cz, block_t ore, int radius) {
    for (int dy = -radius; dy <= radius; dy++)
        for (int dz = -radius; dz <= radius; dz++)
            for (int dx = -radius; dx <= radius; dx++) {
                if (dx*dx + dy*dy + dz*dz > radius * radius) continue;
                int wx = cx + dx, wy = cy + dy, wz = cz + dz;
                if (world_get_block(w, wx, wy, wz) != BLOCK_STONE) continue;
                uint32_t h = hash3(w->seed + 0xABCDu, wx, wy, wz);
                if ((h & 0xFFu) < 200) {
                    world_set_block_gen(w, wx, wy, wz, ore);
                }
            }
}

static void place_veins_in_chunk(world_t *w, int cx, int cy, int cz) {
    uint32_t base = hash3(w->seed + SALT_VEIN, cx, cy, cz);
    for (int attempt = 0; attempt < 4; attempt++) {
        uint32_t h = hash3(base, attempt, 0, 0);
        int lx = (int)((h >>  0) & 15u);
        int ly = (int)((h >>  4) & 15u);
        int lz = (int)((h >>  8) & 15u);
        int wx = cx * CHUNK_DIM + lx;
        int wy = cy * CHUNK_DIM + ly;
        int wz = cz * CHUNK_DIM + lz;
        uint32_t roll = (h >> 12) & 0x3FFu;

        block_t ore = BLOCK_STONE;
        int radius = 0;
        if      (wy <= 10 && roll < 35u)               { ore = BLOCK_DIAMOND_ORE; radius = 1; }
        else if (wy >= 5 && wy <= 30 && roll < 110u)   { ore = BLOCK_IRON_ORE;    radius = 2; }
        else if (wy >= 5 && wy <= 55 && roll < 220u)   { ore = BLOCK_COAL_ORE;    radius = 2; }
        else continue;

        stamp_vein(w, wx, wy, wz, ore, radius);
    }
}

static void leaf_at(world_t *w, int x, int y, int z, block_t leaf) {
    if (world_get_block(w, x, y, z) == BLOCK_AIR) gen_write_keep_player(w, x, y, z, leaf);
}

static void leaf_disc(world_t *w, int cx, int y, int cz, int r, int trim, block_t leaf) {
    for (int dz = -r; dz <= r; dz++)
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dz * dz > r * r + trim) continue;
            leaf_at(w, cx + dx, y, cz + dz, leaf);
        }
}

static void stamp_tree(world_t *w, int wx, int wy, int wz, block_t leaf, int kind) {
    uint32_t rh = hash2(w->seed + SALT_TREE, wx * 13 + 1, wz * 13 + 1);
    int trunk;
    switch (kind) {
        case 1: trunk = 6 + (int)(rh % 2u); break;
        case 2: trunk = 4 + (int)(rh % 2u); break;
        case 3: trunk = 4; break;
        case 4: trunk = 7 + (int)(rh % 3u); break;
        default: trunk = 4 + (int)(rh % 2u); break;
    }
    for (int i = 0; i < trunk; i++)
        if (world_get_block(w, wx, wy + i, wz) == BLOCK_AIR)
            gen_write_keep_player(w, wx, wy + i, wz, BLOCK_WOOD);
    int top = wy + trunk;

    if (kind == 1) {
        for (int dy = 2; dy <= trunk; dy += 1) {
            int r = (trunk - dy) / 2 + 1;
            if (r > 2) r = 2;
            leaf_disc(w, wx, wy + dy, wz, r, 0, leaf);
        }
        leaf_at(w, wx, top, wz, leaf);
        leaf_at(w, wx, top + 1, wz, leaf);
    } else if (kind == 2) {
        leaf_disc(w, wx, top - 1, wz, 3, 0, leaf);
        leaf_disc(w, wx, top,     wz, 2, 0, leaf);
    } else if (kind == 4) {
        leaf_disc(w, wx, top - 3, wz, 3, 1, leaf);
        leaf_disc(w, wx, top - 2, wz, 3, 1, leaf);
        leaf_disc(w, wx, top - 1, wz, 2, 1, leaf);
        leaf_disc(w, wx, top,     wz, 1, 0, leaf);
        leaf_at(w, wx, top + 1, wz, leaf);
    } else if (kind == 3) {
        leaf_disc(w, wx, top - 1, wz, 3, 1, leaf);
        leaf_disc(w, wx, top,     wz, 2, 0, leaf);
        leaf_at(w, wx + 3, top - 2, wz, leaf);
        leaf_at(w, wx - 3, top - 2, wz, leaf);
        leaf_at(w, wx, top - 2, wz + 3, leaf);
        leaf_at(w, wx, top - 2, wz - 3, leaf);
    } else {
        leaf_disc(w, wx, top - 2, wz, 2, -1, leaf);
        leaf_disc(w, wx, top - 1, wz, 2, -1, leaf);
        leaf_disc(w, wx, top,     wz, 1, 0, leaf);
        leaf_at(w, wx, top + 1, wz, leaf);
    }
}

static void place_trees_for_column(world_t *w, int cx, int cz) {
    for (int dcz = -1; dcz <= 1; dcz++) {
        for (int dcx = -1; dcx <= 1; dcx++) {
            int x0 = (cx + dcx) * CHUNK_DIM;
            int z0 = (cz + dcz) * CHUNK_DIM;
            for (int lz = 0; lz < CHUNK_DIM; lz++) {
                for (int lx = 0; lx < CHUNK_DIM; lx++) {
                    int x = x0 + lx;
                    int z = z0 + lz;
                    biome_t bm = world_biome_at(w->seed, x, z);
                    int gh = world_height_at(w->seed, x, z);
                    if (gh <= SEA_LEVEL + 1 || gh + 12 >= WORLD_HEIGHT) continue;

                    int dsum = 0, dn = 0;
                    for (int ddz = -16; ddz <= 16; ddz += 8)
                        for (int ddx = -16; ddx <= 16; ddx += 8) {
                            dsum += BIOMES[world_biome_at(w->seed, x + ddx, z + ddz)].tree_density;
                            dn++;
                        }
                    int density = dsum / dn;

                    uint32_t th = hash2(w->seed + SALT_TREE, x, z);
                    if (density > 0 && (int)(th % 1000u) < density) {
                        if (world_get_block(w, x, gh, z) == BIOMES[bm].surface)
                            stamp_tree(w, x, gh + 1, z, BIOMES[bm].leaf, BIOMES[bm].tree_kind);
                        continue;
                    }

                    int gd = BIOMES[bm].grass_density;
                    if (gd > 0) {
                        uint32_t grh = hash2(w->seed + SALT_GRASS, x, z);
                        if ((int)(grh % 1000u) < gd &&
                            world_get_block(w, x, gh, z) == BLOCK_GRASS &&
                            world_get_block(w, x, gh + 1, z) == BLOCK_AIR)
                            gen_write_keep_player(w, x, gh + 1, z, BLOCK_TALL_GRASS);
                    }
                }
            }
        }
    }
}

static void mark_chunk_neighbors_dirty(world_t *w, int cx, int cy, int cz) {
    static const int off[7][3] = {
        {0,0,0}, {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1},
    };
    for (int i = 0; i < 7; i++) {
        chunk_node_t *n = find_chunk(w, cx + off[i][0], cy + off[i][1], cz + off[i][2]);
        if (n && !n->dormant && !n->generating) n->dirty = 1;
    }
}

static int column_fully_generated(const world_t *w, int cx, int cz) {
    for (int cy = 0; cy < WORLD_HEIGHT_CHUNKS; cy++) {
        const chunk_node_t *n = find_chunk(w, cx, cy, cz);
        if (!n || n->generating) return 0;
    }
    return 1;
}

static void mark_column_and_neighbors_dirty(world_t *w, int cx, int cz) {
    for (int dcz = -1; dcz <= 1; dcz++)
        for (int dcx = -1; dcx <= 1; dcx++)
            for (int cy = 0; cy < WORLD_HEIGHT_CHUNKS; cy++) {
                chunk_node_t *n = find_chunk(w, cx + dcx, cy, cz + dcz);
                if (n && !n->dormant && !n->generating) n->dirty = 1;
            }
}

static void finalize_chunk(world_t *w, chunk_node_t *n, const chunk_t *data) {
    n->data = *data;
    place_veins_in_chunk(w, n->cx, n->cy, n->cz);
    apply_deltas_to_chunk(w, n);
    chunk_edit_t *ce = edit_chunk_find(w, n->cx, n->cy, n->cz);
    if (ce && ce->overlay) n->data = *ce->overlay;
    enqueue_water_candidates_in_chunk(w, n->cx, n->cy, n->cz);
    n->generating = 0;
    n->dirty = n->dormant ? 0 : 1;
    mark_chunk_neighbors_dirty(w, n->cx, n->cy, n->cz);
    if (column_fully_generated(w, n->cx, n->cz)) {
        place_trees_for_column(w, n->cx, n->cz);
        mark_column_and_neighbors_dirty(w, n->cx, n->cz);
    }
}

static int collect_generated(world_t *w, int budget) {
    pthread_rwlock_wrlock(&w->map_lock);
    int taken = 0;
    while (taken < budget) {
        pthread_mutex_lock(&w->gen_res_mutex);
        if (w->gen_res_head == w->gen_res_tail) {
            pthread_mutex_unlock(&w->gen_res_mutex);
            break;
        }
        int cx = w->gen_res_ring[w->gen_res_head].cx;
        int cy = w->gen_res_ring[w->gen_res_head].cy;
        int cz = w->gen_res_ring[w->gen_res_head].cz;
        chunk_t *data = w->gen_res_ring[w->gen_res_head].data;
        w->gen_res_head = (w->gen_res_head + 1) % MESH_QUEUE_CAP;
        pthread_mutex_unlock(&w->gen_res_mutex);

        w->gen_pending--;
        chunk_node_t *n = find_chunk(w, cx, cy, cz);
        if (n && n->generating) {
            if (data) {
                finalize_chunk(w, n, data);
            } else {
                chunk_t fallback;
                gen_chunk_into(w->seed, cx, cy, cz, &fallback);
                finalize_chunk(w, n, &fallback);
            }
        }
        free(data);
        taken++;
    }
    pthread_rwlock_unlock(&w->map_lock);
    return taken;
}

static void load_chunk_at(world_t *w, int cx, int cy, int cz) {
    chunk_node_t *existing = find_chunk(w, cx, cy, cz);
    if (existing) {
        if (existing->dormant) {
            existing->dormant = 0;
            existing->dirty = 1;
            existing->last_touched = ++w->seq;
        }
        return;
    }
    chunk_node_t *n = insert_chunk(w, cx, cy, cz);
    if (!n) return;
    n->last_touched = ++w->seq;
    if (w->gen_worker_count > 0) {
        n->generating = 1;
        if (!gen_enqueue(w, cx, cy, cz)) {
            chunk_t data;
            gen_chunk_into(w->seed, cx, cy, cz, &data);
            finalize_chunk(w, n, &data);
        }
    } else {
        chunk_t data;
        gen_chunk_into(w->seed, cx, cy, cz, &data);
        finalize_chunk(w, n, &data);
    }
}

static void load_column(world_t *w, int cx, int cz) {
    for (int cy = 0; cy < WORLD_HEIGHT_CHUNKS; cy++) {
        load_chunk_at(w, cx, cy, cz);
    }
}

static void unload_column(world_t *w, int cx, int cz) {
    for (int cy = 0; cy < WORLD_HEIGHT_CHUNKS; cy++) {
        mark_chunk_dormant(w, cx, cy, cz);
    }
}

static int column_active(const world_t *w, int cx, int cz) {
    chunk_node_t *n = find_chunk(w, cx, 0, cz);
    return n && !n->dormant;
}

void world_update_streaming(world_t *w, float player_x, float player_z) {
    int new_cx = floor_div((int)player_x, CHUNK_DIM);
    int new_cz = floor_div((int)player_z, CHUNK_DIM);

    if (w->initialized && new_cx == w->center_cx && new_cz == w->center_cz) return;

    pthread_rwlock_wrlock(&w->map_lock);

    int old_cx = w->center_cx;
    int old_cz = w->center_cz;
    w->center_cx = new_cx;
    w->center_cz = new_cz;

    if (w->initialized) {
        int sweep = w->radius + 1;
        for (int cz = old_cz - sweep; cz <= old_cz + sweep; cz++) {
            for (int cx = old_cx - sweep; cx <= old_cx + sweep; cx++) {
                if (iabs(cx - new_cx) > w->radius || iabs(cz - new_cz) > w->radius) {
                    if (column_active(w, cx, cz)) unload_column(w, cx, cz);
                }
            }
        }
    }
    w->initialized = 1;

    for (int cz = new_cz - w->radius; cz <= new_cz + w->radius; cz++) {
        for (int cx = new_cx - w->radius; cx <= new_cx + w->radius; cx++) {
            if (!column_active(w, cx, cz)) {
                load_column(w, cx, cz);
            }
        }
    }

    enforce_chunk_cap(w);

    pthread_rwlock_unlock(&w->map_lock);
}

typedef struct { float a, b, c, d; } plane_t;
typedef struct { plane_t planes[6]; } frustum_t;

static frustum_t extract_frustum(mat4 m) {
    frustum_t f;
    f.planes[0] = (plane_t){m.m[3]+m.m[0], m.m[7]+m.m[4], m.m[11]+m.m[8],  m.m[15]+m.m[12]};
    f.planes[1] = (plane_t){m.m[3]-m.m[0], m.m[7]-m.m[4], m.m[11]-m.m[8],  m.m[15]-m.m[12]};
    f.planes[2] = (plane_t){m.m[3]+m.m[1], m.m[7]+m.m[5], m.m[11]+m.m[9],  m.m[15]+m.m[13]};
    f.planes[3] = (plane_t){m.m[3]-m.m[1], m.m[7]-m.m[5], m.m[11]-m.m[9],  m.m[15]-m.m[13]};
    f.planes[4] = (plane_t){m.m[3]+m.m[2], m.m[7]+m.m[6], m.m[11]+m.m[10], m.m[15]+m.m[14]};
    f.planes[5] = (plane_t){m.m[3]-m.m[2], m.m[7]-m.m[6], m.m[11]-m.m[10], m.m[15]-m.m[14]};
    return f;
}

static int aabb_in_frustum(const frustum_t *f, float min_x, float min_y, float min_z,
                           float max_x, float max_y, float max_z) {
    for (int i = 0; i < 6; i++) {
        plane_t p = f->planes[i];
        float px = (p.a > 0.0f) ? max_x : min_x;
        float py = (p.b > 0.0f) ? max_y : min_y;
        float pz = (p.c > 0.0f) ? max_z : min_z;
        if (p.a * px + p.b * py + p.c * pz + p.d < 0.0f) return 0;
    }
    return 1;
}

world_t *world_create(uint32_t seed, int radius) {
    world_t *w = calloc(1, sizeof *w);
    if (!w) return NULL;
    w->seed   = seed;
    w->radius = radius;
    w->sun_dx = 0.0f; w->sun_dy = 1.0f; w->sun_dz = 0.0f;
    w->difficulty = 2; w->mob_spawning = 1; w->keep_inventory = 0; w->natural_regen = 1;
    w->pvp = 1; w->fall_damage = 1; w->hunger_enabled = 1; w->daylight_cycle = 1;
    w->mob_griefing = 1; w->weather_state = 3;
    w->spawn_rate = 1.0f; w->smelt_mult = 1.0f;
    w->dawn_time = 0.27f;
    world_start_workers(w);
    return w;
}

void world_destroy(world_t *w) {
    if (!w) return;
    world_stop_workers(w);
    for (int i = 0; i < WORLD_BUCKETS; i++) {
        chunk_node_t *n = w->buckets[i];
        while (n) {
            chunk_node_t *next = n->next;
            free_chunk_node(n);
            n = next;
        }
    }
    for (int i = 0; i < WORLD_BUCKETS; i++) {
        chunk_edit_t *e = w->edit_buckets[i];
        while (e) {
            chunk_edit_t *next = e->next;
            free(e->overlay);
            free(e);
            e = next;
        }
    }
    free(w->deltas);
    free(w->delta_hash);
    free(w);
}

#define SAVE_MAGIC       0x444C5742u
#define SAVE_VERSION     8u
#define SAVE_VERSION_MIN 5u

static int write_u32(FILE *f, uint32_t v)   { return fwrite(&v, 4, 1, f) == 1; }
static int write_i32(FILE *f, int32_t v)    { return fwrite(&v, 4, 1, f) == 1; }
static int write_f32(FILE *f, float v)      { return fwrite(&v, 4, 1, f) == 1; }
static int write_u8 (FILE *f, uint8_t v)    { return fwrite(&v, 1, 1, f) == 1; }
static int write_u16(FILE *f, uint16_t v)   { return fwrite(&v, 2, 1, f) == 1; }
static int write_i16(FILE *f, int16_t v)    { return fwrite(&v, 2, 1, f) == 1; }
static int read_u32 (FILE *f, uint32_t *v)  { return fread(v, 4, 1, f) == 1; }
static int read_i32 (FILE *f, int32_t *v)   { return fread(v, 4, 1, f) == 1; }
static int read_f32 (FILE *f, float *v)     { return fread(v, 4, 1, f) == 1; }
static int read_u8  (FILE *f, uint8_t *v)   { return fread(v, 1, 1, f) == 1; }
static int read_u16 (FILE *f, uint16_t *v)  { return fread(v, 2, 1, f) == 1; }
static int read_i16 (FILE *f, int16_t *v)   { return fread(v, 2, 1, f) == 1; }

static int write_slot(FILE *f, save_slot_t s) {
    return write_u16(f, s.id) && write_i16(f, s.count) && write_u16(f, s.durability);
}
static int read_slot(FILE *f, save_slot_t *s) {
    return read_u16(f, &s->id) && read_i16(f, &s->count) && read_u16(f, &s->durability);
}

static world_section_io_t g_entity_io  = { NULL, NULL };
static world_section_io_t g_station_io = { NULL, NULL };
static world_section_io_t g_players_io = { NULL, NULL };
void world_set_entity_io(world_section_io_t io)  { g_entity_io = io; }
void world_set_station_io(world_section_io_t io) { g_station_io = io; }
void world_set_players_io(world_section_io_t io)  { g_players_io = io; }

void world_foreach_edit(const world_t *w,
                        void (*cb)(int x, int y, int z, block_t b, void *ud), void *ud) {
    if (!w || !cb) return;
    for (int i = 0; i < w->delta_count; i++)
        cb(w->deltas[i].x, w->deltas[i].y, w->deltas[i].z, w->deltas[i].block, ud);
}

int world_save(const world_t *w, const save_player_t *p, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "world_save: fopen('%s') failed\n", path); return 0; }
    int ok = 1;
    ok &= write_u32(f, SAVE_MAGIC);
    ok &= write_u32(f, SAVE_VERSION);
    ok &= write_u32(f, w->seed);
    ok &= (fwrite(w->name, 1, WORLD_NAME_MAX, f) == WORLD_NAME_MAX);
    ok &= write_i32(f, (int32_t)w->gamemode);
    ok &= write_i32(f, (int32_t)w->allow_commands);
    ok &= write_u8(f, w->difficulty);
    ok &= write_u8(f, w->mob_spawning);
    ok &= write_u8(f, w->keep_inventory);
    ok &= write_u8(f, w->natural_regen);
    ok &= write_u8(f, w->pvp);
    ok &= write_u8(f, w->fall_damage);
    ok &= write_u8(f, w->hunger_enabled);
    ok &= write_u8(f, w->daylight_cycle);
    ok &= write_u8(f, w->mob_griefing);
    ok &= write_u8(f, w->weather_state);
    ok &= write_f32(f, w->spawn_rate);
    ok &= write_f32(f, w->smelt_mult);
    ok &= write_i32(f, w->spawn_x);
    ok &= write_i32(f, w->spawn_y);
    ok &= write_i32(f, w->spawn_z);
    ok &= write_f32(f, p->pos_x);
    ok &= write_f32(f, p->pos_y);
    ok &= write_f32(f, p->pos_z);
    ok &= write_f32(f, p->yaw);
    ok &= write_f32(f, p->pitch);
    ok &= write_i32(f, (int32_t)p->mode);
    ok &= write_u8 (f, (uint8_t)p->selected_block);
    ok &= write_f32(f, p->world_time);
    ok &= write_u32(f, (uint32_t)w->delta_count);
    for (int i = 0; i < w->delta_count && ok; i++) {
        const delta_t *d = &w->deltas[i];
        ok &= write_i32(f, d->x);
        ok &= write_i32(f, d->y);
        ok &= write_i32(f, d->z);
        ok &= write_u8 (f, (uint8_t)d->block);
    }

    uint32_t overlay_count = 0;
    for (int i = 0; i < WORLD_BUCKETS; i++)
        for (const chunk_edit_t *e = w->edit_buckets[i]; e; e = e->next)
            if (e->overlay) overlay_count++;
    ok &= write_u32(f, overlay_count);
    for (int i = 0; i < WORLD_BUCKETS && ok; i++) {
        for (const chunk_edit_t *e = w->edit_buckets[i]; e && ok; e = e->next) {
            if (!e->overlay) continue;
            ok &= write_i32(f, (int32_t)e->cx);
            ok &= write_i32(f, (int32_t)e->cy);
            ok &= write_i32(f, (int32_t)e->cz);
            ok &= (fwrite(e->overlay->blocks, 1, CHUNK_VOLUME, f) == CHUNK_VOLUME);
        }
    }

    ok &= write_i32(f, (int32_t)p->hotbar_sel);
    for (int i = 0; i < SAVE_HOTBAR_SLOTS && ok; i++) ok &= write_slot(f, p->hotbar[i]);
    for (int i = 0; i < SAVE_PACK_SLOTS && ok; i++)   ok &= write_slot(f, p->pack[i]);
    for (int i = 0; i < SAVE_ARMOR_SLOTS && ok; i++)  ok &= write_slot(f, p->armor[i]);
    ok &= write_slot(f, p->offhand);

    ok &= write_i16(f, p->health);
    ok &= write_i16(f, p->max_health);
    ok &= write_f32(f, p->hunger);
    ok &= write_f32(f, p->saturation);
    ok &= write_f32(f, p->exhaustion);
    ok &= write_i16(f, p->air);
    ok &= write_i32(f, p->xp_total);
    ok &= write_i32(f, p->xp_level);
    ok &= write_u8 (f, p->is_dead);
    ok &= write_i32(f, p->respawn_x);
    ok &= write_i32(f, p->respawn_y);
    ok &= write_i32(f, p->respawn_z);
    ok &= write_u8 (f, p->has_respawn);
    {
        uint8_t ne = p->effect_count > SAVE_EFFECTS_MAX ? SAVE_EFFECTS_MAX : p->effect_count;
        ok &= write_u8(f, ne);
        for (int i = 0; i < ne && ok; i++) {
            ok &= write_u8 (f, p->effects[i].id);
            ok &= write_i32(f, p->effects[i].ticks);
            ok &= write_u8 (f, p->effects[i].amp);
        }
    }

    if (g_entity_io.save) ok &= g_entity_io.save(f); else ok &= write_u32(f, 0u);
    if (g_station_io.save) ok &= g_station_io.save(f); else ok &= write_u32(f, 0u);
    ok &= write_u32(f, p->achievements);

    ok &= write_f32(f, w->dawn_time);
    {
        uint32_t nu = 0;
        for (int i = 0; i < WORLD_MAX_USERS; i++) if (w->users[i].used) nu++;
        ok &= write_u32(f, nu);
        for (int i = 0; i < WORLD_MAX_USERS && ok; i++) {
            if (!w->users[i].used) continue;
            ok &= (fwrite(w->users[i].name, 1, USER_NAME_MAX, f) == USER_NAME_MAX);
            ok &= write_u32(f, w->users[i].allow_mask);
        }
    }
    if (g_players_io.save) ok &= g_players_io.save(f); else ok &= write_u32(f, 0u);

    fclose(f);
    if (!ok) {
        fprintf(stderr, "world_save: write error on '%s'\n", path);
        return 0;
    }
    printf("world_save: %d deltas + %u overlays → %s\n", w->delta_count, overlay_count, path);
    return 1;
}

int world_load_into(world_t *w, save_player_t *p, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int ok = 1;
    uint32_t magic = 0, version = 0, seed = 0, count = 0;
    int32_t mode = 0;
    uint8_t sel = 0;
    char    nm[WORLD_NAME_MAX];
    int32_t gm = 0;
    int32_t ac = 1;
    ok &= read_u32(f, &magic);
    ok &= read_u32(f, &version);
    ok &= read_u32(f, &seed);
    if (!ok || magic != SAVE_MAGIC || version < SAVE_VERSION_MIN || version > SAVE_VERSION) {
        fprintf(stderr, "world_load: bad header/version in '%s'\n", path);
        fclose(f);
        return 0;
    }
    if (seed != w->seed) {
        fprintf(stderr,
                "world_load: save seed (%u) ≠ current world seed (%u); ignoring save\n",
                seed, w->seed);
        fclose(f);
        return 0;
    }
    ok &= (fread(nm, 1, WORLD_NAME_MAX, f) == WORLD_NAME_MAX);
    ok &= read_i32(f, &gm);
    ok &= read_i32(f, &ac);
    if (ok && version >= 7) {
        uint8_t s[10];
        for (int i = 0; i < 10 && ok; i++) ok &= read_u8(f, &s[i]);
        float sr = 1.0f, sm = 1.0f; int32_t spx = 0, spy = 0, spz = 0;
        ok &= read_f32(f, &sr); ok &= read_f32(f, &sm);
        ok &= read_i32(f, &spx); ok &= read_i32(f, &spy); ok &= read_i32(f, &spz);
        if (ok) {
            w->difficulty = s[0]; w->mob_spawning = s[1]; w->keep_inventory = s[2];
            w->natural_regen = s[3]; w->pvp = s[4]; w->fall_damage = s[5];
            w->hunger_enabled = s[6]; w->daylight_cycle = s[7]; w->mob_griefing = s[8];
            w->weather_state = s[9];
            w->spawn_rate = sr; w->smelt_mult = sm; w->spawn_x = spx; w->spawn_y = spy; w->spawn_z = spz;
        }
    }
    ok &= read_f32(f, &p->pos_x);
    ok &= read_f32(f, &p->pos_y);
    ok &= read_f32(f, &p->pos_z);
    ok &= read_f32(f, &p->yaw);
    ok &= read_f32(f, &p->pitch);
    ok &= read_i32(f, &mode);
    ok &= read_u8 (f, &sel);
    ok &= read_f32(f, &p->world_time);
    ok &= read_u32(f, &count);
    if (!ok) {
        fprintf(stderr, "world_load: truncated header in '%s'\n", path);
        fclose(f);
        return 0;
    }
    p->mode           = (int)mode;
    p->selected_block = (int)sel;
    world_set_meta(w, nm, (int)gm, (int)ac);

    for (uint32_t i = 0; i < count && ok; i++) {
        int32_t x = 0, y = 0, z = 0;
        uint8_t blk = 0;
        ok &= read_i32(f, &x);
        ok &= read_i32(f, &y);
        ok &= read_i32(f, &z);
        ok &= read_u8 (f, &blk);
        if (!ok) break;
        record_delta(w, x, y, z, (block_t)blk);
        world_set_block_gen(w, x, y, z, (block_t)blk);
    }

    uint32_t overlay_count = 0;
    ok &= read_u32(f, &overlay_count);
    for (uint32_t i = 0; i < overlay_count && ok; i++) {
        int32_t ocx = 0, ocy = 0, ocz = 0;
        ok &= read_i32(f, &ocx);
        ok &= read_i32(f, &ocy);
        ok &= read_i32(f, &ocz);
        if (!ok) break;
        chunk_t *snap = malloc(sizeof *snap);
        if (!snap) { ok = 0; break; }
        if (fread(snap->blocks, 1, CHUNK_VOLUME, f) != CHUNK_VOLUME) { free(snap); ok = 0; break; }
        chunk_edit_t *ce = edit_chunk_get_or_create(w, ocx, ocy, ocz);
        free(ce->overlay);
        ce->overlay     = snap;
        ce->delta_count = 0;
        chunk_node_t *loaded = find_chunk(w, ocx, ocy, ocz);
        if (loaded) loaded->data = *snap;
    }
    if (!ok) {
        fprintf(stderr, "world_load: truncated terrain stream in '%s'\n", path);
        fclose(f);
        return 0;
    }

    p->has_inventory = 0;
    p->has_survival  = 0;
    int tail = 1;
    if (version >= 7) {
        int32_t hs = 0;
        tail &= read_i32(f, &hs);
        p->hotbar_sel = (int)hs;
        for (int i = 0; i < SAVE_HOTBAR_SLOTS && tail; i++) tail &= read_slot(f, &p->hotbar[i]);
        for (int i = 0; i < SAVE_PACK_SLOTS && tail; i++)   tail &= read_slot(f, &p->pack[i]);
        for (int i = 0; i < SAVE_ARMOR_SLOTS && tail; i++)  tail &= read_slot(f, &p->armor[i]);
        tail &= read_slot(f, &p->offhand);
        if (tail) p->has_inventory = 1;
        if (tail) {
            tail &= read_i16(f, &p->health);
            tail &= read_i16(f, &p->max_health);
            tail &= read_f32(f, &p->hunger);
            tail &= read_f32(f, &p->saturation);
            tail &= read_f32(f, &p->exhaustion);
            tail &= read_i16(f, &p->air);
            tail &= read_i32(f, &p->xp_total);
            tail &= read_i32(f, &p->xp_level);
            tail &= read_u8 (f, &p->is_dead);
            tail &= read_i32(f, &p->respawn_x);
            tail &= read_i32(f, &p->respawn_y);
            tail &= read_i32(f, &p->respawn_z);
            tail &= read_u8 (f, &p->has_respawn);
            uint8_t ne = 0;
            tail &= read_u8(f, &ne);
            if (ne > SAVE_EFFECTS_MAX) ne = SAVE_EFFECTS_MAX;
            p->effect_count = ne;
            for (int i = 0; i < ne && tail; i++) {
                tail &= read_u8 (f, &p->effects[i].id);
                tail &= read_i32(f, &p->effects[i].ticks);
                tail &= read_u8 (f, &p->effects[i].amp);
            }
            if (tail) p->has_survival = 1;
        }
        if (tail) {
            if (g_entity_io.load) tail &= g_entity_io.load(f, version);
            else { uint32_t c = 0; tail &= read_u32(f, &c); }
        }
        if (tail) {
            if (g_station_io.load) tail &= g_station_io.load(f, version);
            else { uint32_t c = 0; tail &= read_u32(f, &c); }
        }
        if (tail) { uint32_t a = 0; if (read_u32(f, &a)) p->achievements = a; }
        if (tail && version >= 8) {
            float dt = 0.27f;
            tail &= read_f32(f, &dt);
            if (tail) w->dawn_time = dt;
            uint32_t nu = 0;
            if (tail) tail &= read_u32(f, &nu);
            for (uint32_t i = 0; i < nu && tail; i++) {
                char unm[USER_NAME_MAX]; uint32_t mask = 0;
                tail &= (fread(unm, 1, USER_NAME_MAX, f) == USER_NAME_MAX);
                tail &= read_u32(f, &mask);
                if (tail && i < WORLD_MAX_USERS) {
                    memcpy(w->users[i].name, unm, USER_NAME_MAX);
                    w->users[i].allow_mask = mask;
                    w->users[i].used = 1;
                }
            }
            if (tail) {
                if (g_players_io.load) tail &= g_players_io.load(f, version);
                else { uint32_t c = 0; tail &= read_u32(f, &c); }
            }
        }
    } else if (version == 6) {
        int32_t hs = 0;
        tail &= read_i32(f, &hs);
        p->hotbar_sel = (int)hs;
        for (int i = 0; i < SAVE_HOTBAR_SLOTS && tail; i++) {
            uint8_t id = 0; int32_t cnt = 0;
            tail &= read_u8(f, &id); tail &= read_i32(f, &cnt);
            p->hotbar[i].id = id;
            p->hotbar[i].count = (int16_t)(cnt < 0 ? 0 : (cnt > 32767 ? 32767 : cnt));
            p->hotbar[i].durability = 0;
        }
        for (int i = 0; i < SAVE_PACK_SLOTS && tail; i++) {
            uint8_t id = 0; int32_t cnt = 0;
            tail &= read_u8(f, &id); tail &= read_i32(f, &cnt);
            p->pack[i].id = id;
            p->pack[i].count = (int16_t)(cnt < 0 ? 0 : (cnt > 32767 ? 32767 : cnt));
            p->pack[i].durability = 0;
        }
        if (tail) p->has_inventory = 1;
    }
    if (!tail) fprintf(stderr, "world_load: save tail truncated in '%s' (kept terrain+inventory)\n", path);

    fclose(f);

    for (int i = 0; i < WORLD_BUCKETS; i++) {
        for (chunk_node_t *n = w->buckets[i]; n; n = n->next) {
            n->dirty = 1;
            enqueue_water_candidates_in_chunk(w, n->cx, n->cy, n->cz);
        }
    }

    printf("world_load: %u deltas applied from %s\n", count, path);
    return 1;
}

int world_peek_meta(const char *path, world_meta_t *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint32_t magic = 0, version = 0, seed = 0;
    int32_t gm = 0, ac = 1;
    int ok = 1;
    ok &= read_u32(f, &magic);
    ok &= read_u32(f, &version);
    ok &= read_u32(f, &seed);
    ok &= (fread(out->name, 1, WORLD_NAME_MAX, f) == WORLD_NAME_MAX);
    ok &= read_i32(f, &gm);
    ok &= read_i32(f, &ac);
    out->difficulty = 2; out->mob_spawning = 1; out->keep_inventory = 0; out->natural_regen = 1;
    out->pvp = 1; out->fall_damage = 1; out->hunger_enabled = 1; out->daylight_cycle = 1;
    out->mob_griefing = 1; out->weather_state = 3; out->spawn_rate = 1.0f; out->smelt_mult = 1.0f;
    out->dawn_time = 0.27f;
    out->spawn_x = 0; out->spawn_y = 0; out->spawn_z = 0;
    if (ok && version >= 7) {
        uint8_t s[10];
        for (int i = 0; i < 10 && ok; i++) ok &= read_u8(f, &s[i]);
        float sr = 1.0f, sm = 1.0f; int32_t spx = 0, spy = 0, spz = 0;
        ok &= read_f32(f, &sr); ok &= read_f32(f, &sm);
        ok &= read_i32(f, &spx); ok &= read_i32(f, &spy); ok &= read_i32(f, &spz);
        if (ok) {
            out->difficulty = s[0]; out->mob_spawning = s[1]; out->keep_inventory = s[2];
            out->natural_regen = s[3]; out->pvp = s[4]; out->fall_damage = s[5];
            out->hunger_enabled = s[6]; out->daylight_cycle = s[7]; out->mob_griefing = s[8];
            out->weather_state = s[9];
            out->spawn_rate = sr; out->smelt_mult = sm; out->spawn_x = spx; out->spawn_y = spy; out->spawn_z = spz;
        }
    }
    fclose(f);
    if (!ok || magic != SAVE_MAGIC || version < SAVE_VERSION_MIN || version > SAVE_VERSION) return 0;
    out->name[WORLD_NAME_MAX - 1] = '\0';
    out->seed           = seed;
    out->gamemode       = (int)gm;
    out->allow_commands = (int)ac;
    out->world_time     = 0.0f;
    return 1;
}

void world_render(const world_t *w, int mvp_loc, int chunk_origin_loc, mat4 pv, int skip_water) {
    frustum_t f = extract_frustum(pv);

    typedef struct { const chunk_node_t *n; mat4 mvp; float ox, oy, oz; } visible_t;
    int cap = WORLD_BUCKETS * 4;
    visible_t *visible = malloc((size_t)cap * sizeof *visible);
    if (!visible) return;
    int vcount = 0;

    for (int i = 0; i < WORLD_BUCKETS; i++) {
        for (const chunk_node_t *n = w->buckets[i]; n; n = n->next) {
            if (n->dormant || !n->has_gpu || n->idx_count == 0 || !n->has_content) continue;
            float origin_x = (float)(n->cx * CHUNK_DIM);
            float origin_y = (float)(n->cy * CHUNK_DIM);
            float origin_z = (float)(n->cz * CHUNK_DIM);
            float min_x = origin_x + (float)n->cmin_x;
            float min_y = origin_y + (float)n->cmin_y;
            float min_z = origin_z + (float)n->cmin_z;
            float max_x = origin_x + (float)(n->cmax_x + 1);
            float max_y = origin_y + (float)(n->cmax_y + 1);
            float max_z = origin_z + (float)(n->cmax_z + 1);
            if (!aabb_in_frustum(&f, min_x, min_y, min_z, max_x, max_y, max_z)) continue;
            if (vcount >= cap) break;
            mat4 model = mat4_translate((vec3){origin_x, origin_y, origin_z});
            visible[vcount].n   = n;
            visible[vcount].mvp = mat4_multiply(pv, model);
            visible[vcount].ox  = origin_x;
            visible[vcount].oy  = origin_y;
            visible[vcount].oz  = origin_z;
            vcount++;
        }
    }

    glDepthMask(GL_TRUE);
    for (int i = 0; i < vcount; i++) {
        const chunk_node_t *n = visible[i].n;
        if (n->opaque_idx_count == 0) continue;
        glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, visible[i].mvp.m);
        if (chunk_origin_loc >= 0) glUniform3f(chunk_origin_loc, visible[i].ox, visible[i].oy, visible[i].oz);
        glBindVertexArray(n->vao);
        glDrawElements(GL_TRIANGLES, n->opaque_idx_count, GL_UNSIGNED_INT, 0);
    }

    glDepthMask(GL_FALSE);
    for (int i = 0; i < vcount; i++) {
        const chunk_node_t *n = visible[i].n;
        int glass_count = n->glass_idx_count - n->opaque_idx_count;
        if (glass_count == 0) continue;
        glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, visible[i].mvp.m);
        if (chunk_origin_loc >= 0) glUniform3f(chunk_origin_loc, visible[i].ox, visible[i].oy, visible[i].oz);
        glBindVertexArray(n->vao);
        glDrawElements(GL_TRIANGLES, glass_count, GL_UNSIGNED_INT,
                       (const void *)(uintptr_t)((size_t)n->opaque_idx_count * sizeof(unsigned int)));
    }

    if (!skip_water) {
        for (int i = 0; i < vcount; i++) {
            const chunk_node_t *n = visible[i].n;
            int water_count = n->idx_count - n->glass_idx_count;
            if (water_count == 0) continue;
            glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, visible[i].mvp.m);
            if (chunk_origin_loc >= 0) glUniform3f(chunk_origin_loc, visible[i].ox, visible[i].oy, visible[i].oz);
            glBindVertexArray(n->vao);
            glDrawElements(GL_TRIANGLES, water_count, GL_UNSIGNED_INT,
                           (const void *)(uintptr_t)((size_t)n->glass_idx_count * sizeof(unsigned int)));
        }
    }
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);

    free(visible);
}

void world_render_depth(const world_t *w, mat4 light_vp, int mvp_loc) {
    frustum_t f = extract_frustum(light_vp);
    glDepthMask(GL_TRUE);
    for (int i = 0; i < WORLD_BUCKETS; i++) {
        for (const chunk_node_t *n = w->buckets[i]; n; n = n->next) {
            if (n->dormant || !n->has_gpu || n->opaque_idx_count == 0 || !n->has_content) continue;
            float origin_x = (float)(n->cx * CHUNK_DIM);
            float origin_y = (float)(n->cy * CHUNK_DIM);
            float origin_z = (float)(n->cz * CHUNK_DIM);
            float min_x = origin_x + (float)n->cmin_x;
            float min_y = origin_y + (float)n->cmin_y;
            float min_z = origin_z + (float)n->cmin_z;
            float max_x = origin_x + (float)(n->cmax_x + 1);
            float max_y = origin_y + (float)(n->cmax_y + 1);
            float max_z = origin_z + (float)(n->cmax_z + 1);
            if (!aabb_in_frustum(&f, min_x, min_y, min_z, max_x, max_y, max_z)) continue;
            mat4 model = mat4_translate((vec3){origin_x, origin_y, origin_z});
            mat4 mvp = mat4_multiply(light_vp, model);
            glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, mvp.m);
            glBindVertexArray(n->vao);
            glDrawElements(GL_TRIANGLES, n->opaque_idx_count, GL_UNSIGNED_INT, 0);
        }
    }
    glBindVertexArray(0);
}

void world_render_water(const world_t *w, int mvp_loc, int chunk_origin_loc, mat4 pv) {
    frustum_t f = extract_frustum(pv);
    glDepthMask(GL_FALSE);
    for (int i = 0; i < WORLD_BUCKETS; i++) {
        for (const chunk_node_t *n = w->buckets[i]; n; n = n->next) {
            if (n->dormant || !n->has_gpu || !n->has_content) continue;
            int water_count = n->idx_count - n->glass_idx_count;
            if (water_count == 0) continue;
            float origin_x = (float)(n->cx * CHUNK_DIM);
            float origin_y = (float)(n->cy * CHUNK_DIM);
            float origin_z = (float)(n->cz * CHUNK_DIM);
            float min_x = origin_x + (float)n->cmin_x;
            float min_y = origin_y + (float)n->cmin_y;
            float min_z = origin_z + (float)n->cmin_z;
            float max_x = origin_x + (float)(n->cmax_x + 1);
            float max_y = origin_y + (float)(n->cmax_y + 1);
            float max_z = origin_z + (float)(n->cmax_z + 1);
            if (!aabb_in_frustum(&f, min_x, min_y, min_z, max_x, max_y, max_z)) continue;
            mat4 model = mat4_translate((vec3){origin_x, origin_y, origin_z});
            mat4 mvp = mat4_multiply(pv, model);
            glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, mvp.m);
            if (chunk_origin_loc >= 0) glUniform3f(chunk_origin_loc, origin_x, origin_y, origin_z);
            glBindVertexArray(n->vao);
            glDrawElements(GL_TRIANGLES, water_count, GL_UNSIGNED_INT,
                           (const void *)(uintptr_t)((size_t)n->glass_idx_count * sizeof(unsigned int)));
        }
    }
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
}

void world_render_reflection(const world_t *w, int mvp_loc, int chunk_origin_loc, mat4 pv) {
    glDepthMask(GL_TRUE);
    for (int i = 0; i < WORLD_BUCKETS; i++) {
        for (const chunk_node_t *n = w->buckets[i]; n; n = n->next) {
            if (n->dormant || !n->has_gpu || n->opaque_idx_count == 0 || !n->has_content) continue;
            float ox = (float)(n->cx * CHUNK_DIM);
            float oy = (float)(n->cy * CHUNK_DIM);
            float oz = (float)(n->cz * CHUNK_DIM);
            mat4 model = mat4_translate((vec3){ox, oy, oz});
            mat4 mvp = mat4_multiply(pv, model);
            glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, mvp.m);
            if (chunk_origin_loc >= 0) glUniform3f(chunk_origin_loc, ox, oy, oz);
            glBindVertexArray(n->vao);
            glDrawElements(GL_TRIANGLES, n->opaque_idx_count, GL_UNSIGNED_INT, 0);
        }
    }
    glDepthMask(GL_FALSE);
    for (int i = 0; i < WORLD_BUCKETS; i++) {
        for (const chunk_node_t *n = w->buckets[i]; n; n = n->next) {
            if (n->dormant || !n->has_gpu || !n->has_content) continue;
            int glass_count = n->glass_idx_count - n->opaque_idx_count;
            if (glass_count == 0) continue;
            float ox = (float)(n->cx * CHUNK_DIM);
            float oy = (float)(n->cy * CHUNK_DIM);
            float oz = (float)(n->cz * CHUNK_DIM);
            mat4 model = mat4_translate((vec3){ox, oy, oz});
            mat4 mvp = mat4_multiply(pv, model);
            glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, mvp.m);
            if (chunk_origin_loc >= 0) glUniform3f(chunk_origin_loc, ox, oy, oz);
            glBindVertexArray(n->vao);
            glDrawElements(GL_TRIANGLES, glass_count, GL_UNSIGNED_INT,
                           (const void *)(uintptr_t)((size_t)n->opaque_idx_count * sizeof(unsigned int)));
        }
    }
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
}
