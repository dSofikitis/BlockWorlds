#pragma once

#include "mat4.h"
#include "vec3.h"
#include "chunk.h"

struct world_s;
typedef struct world_s world_t;

typedef struct falling_system_s falling_system_t;

falling_system_t *falling_create(void);
void              falling_destroy(falling_system_t *s);

void falling_spawn(falling_system_t *s, int x, int y, int z, block_t type);

int falling_update(falling_system_t *s, world_t *world, float dt);

void falling_render(const falling_system_t *s, mat4 pv,
                    vec3 sun_dir, float sun_strength, float ambient);

int falling_active_count(const falling_system_t *s);
