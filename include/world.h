#pragma once

#include <stdint.h>

#include "chunk.h"
#include "mat4.h"

#define WORLD_HEIGHT_CHUNKS 16
#define WORLD_HEIGHT        (WORLD_HEIGHT_CHUNKS * CHUNK_DIM)
#define SEA_LEVEL           22

#define WORLD_NAME_MAX      32

#define GAMEMODE_CREATIVE   0
#define GAMEMODE_SURVIVAL   1

typedef enum {
    BIOME_PLAINS,
    BIOME_FOREST,
    BIOME_DESERT,
    BIOME_SNOW,
} biome_t;

typedef struct world_s world_t;

world_t *world_create(uint32_t seed, int radius);
void     world_destroy(world_t *w);

block_t world_get_block(const world_t *w, int x, int y, int z);
void    world_set_block(world_t *w, int x, int y, int z, block_t b);
void    world_remesh_chunk(world_t *w, int cx, int cy, int cz);

uint32_t world_seed(const world_t *w);

void        world_set_meta(world_t *w, const char *name, int gamemode, int allow_commands);
void        world_set_gamemode(world_t *w, int gamemode);
const char *world_name(const world_t *w);
int         world_gamemode(const world_t *w);
int         world_allow_commands(const world_t *w);

void  world_set_sun_dir(world_t *w, float x, float y, float z);
float world_sun_dir_x(const world_t *w);
float world_sun_dir_y(const world_t *w);
float world_sun_dir_z(const world_t *w);

void  world_mark_all_dirty(world_t *w);

void  world_pump_meshing(world_t *w);

void  world_wait_idle(world_t *w);

int   world_work_remaining(const world_t *w);

struct falling_system_s;
typedef struct falling_system_s falling_system_t;
void world_set_falling_system(world_t *w, falling_system_t *fs);

biome_t world_biome_at(uint32_t seed, int x, int z);
int     world_height_at(uint32_t seed, int x, int z);

void world_update_streaming(world_t *w, float player_x, float player_z);

void world_tick_water(world_t *w, int max_cells);

#define WORLD_LIGHTVOL_MAX_DIM 128
void world_build_light_volume(world_t *w, int ox, int oy, int oz, int dim, uint8_t *out);

void world_render(const world_t *w, int mvp_uniform_loc, int chunk_origin_uniform_loc, mat4 pv, int skip_water);

void world_render_depth(const world_t *w, mat4 light_vp, int mvp_uniform_loc);

void world_render_water(const world_t *w, int mvp_uniform_loc, int chunk_origin_uniform_loc, mat4 pv);

void world_render_reflection(const world_t *w, int mvp_uniform_loc, int chunk_origin_uniform_loc, mat4 pv);

#define SAVE_HOTBAR_SLOTS 10
#define SAVE_PACK_SLOTS   32

typedef struct {
    float pos_x, pos_y, pos_z;
    float yaw, pitch;
    int   mode;
    int   selected_block;
    float world_time;
    int     hotbar_sel;
    int     has_inventory;
    uint8_t hotbar_block[SAVE_HOTBAR_SLOTS];
    int32_t hotbar_count[SAVE_HOTBAR_SLOTS];
    uint8_t pack_block[SAVE_PACK_SLOTS];
    int32_t pack_count[SAVE_PACK_SLOTS];
} save_player_t;

int world_save(const world_t *w, const save_player_t *p, const char *path);
int world_load_into(world_t *w, save_player_t *p, const char *path);

typedef struct {
    char     name[WORLD_NAME_MAX];
    uint32_t seed;
    int      gamemode;
    int      allow_commands;
    float    world_time;
} world_meta_t;

int world_peek_meta(const char *path, world_meta_t *out);