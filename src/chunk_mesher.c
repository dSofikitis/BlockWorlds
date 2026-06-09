#include "chunk_mesher.h"
#include "world.h"
#include "chunk.h"
#include "texture.h"
#include "registry.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SKYLIGHT_FALLOFF 0.14f
#define SKYLIGHT_FLOOR   0.20f

#define LIGHT_PAD       10
#define LIGHT_RADIUS    10
#define LIGHT_GRID_DIM  (CHUNK_DIM + 2 * LIGHT_PAD)

typedef struct {
    int   origin_x, origin_y, origin_z;
    float v[LIGHT_GRID_DIM][LIGHT_GRID_DIM][LIGHT_GRID_DIM];
} light_grid_t;

static render_class_t block_class(block_t b) { return block_render_class(b); }

static int light_passes(block_t b) { return block_class(b) != RCLASS_SOLID; }

static void light_grid_init(const world_t *world, int cx, int cy, int cz, light_grid_t *g) {
    g->origin_x = cx * CHUNK_DIM - LIGHT_PAD;
    g->origin_y = cy * CHUNK_DIM - LIGHT_PAD;
    g->origin_z = cz * CHUNK_DIM - LIGHT_PAD;
    memset(g->v, 0, sizeof g->v);

    int total = LIGHT_GRID_DIM * LIGHT_GRID_DIM * LIGHT_GRID_DIM;
    uint8_t *level = NULL;
    int     *queue = NULL;
    int qh = 0, qt = 0;

    for (int lx = 0; lx < LIGHT_GRID_DIM; lx++)
        for (int ly = 0; ly < LIGHT_GRID_DIM; ly++)
            for (int lz = 0; lz < LIGHT_GRID_DIM; lz++) {
                if (block_light_emission(world_get_block(world, g->origin_x + lx,
                        g->origin_y + ly, g->origin_z + lz)) <= 0) continue;
                if (!level) {
                    level = calloc((size_t)total, 1);
                    queue = malloc((size_t)total * sizeof *queue);
                    if (!level || !queue) { free(level); free(queue); return; }
                }
                int idx = (lx * LIGHT_GRID_DIM + ly) * LIGHT_GRID_DIM + lz;
                level[idx] = LIGHT_RADIUS;
                queue[qt++] = idx;
            }

    if (!level) return;

    static const int OFF[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    while (qh < qt) {
        int idx = queue[qh++];
        int lz = idx % LIGHT_GRID_DIM;
        int ly = (idx / LIGHT_GRID_DIM) % LIGHT_GRID_DIM;
        int lx = idx / (LIGHT_GRID_DIM * LIGHT_GRID_DIM);
        int lv = level[idx];
        if (lv <= 1) continue;
        for (int k = 0; k < 6; k++) {
            int nx = lx + OFF[k][0], ny = ly + OFF[k][1], nz = lz + OFF[k][2];
            if (nx < 0 || ny < 0 || nz < 0 ||
                nx >= LIGHT_GRID_DIM || ny >= LIGHT_GRID_DIM || nz >= LIGHT_GRID_DIM) continue;
            if (!light_passes(world_get_block(world, g->origin_x + nx,
                                              g->origin_y + ny, g->origin_z + nz))) continue;
            int nidx = (nx * LIGHT_GRID_DIM + ny) * LIGHT_GRID_DIM + nz;
            if (level[nidx] < lv - 1) {
                level[nidx] = (uint8_t)(lv - 1);
                queue[qt++] = nidx;
            }
        }
    }

    for (int lx = 0; lx < LIGHT_GRID_DIM; lx++)
        for (int ly = 0; ly < LIGHT_GRID_DIM; ly++)
            for (int lz = 0; lz < LIGHT_GRID_DIM; lz++) {
                int lv = level[(lx * LIGHT_GRID_DIM + ly) * LIGHT_GRID_DIM + lz];
                if (lv > 0) {
                    float dist = (float)(LIGHT_RADIUS - lv) / (float)LIGHT_RADIUS;
                    g->v[lx][ly][lz] = 1.0f - dist * dist;
                }
            }

    free(level);
    free(queue);
}

static float light_grid_sample(const light_grid_t *g, int wx, int wy, int wz) {
    int gx = wx - g->origin_x;
    int gy = wy - g->origin_y;
    int gz = wz - g->origin_z;
    if (gx < 0 || gy < 0 || gz < 0 ||
        gx >= LIGHT_GRID_DIM || gy >= LIGHT_GRID_DIM || gz >= LIGHT_GRID_DIM) {
        return 0.0f;
    }
    return g->v[gx][gy][gz];
}

typedef struct { int tx, ty; } tile_t;

typedef struct {
    tile_t top;
    tile_t bottom;
    tile_t side;
} block_tiles_t;

static const block_tiles_t BLOCK_TILES[] = {
    [BLOCK_AIR]          = {{0,0}, {0,0}, {0,0}},
    [BLOCK_STONE]        = {{0,0}, {0,0}, {0,0}},
    [BLOCK_DIRT]         = {{1,0}, {1,0}, {1,0}},
    [BLOCK_GRASS]        = {{2,0}, {1,0}, {3,0}},
    [BLOCK_WATER]        = {{0,1}, {0,1}, {0,1}},
    [BLOCK_GRAVEL]       = {{1,1}, {1,1}, {1,1}},
    [BLOCK_DEEPSTONE]    = {{2,1}, {2,1}, {2,1}},
    [BLOCK_SAND]         = {{3,1}, {3,1}, {3,1}},
    [BLOCK_SNOW]         = {{0,2}, {1,0}, {1,2}},
    [BLOCK_WOOD]         = {{2,2}, {2,2}, {3,2}},
    [BLOCK_LEAVES]       = {{0,3}, {0,3}, {0,3}},
    [BLOCK_COAL_ORE]     = {{1,3}, {1,3}, {1,3}},
    [BLOCK_IRON_ORE]     = {{2,3}, {2,3}, {2,3}},
    [BLOCK_DIAMOND_ORE]  = {{3,3}, {3,3}, {3,3}},
    [BLOCK_GLOWSTONE]    = {{4,0}, {4,0}, {4,0}},
    [BLOCK_PLANKS]       = {{4,1}, {4,1}, {4,1}},
    [BLOCK_QUARTZ]       = {{4,2}, {4,2}, {4,2}},
    [BLOCK_CONCRETE]     = {{4,3}, {4,3}, {4,3}},
    [BLOCK_GLASS]        = {{0,4}, {0,4}, {0,4}},
    [BLOCK_TORCH]          = {{5,0},  {5,0},  {5,0}},
    [BLOCK_CRAFTING_TABLE] = {{6,0},  {7,0},  {7,0}},
    [BLOCK_FURNACE]        = {{9,0},  {9,0},  {8,0}},
    [BLOCK_FORGE]          = {{10,0}, {10,0}, {10,0}},
    [BLOCK_ANVIL]          = {{9,2},  {11,0}, {11,0}},
    [BLOCK_CHEST]          = {{8,2},  {12,0}, {12,0}},
    [BLOCK_BED_FOOT]       = {{13,0}, {13,0}, {13,0}},
    [BLOCK_BED_HEAD]       = {{14,0}, {14,0}, {14,0}},
    [BLOCK_FARMLAND]       = {{15,0}, {1,0},  {1,0}},
    [BLOCK_WHEAT_CROP]     = {{5,1},  {5,1},  {5,1}},
    [BLOCK_SAPLING]        = {{6,1},  {6,1},  {6,1}},
    [BLOCK_IRON_BLOCK]     = {{7,1},  {7,1},  {7,1}},
    [BLOCK_DIAMOND_BLOCK]  = {{8,1},  {8,1},  {8,1}},
    [BLOCK_COAL_BLOCK]     = {{9,1},  {9,1},  {9,1}},
    [BLOCK_COBBLESTONE]    = {{10,1}, {10,1}, {10,1}},
    [BLOCK_WOOL]           = {{11,1}, {11,1}, {11,1}},
    [BLOCK_LEAVES_PINE]    = {{12,1}, {12,1}, {12,1}},
    [BLOCK_LEAVES_ACACIA]  = {{13,1}, {13,1}, {13,1}},
    [BLOCK_LEAVES_SWAMP]   = {{14,1}, {14,1}, {14,1}},
    [BLOCK_LEAVES_JUNGLE]  = {{15,1}, {15,1}, {15,1}},
    [BLOCK_TALL_GRASS]     = {{5,2},  {5,2},  {5,2}},
    [BLOCK_WATER_SHALLOW]  = {{0,1},  {0,1},  {0,1}},
    [BLOCK_BREWING_STAND]  = {{7,2},  {6,2},  {6,2}},
    [BLOCK_ICE]            = {{1,4},  {1,4},  {1,4}},
};

typedef struct {
    int axis;
    int dir;
    int u_axis;
    int v_axis;
} pass_t;

static const pass_t PASSES[6] = {
    {0, +1, 2, 1},
    {0, -1, 2, 1},
    {1, +1, 0, 2},
    {1, -1, 0, 2},
    {2, +1, 0, 1},
    {2, -1, 0, 1},
};

static tile_t pick_tile_for_pass(block_t b, const pass_t *p) {
    const block_tiles_t *bt = &BLOCK_TILES[b];
    if (p->axis == 1 && p->dir == +1) return bt->top;
    if (p->axis == 1 && p->dir == -1) return bt->bottom;
    return bt->side;
}

static float skylight_at(uint32_t seed, int world_x, int world_y, int world_z) {
    int terrain_h = world_height_at(seed, world_x, world_z);
    int depth = terrain_h - world_y;
    if (depth <= 0) return 1.0f;
    float v = expf(-(float)depth * SKYLIGHT_FALLOFF);
    if (v < SKYLIGHT_FLOOR) v = SKYLIGHT_FLOOR;
    return v;
}

static const float AO_LEVELS[4] = { 0.55f, 0.70f, 0.85f, 1.0f };

static int occludes_ao(block_t b) { return block_class(b) == RCLASS_SOLID; }

static int ao_level(const world_t *world, const pass_t *p,
                    int awx, int awy, int awz, int du, int dv) {
    int s1[3] = {awx, awy, awz}; s1[p->u_axis] += du;
    int s2[3] = {awx, awy, awz}; s2[p->v_axis] += dv;
    int cn[3] = {awx, awy, awz}; cn[p->u_axis] += du; cn[p->v_axis] += dv;
    int o1 = occludes_ao(world_get_block(world, s1[0], s1[1], s1[2]));
    int o2 = occludes_ao(world_get_block(world, s2[0], s2[1], s2[2]));
    int oc = occludes_ao(world_get_block(world, cn[0], cn[1], cn[2]));
    if (o1 && o2) return 0;
    return 3 - (o1 + o2 + oc);
}

typedef struct { block_t b; uint8_t ao[4]; } maskcell_t;

static int mc_match(const maskcell_t *m, block_t b, const uint8_t ao[4]) {
    return m->b == b &&
           m->ao[0] == ao[0] && m->ao[1] == ao[1] &&
           m->ao[2] == ao[2] && m->ao[3] == ao[3];
}

static void emit_greedy_quad(mesh_buf_t *out, const pass_t *p,
                             int slice, int mu, int mv, int w, int h, block_t b,
                             uint32_t seed, int chunk_origin_x, int chunk_origin_y, int chunk_origin_z,
                             const light_grid_t *light, const uint8_t ao_corners[4]) {
    int plane = slice + (p->dir > 0 ? 1 : 0);
    float plane_f = (float)plane;

    if (block_is_water(b) && p->axis == 1 && p->dir == +1) {
        plane_f -= 0.125f;
    }

    float corners_f[4][3];
    for (int i = 0; i < 4; i++) corners_f[i][p->axis] = plane_f;
    corners_f[0][p->u_axis] = (float)mu;       corners_f[0][p->v_axis] = (float)mv;
    corners_f[1][p->u_axis] = (float)(mu + w); corners_f[1][p->v_axis] = (float)mv;
    corners_f[2][p->u_axis] = (float)(mu + w); corners_f[2][p->v_axis] = (float)(mv + h);
    corners_f[3][p->u_axis] = (float)mu;       corners_f[3][p->v_axis] = (float)(mv + h);

    int air_axis = slice + p->dir;
    int vert_u[4] = { mu, mu + w, mu + w, mu     };
    int vert_v[4] = { mv, mv,     mv + h, mv + h };

    float uvs[4][2] = {
        {0.0f,     0.0f},
        {(float)w, 0.0f},
        {(float)w, (float)h},
        {0.0f,     (float)h},
    };

    tile_t tile = pick_tile_for_pass(b, p);
    float rflags = block_render_flags(b);

    float nx = (p->axis == 0) ? (float)p->dir : 0.0f;
    float ny = (p->axis == 1) ? (float)p->dir : 0.0f;
    float nz = (p->axis == 2) ? (float)p->dir : 0.0f;

    unsigned int base = (unsigned int)out->vert_count;
    for (int i = 0; i < 4; i++) {
        int wx = chunk_origin_x + (int)corners_f[i][0];
        int wy = chunk_origin_y + (int)corners_f[i][1];
        int wz = chunk_origin_z + (int)corners_f[i][2];
        float sky = skylight_at(seed, wx, wy, wz);

        float bl = 0.0f;
        for (int du = -1; du <= 0; du++) {
            for (int dv = -1; dv <= 0; dv++) {
                int lcell[3];
                lcell[p->axis]   = air_axis;
                lcell[p->u_axis] = vert_u[i] + du;
                lcell[p->v_axis] = vert_v[i] + dv;
                bl += light_grid_sample(light,
                                        chunk_origin_x + lcell[0],
                                        chunk_origin_y + lcell[1],
                                        chunk_origin_z + lcell[2]);
            }
        }
        bl *= 0.25f;

        mesh_vertex_t v = {
            corners_f[i][0],
            corners_f[i][1],
            corners_f[i][2],
            uvs[i][0],
            uvs[i][1],
            (float)tile.tx,
            (float)tile.ty,
            nx, ny, nz,
            sky,
            bl,
            AO_LEVELS[ao_corners[i]],
            rflags,
        };
        mesh_push_vertex(out, v);
    }
    if (ao_corners[0] + ao_corners[2] < ao_corners[1] + ao_corners[3]) {
        mesh_push_index(out, base + 0); mesh_push_index(out, base + 1); mesh_push_index(out, base + 3);
        mesh_push_index(out, base + 1); mesh_push_index(out, base + 2); mesh_push_index(out, base + 3);
    } else {
        mesh_push_index(out, base + 0); mesh_push_index(out, base + 1); mesh_push_index(out, base + 2);
        mesh_push_index(out, base + 0); mesh_push_index(out, base + 2); mesh_push_index(out, base + 3);
    }
}

typedef enum {
    MESH_OPAQUE,
    MESH_GLASS,
    MESH_WATER,
} mesh_kind_t;

static int is_translucent(block_t b) { return block_class(b) == RCLASS_TRANSLUCENT; }
static int block_box_model(block_t b, float *hw, float *h);

static int face_visible(block_t self, block_t nb, int dir) {
    render_class_t sc = block_class(self);
    render_class_t nc = block_class(nb);
    float bhw, bh;
    if (nc == RCLASS_EMPTY) return 1;
    if (block_box_model(nb, &bhw, &bh)) return 1;
    if (nc == RCLASS_SOLID) return 0;
    if (sc == RCLASS_SOLID) return 1;
    if (block_is_water(self) && block_is_water(nb)) return 0;
    if (block_is_water(self) && nb == BLOCK_ICE) return 0;
    if (nb != self) return 1;
    if (sc == RCLASS_TRANSLUCENT) return 0;
    return dir > 0;
}

static int block_is_cross_plant(block_t b) {
    return b == BLOCK_TALL_GRASS || b == BLOCK_SAPLING || b == BLOCK_WHEAT_CROP;
}

static void emit_cross(mesh_buf_t *out, block_t b, int lx, int ly, int lz,
                       uint32_t seed, int ox, int oy, int oz, const light_grid_t *light, float H) {
    tile_t tile = BLOCK_TILES[b].side;
    float rflags = block_render_flags(b);
    int wx = ox + lx, wy = oy + ly, wz = oz + lz;
    float sky = skylight_at(seed, wx, wy, wz);
    float bl  = light_grid_sample(light, wx, wy, wz);
    float x0 = (float)lx, y0 = (float)ly, z0 = (float)lz;
    float diag[2][4] = {
        { x0,        z0,        x0 + 1.0f, z0 + 1.0f },
        { x0 + 1.0f, z0,        x0,        z0 + 1.0f },
    };
    for (int d = 0; d < 2; d++) {
        float ax = diag[d][0], az = diag[d][1], bx = diag[d][2], bz = diag[d][3];
        unsigned int base = (unsigned int)out->vert_count;
        mesh_vertex_t v0 = { ax, y0,     az, 0.0f, 1.0f, (float)tile.tx, (float)tile.ty, 0.0f, 1.0f, 0.0f, sky, bl, 1.0f, rflags };
        mesh_vertex_t v1 = { bx, y0,     bz, 1.0f, 1.0f, (float)tile.tx, (float)tile.ty, 0.0f, 1.0f, 0.0f, sky, bl, 1.0f, rflags };
        mesh_vertex_t v2 = { bx, y0 + H, bz, 1.0f, 0.0f, (float)tile.tx, (float)tile.ty, 0.0f, 1.0f, 0.0f, sky, bl, 1.0f, rflags };
        mesh_vertex_t v3 = { ax, y0 + H, az, 0.0f, 0.0f, (float)tile.tx, (float)tile.ty, 0.0f, 1.0f, 0.0f, sky, bl, 1.0f, rflags };
        mesh_push_vertex(out, v0);
        mesh_push_vertex(out, v1);
        mesh_push_vertex(out, v2);
        mesh_push_vertex(out, v3);
        mesh_push_index(out, base + 0); mesh_push_index(out, base + 1); mesh_push_index(out, base + 2);
        mesh_push_index(out, base + 0); mesh_push_index(out, base + 2); mesh_push_index(out, base + 3);
        mesh_push_index(out, base + 0); mesh_push_index(out, base + 2); mesh_push_index(out, base + 1);
        mesh_push_index(out, base + 0); mesh_push_index(out, base + 3); mesh_push_index(out, base + 2);
    }
}

static int block_box_model(block_t b, float *hw, float *h) {
    switch (b) {
        case BLOCK_ANVIL:         *hw = 0.40f; *h = 0.64f; return 1;
        case BLOCK_CHEST:         *hw = 0.42f; *h = 0.84f; return 1;
        case BLOCK_BREWING_STAND: *hw = 0.26f; *h = 0.82f; return 1;
        default: return 0;
    }
}

static void box_quad(mesh_buf_t *out, const float c[4][3], tile_t t,
                     float nx, float ny, float nz, float sky, float bl, float rflags) {
    static const float uv[4][2] = {{0.0f,1.0f},{1.0f,1.0f},{1.0f,0.0f},{0.0f,0.0f}};
    unsigned int base = (unsigned int)out->vert_count;
    for (int i = 0; i < 4; i++) {
        mesh_vertex_t v = { c[i][0], c[i][1], c[i][2], uv[i][0], uv[i][1],
                            (float)t.tx, (float)t.ty, nx, ny, nz, sky, bl, 1.0f, rflags };
        mesh_push_vertex(out, v);
    }
    mesh_push_index(out, base + 0); mesh_push_index(out, base + 1); mesh_push_index(out, base + 2);
    mesh_push_index(out, base + 0); mesh_push_index(out, base + 2); mesh_push_index(out, base + 3);
}

static void emit_box_model(mesh_buf_t *out, block_t b, int lx, int ly, int lz,
                           uint32_t seed, int ox, int oy, int oz, const light_grid_t *light,
                           float hw, float h) {
    const block_tiles_t *bt = &BLOCK_TILES[b];
    float rflags = block_render_flags(b);
    int wx = ox + lx, wy = oy + ly, wz = oz + lz;
    float sky = skylight_at(seed, wx, wy, wz);
    float bl  = light_grid_sample(light, wx, wy, wz);
    float cxc = (float)lx + 0.5f, czc = (float)lz + 0.5f;
    float y0 = (float)ly, y1 = (float)ly + h;
    float x0 = cxc - hw, x1 = cxc + hw, z0 = czc - hw, z1 = czc + hw;
    float px[4][3] = {{x1,y0,z1},{x1,y0,z0},{x1,y1,z0},{x1,y1,z1}}; box_quad(out, px, bt->side,  1,0,0, sky,bl,rflags);
    float nx[4][3] = {{x0,y0,z0},{x0,y0,z1},{x0,y1,z1},{x0,y1,z0}}; box_quad(out, nx, bt->side, -1,0,0, sky,bl,rflags);
    float pz[4][3] = {{x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}}; box_quad(out, pz, bt->side,  0,0,1, sky,bl,rflags);
    float nz[4][3] = {{x1,y0,z0},{x0,y0,z0},{x0,y1,z0},{x1,y1,z0}}; box_quad(out, nz, bt->side,  0,0,-1, sky,bl,rflags);
    float py[4][3] = {{x0,y1,z1},{x1,y1,z1},{x1,y1,z0},{x0,y1,z0}}; box_quad(out, py, bt->top,    0,1,0, sky,bl,rflags);
    float my[4][3] = {{x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1}}; box_quad(out, my, bt->bottom, 0,-1,0, sky,bl,rflags);
}

static int block_is_model(block_t b) {
    float hw, h;
    return block_is_cross_plant(b) || b == BLOCK_TORCH || block_box_model(b, &hw, &h);
}

static void emit_cross_plants(const world_t *world, int cx, int cy, int cz,
                              mesh_buf_t *out, const light_grid_t *light) {
    int ox = cx * CHUNK_DIM, oy = cy * CHUNK_DIM, oz = cz * CHUNK_DIM;
    uint32_t seed = world_seed(world);
    for (int ly = 0; ly < CHUNK_DIM; ly++)
        for (int lz = 0; lz < CHUNK_DIM; lz++)
            for (int lx = 0; lx < CHUNK_DIM; lx++) {
                block_t b = world_get_block(world, ox + lx, oy + ly, oz + lz);
                float hw, h;
                if (block_is_cross_plant(b))
                    emit_cross(out, b, lx, ly, lz, seed, ox, oy, oz, light, 0.94f);
                else if (b == BLOCK_TORCH)
                    emit_cross(out, b, lx, ly, lz, seed, ox, oy, oz, light, 0.62f);
                else if (block_box_model(b, &hw, &h))
                    emit_box_model(out, b, lx, ly, lz, seed, ox, oy, oz, light, hw, h);
            }
}

static void mesh_slice(const world_t *world, int cx, int cy, int cz,
                       mesh_buf_t *out, const pass_t *p, int slice,
                       mesh_kind_t kind, const light_grid_t *light) {
    int origin_x = cx * CHUNK_DIM;
    int origin_y = cy * CHUNK_DIM;
    int origin_z = cz * CHUNK_DIM;

    maskcell_t mask[CHUNK_DIM][CHUNK_DIM];
    for (int mv = 0; mv < CHUNK_DIM; mv++) {
        for (int mu = 0; mu < CHUNK_DIM; mu++) {
            maskcell_t cell = { BLOCK_AIR, {3, 3, 3, 3} };
            int lp[3] = {0, 0, 0};
            lp[p->axis]   = slice;
            lp[p->u_axis] = mu;
            lp[p->v_axis] = mv;

            int wx = origin_x + lp[0];
            int wy = origin_y + lp[1];
            int wz = origin_z + lp[2];

            block_t self = world_get_block(world, wx, wy, wz);
            if (self == BLOCK_AIR) { mask[mv][mu] = cell; continue; }
            if (block_is_model(self)) { mask[mv][mu] = cell; continue; }

            if (kind == MESH_OPAQUE && is_translucent(self)) { mask[mv][mu] = cell; continue; }
            if (kind == MESH_GLASS && self != BLOCK_GLASS && self != BLOCK_ICE) { mask[mv][mu] = cell; continue; }
            if (kind == MESH_WATER && !block_is_water(self)) { mask[mv][mu] = cell; continue; }

            int nlp[3] = { lp[0], lp[1], lp[2] };
            nlp[p->axis] += p->dir;
            block_t nb = world_get_block(world, origin_x + nlp[0],
                                                origin_y + nlp[1],
                                                origin_z + nlp[2]);

            int visible = face_visible(self, nb, p->dir);
            if (!visible) { mask[mv][mu] = cell; continue; }

            cell.b = self;
            if (kind == MESH_OPAQUE) {
                int aw[3] = { wx, wy, wz };
                aw[p->axis] += p->dir;
                cell.ao[0] = (uint8_t)ao_level(world, p, aw[0], aw[1], aw[2], -1, -1);
                cell.ao[1] = (uint8_t)ao_level(world, p, aw[0], aw[1], aw[2], +1, -1);
                cell.ao[2] = (uint8_t)ao_level(world, p, aw[0], aw[1], aw[2], +1, +1);
                cell.ao[3] = (uint8_t)ao_level(world, p, aw[0], aw[1], aw[2], -1, +1);
            }
            mask[mv][mu] = cell;
        }
    }

    for (int mv = 0; mv < CHUNK_DIM; mv++) {
        int mu = 0;
        while (mu < CHUNK_DIM) {
            block_t b = mask[mv][mu].b;
            if (b == BLOCK_AIR) { mu++; continue; }
            uint8_t ao[4] = { mask[mv][mu].ao[0], mask[mv][mu].ao[1],
                              mask[mv][mu].ao[2], mask[mv][mu].ao[3] };

            int w = 1;
            while (mu + w < CHUNK_DIM && mc_match(&mask[mv][mu + w], b, ao)) w++;

            int h = 1;
            int extending = 1;
            while (extending && mv + h < CHUNK_DIM) {
                for (int i = 0; i < w; i++) {
                    if (!mc_match(&mask[mv + h][mu + i], b, ao)) { extending = 0; break; }
                }
                if (extending) h++;
            }

            emit_greedy_quad(out, p, slice, mu, mv, w, h, b,
                             world_seed(world), origin_x, origin_y, origin_z, light, ao);

            for (int hh = 0; hh < h; hh++)
                for (int ww = 0; ww < w; ww++)
                    mask[mv + hh][mu + ww].b = BLOCK_AIR;

            mu += w;
        }
    }
}

void chunk_to_mesh(const world_t *world, int cx, int cy, int cz, mesh_buf_t *out) {
    light_grid_t *light = malloc(sizeof *light);
    if (!light) return;
    light_grid_init(world, cx, cy, cz, light);

    for (int p = 0; p < 6; p++) {
        const pass_t *pass = &PASSES[p];
        for (int slice = 0; slice < CHUNK_DIM; slice++) {
            mesh_slice(world, cx, cy, cz, out, pass, slice, MESH_OPAQUE, light);
        }
    }
    emit_cross_plants(world, cx, cy, cz, out, light);
    out->opaque_idx_count = out->idx_count;

    for (int p = 0; p < 6; p++) {
        const pass_t *pass = &PASSES[p];
        for (int slice = 0; slice < CHUNK_DIM; slice++) {
            mesh_slice(world, cx, cy, cz, out, pass, slice, MESH_GLASS, light);
        }
    }
    out->glass_idx_count = out->idx_count;

    for (int p = 0; p < 6; p++) {
        const pass_t *pass = &PASSES[p];
        for (int slice = 0; slice < CHUNK_DIM; slice++) {
            mesh_slice(world, cx, cy, cz, out, pass, slice, MESH_WATER, light);
        }
    }

    free(light);
}
