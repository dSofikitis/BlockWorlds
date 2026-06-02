#pragma once

#include "vec3.h"

struct world_s;
typedef struct world_s world_t;

typedef struct {
    int hit;
    int x, y, z;
    int face_x, face_y, face_z;
} raycast_hit_t;

raycast_hit_t raycast(const world_t *w, vec3 origin, vec3 dir, float max_dist);