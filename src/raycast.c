#include "raycast.h"
#include "world.h"
#include "chunk.h"
#include <math.h>

static int block_is_solid_for_raycast(block_t b) {
    return b != BLOCK_AIR && b != BLOCK_WATER;
}

static int floorf_to_int(float v) { return (int)floorf(v); }

raycast_hit_t raycast(const world_t *w, vec3 origin, vec3 dir, float max_dist) {
    raycast_hit_t hit = {0};

    int x = floorf_to_int(origin.x);
    int y = floorf_to_int(origin.y);
    int z = floorf_to_int(origin.z);

    int step_x = (dir.x > 0) ? 1 : -1;
    int step_y = (dir.y > 0) ? 1 : -1;
    int step_z = (dir.z > 0) ? 1 : -1;

    float t_delta_x = (dir.x != 0) ? fabsf(1.0f / dir.x) : 1e30f;
    float t_delta_y = (dir.y != 0) ? fabsf(1.0f / dir.y) : 1e30f;
    float t_delta_z = (dir.z != 0) ? fabsf(1.0f / dir.z) : 1e30f;

    float next_x_boundary = (step_x > 0) ? (float)(x + 1) : (float)x;
    float next_y_boundary = (step_y > 0) ? (float)(y + 1) : (float)y;
    float next_z_boundary = (step_z > 0) ? (float)(z + 1) : (float)z;

    float t_max_x = (dir.x != 0) ? (next_x_boundary - origin.x) / dir.x : 1e30f;
    float t_max_y = (dir.y != 0) ? (next_y_boundary - origin.y) / dir.y : 1e30f;
    float t_max_z = (dir.z != 0) ? (next_z_boundary - origin.z) / dir.z : 1e30f;

    int face_x = 0, face_y = 0, face_z = 0;

    if (block_is_solid_for_raycast(world_get_block(w, x, y, z))) {
        hit.hit = 1;
        hit.x = x; hit.y = y; hit.z = z;
        hit.face_x = -step_x; hit.face_y = 0; hit.face_z = 0;
        return hit;
    }

    float t = 0.0f;
    while (t <= max_dist) {
        if (t_max_x < t_max_y && t_max_x < t_max_z) {
            x += step_x;
            t = t_max_x;
            t_max_x += t_delta_x;
            face_x = -step_x; face_y = 0; face_z = 0;
        } else if (t_max_y < t_max_z) {
            y += step_y;
            t = t_max_y;
            t_max_y += t_delta_y;
            face_x = 0; face_y = -step_y; face_z = 0;
        } else {
            z += step_z;
            t = t_max_z;
            t_max_z += t_delta_z;
            face_x = 0; face_y = 0; face_z = -step_z;
        }

        if (t > max_dist) break;
        if (block_is_solid_for_raycast(world_get_block(w, x, y, z))) {
            hit.hit = 1;
            hit.x = x; hit.y = y; hit.z = z;
            hit.face_x = face_x; hit.face_y = face_y; hit.face_z = face_z;
            return hit;
        }
    }
    return hit;
}