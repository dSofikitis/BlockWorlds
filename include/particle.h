#pragma once

#include "vec3.h"
#include "mat4.h"
#include "registry.h"

struct world_s;
typedef struct world_s world_t;

#define PARTICLE_MAX 2048

typedef enum {
    PK_SHARD, PK_DUST, PK_SPLASH, PK_CRUMB, PK_CRIT, PK_HIT, PK_POOF,
    PK_SMOKE, PK_FLASH, PK_XP, PK_FLAME, PK_BUBBLE, PK_KIND_COUNT
} particle_kind_t;

typedef struct particle_system_s particle_system_t;

particle_system_t *particle_create(void);
void particle_destroy(particle_system_t *ps);
void particle_set_atlas(particle_system_t *ps, unsigned int hud_atlas_tex);
void particle_update(particle_system_t *ps, const world_t *world, float dt);
void particle_render(const particle_system_t *ps, mat4 pv, vec3 cam_right, vec3 cam_up,
                     vec3 sun_dir, float sun_strength, float ambient);
int  particle_active_count(const particle_system_t *ps);

void particle_block_break(particle_system_t *ps, int bx, int by, int bz, block_t b);
void particle_block_hit(particle_system_t *ps, int bx, int by, int bz, int fx, int fy, int fz, block_t b);
void particle_footstep(particle_system_t *ps, float x, float y, float z, block_t ground);
void particle_land_dust(particle_system_t *ps, float x, float y, float z, block_t ground, float strength);
void particle_water_splash(particle_system_t *ps, float x, float y, float z);
void particle_eat(particle_system_t *ps, float x, float y, float z, item_id food);
void particle_crit(particle_system_t *ps, float x, float y, float z);
void particle_hit(particle_system_t *ps, float x, float y, float z);
void particle_mob_death(particle_system_t *ps, float x, float y, float z);
void particle_explosion(particle_system_t *ps, float x, float y, float z, float radius);
void particle_xp_sparkle(particle_system_t *ps, float x, float y, float z);
void particle_torch_flame(particle_system_t *ps, float x, float y, float z);
