#pragma once

#include "vec3.h"

struct world_s;
typedef struct world_s world_t;

typedef struct {
    vec3 pos, vel;
    int  hit_x, hit_y, hit_z;
    int  grounded;
} aabb_move_t;

void aabb_move_resolve(aabb_move_t *m, float half_w, float height,
                       const world_t *world, float step_up, float dt);

int  aabb_overlaps_solid(vec3 pos, float half_w, float height, const world_t *world);
