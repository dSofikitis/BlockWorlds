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
    BIOME_SWAMP,
    BIOME_JUNGLE,
    BIOME_ROCKY,
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

int world_block_light_at(const world_t *w, int x, int y, int z);

void world_render(const world_t *w, int mvp_uniform_loc, int chunk_origin_uniform_loc, mat4 pv, int skip_water);

void world_render_depth(const world_t *w, mat4 light_vp, int mvp_uniform_loc);

void world_render_water(const world_t *w, int mvp_uniform_loc, int chunk_origin_uniform_loc, mat4 pv);

void world_render_reflection(const world_t *w, int mvp_uniform_loc, int chunk_origin_uniform_loc, mat4 pv);

#define SAVE_HOTBAR_SLOTS 10
#define SAVE_PACK_SLOTS   32
#define SAVE_ARMOR_SLOTS  4
#define SAVE_EFFECTS_MAX  8

typedef struct { uint16_t id; int16_t count; uint16_t durability; } save_slot_t;
typedef struct { uint8_t id; int32_t ticks; uint8_t amp; } save_effect_t;

typedef struct {
    float pos_x, pos_y, pos_z;
    float yaw, pitch;
    int   mode;
    int   selected_block;
    float world_time;
    int          hotbar_sel;
    int          has_inventory;
    save_slot_t  hotbar[SAVE_HOTBAR_SLOTS];
    save_slot_t  pack[SAVE_PACK_SLOTS];
    save_slot_t  armor[SAVE_ARMOR_SLOTS];
    save_slot_t  offhand;
    int          has_survival;
    int16_t      health, max_health;
    float        hunger, saturation, exhaustion;
    int16_t      air;
    int32_t      xp_total, xp_level;
    uint8_t      is_dead;
    int32_t      respawn_x, respawn_y, respawn_z;
    uint8_t      has_respawn;
    uint8_t      effect_count;
    save_effect_t effects[SAVE_EFFECTS_MAX];
    uint32_t     achievements;
} save_player_t;

int world_save(const world_t *w, const save_player_t *p, const char *path);
int world_load_into(world_t *w, save_player_t *p, const char *path);

void world_foreach_edit(const world_t *w,
                        void (*cb)(int x, int y, int z, block_t b, void *ud), void *ud);

typedef struct {
    char     name[WORLD_NAME_MAX];
    uint32_t seed;
    int      gamemode;
    int      allow_commands;
    float    world_time;
    uint8_t  difficulty, mob_spawning, keep_inventory, natural_regen, pvp, fall_damage,
             hunger_enabled, daylight_cycle, mob_griefing, weather_state;
    float    spawn_rate, smelt_mult;
    float    dawn_time;
    int32_t  spawn_x, spawn_y, spawn_z;
} world_meta_t;

int world_peek_meta(const char *path, world_meta_t *out);

typedef enum { GR_MOB_SPAWNING, GR_KEEP_INVENTORY, GR_NATURAL_REGEN, GR_PVP, GR_FALL_DAMAGE,
               GR_HUNGER, GR_DAYLIGHT_CYCLE, GR_MOB_GRIEFING, GR_COUNT } gamerule_t;

void  world_set_difficulty(world_t *w, int d);   int world_difficulty(const world_t *w);
void  world_set_gamerule(world_t *w, gamerule_t g, int on);  int world_gamerule(const world_t *w, gamerule_t g);
void  world_set_spawn_rate(world_t *w, float r); float world_spawn_rate(const world_t *w);
void  world_set_dawn_time(world_t *w, float t);  float world_dawn_time(const world_t *w);
void  world_set_smelt_mult(world_t *w, float m); float world_smelt_mult(const world_t *w);
void  world_set_spawn(world_t *w, int x, int y, int z);  void world_get_spawn(const world_t *w, int *x, int *y, int *z);
int   world_keep_inventory(const world_t *w);  int world_natural_regen(const world_t *w);
int   world_pvp(const world_t *w);             int world_fall_damage(const world_t *w);
int   world_hunger(const world_t *w);          int world_mob_spawning(const world_t *w);
int   world_mob_griefing(const world_t *w);    int world_daylight_cycle(const world_t *w);
void  world_set_weather_state(world_t *w, int s);  int world_weather_state(const world_t *w);
void  world_init_settings(world_t *w, int difficulty, int mob_spawning, float spawn_rate, int keep_inventory);

typedef struct {
    int (*save)(void *f);
    int (*load)(void *f, unsigned version);
} world_section_io_t;
void world_set_entity_io(world_section_io_t io);
void world_set_station_io(world_section_io_t io);
void world_set_players_io(world_section_io_t io);

#define WORLD_MAX_USERS 16
#define USER_NAME_MAX   24
typedef struct { char name[USER_NAME_MAX]; uint32_t allow_mask; uint8_t used; } user_perm_t;

void world_perm_set(world_t *w, const char *user, int cmd_idx, int on);
int  world_perm_allowed(const world_t *w, const char *user, int cmd_idx);
void world_perm_clear(world_t *w, const char *user);
