#pragma once

#include <stdint.h>
#include "vec3.h"
#include "mat4.h"
#include "registry.h"

struct world_s;            typedef struct world_s world_t;
struct particle_system_s;  typedef struct particle_system_s particle_system_t;
struct audio_s;            typedef struct audio_s audio_t;

#define MAX_ENTITIES 768

typedef enum { EK_NONE, EK_MOB, EK_ITEM, EK_XP_ORB, EK_ARROW, EK_THROWN, EK_CLOUD, EK_KIND_COUNT } entity_kind_t;
typedef enum { SP_PIG, SP_COW, SP_CHICKEN, SP_SHEEP, SP_ZOMBIE, SP_SKELETON, SP_CREEPER, SP_SPIDER, SP_COUNT } species_t;
#define SPECIES_FIRST_HOSTILE SP_ZOMBIE
typedef enum { AI_IDLE, AI_WANDER, AI_FLEE, AI_CHASE, AI_ATTACK, AI_STRAFE, AI_FUSE, AI_DEAD } ai_state_t;

typedef struct {
    entity_kind_t kind;
    uint8_t active, on_ground, in_water;
    vec3  pos, vel;
    float yaw, head_yaw, head_pitch, anim_phase, age;
    float health, hurt_flash, knockback_resist;
    species_t  species;
    ai_state_t ai;
    float state_timer, attack_cooldown;
    vec3  wander_target;
    int32_t target_eid;
    item_id  item_id_payload;
    uint8_t  item_count;
    uint16_t item_durability;
    uint16_t xp_amount;
    float    bob_phase, pickup_delay;
    int32_t  shooter_eid;
} entity_t;

typedef struct {
    float max_hp, speed, atk_dmg, atk_range, sight, w, h, knock_resist;
    uint8_t hostile;
    int xp_min, xp_max;
    uint8_t needs_dark, needs_grass, surface_only;
    int herd_min, herd_max;
} mob_def_t;
extern const mob_def_t MOB_DEFS[SP_COUNT];

typedef struct {
    entity_t  e[MAX_ENTITIES];
    int       count;
    particle_system_t *ps;
    audio_t  *audio;
    float     spawn_timer;
    uint32_t  rng;
} entity_system_t;

typedef struct {
    vec3 pos, eye, forward;
    int  difficulty, pvp_enabled;
    int  player_invulnerable;
    int  player_targetable;
    int  (*try_pickup)(item_id id, int count, uint16_t durability);
    void (*grant_xp)(int amount);
    void (*hurt_player)(float dmg, int damage_type, vec3 src);
    void (*break_block)(int x, int y, int z);
    void (*apply_effect)(int effect, int ticks, int amp);
} player_ctx_t;

void entity_spawn_thrown(entity_system_t *es, vec3 pos, vec3 vel, item_id potion_id, uint16_t meta);

void entity_spawn_arrow(entity_system_t *es, vec3 pos, vec3 vel, int shooter_eid, item_id tip);

typedef struct {
    vec3     eye;
    int      eye_in_water;
    float    eye_water_depth;
    float    sea_level;
    vec3     fog_color;
    float    fog_density;
    mat4     light_vp;
    int      shadow_enabled;
    float    shadow_texel;
    float    shadow_world_texel;
    unsigned shadow_map_tex;
    unsigned blocklight_tex;
    int      blocklight_enabled;
    vec3     light_origin;
    float    light_dim;
} mob_render_env_t;

entity_system_t *entity_create(void);
void entity_destroy(entity_system_t *es);
void entity_set_fx(entity_system_t *es, particle_system_t *ps, audio_t *a);

void entity_spawn_mob(entity_system_t *es, int species, vec3 pos);
void entity_spawn_item(entity_system_t *es, vec3 pos, item_id id, int count, uint16_t durability);
void entity_spawn_xp(entity_system_t *es, vec3 pos, int amount);

void entity_update(entity_system_t *es, world_t *world, player_ctx_t *pc, float dt);
void entity_render(const entity_system_t *es, mat4 pv, vec3 sun_dir, float sun_strength,
                   float ambient, const mob_render_env_t *env);

void entity_render_avatars(mat4 pv, vec3 sun_dir, float sun_strength, float ambient,
                           const vec3 *pos, const float *yaw, const float *anim, int count,
                           const mob_render_env_t *env);

int  entity_player_attack(entity_system_t *es, world_t *world, player_ctx_t *pc,
                          vec3 eye, vec3 dir, float reach, int damage, float kb, int crit);

void entity_spawn_pass(entity_system_t *es, world_t *world, vec3 player_pos,
                       int is_night, float spawn_rate, int difficulty, int mob_spawning);

int  entity_active_count(const entity_system_t *es);
int  entity_count_hostile(const entity_system_t *es);

int  entity_save(void *f);
int  entity_load(void *f, unsigned version);
