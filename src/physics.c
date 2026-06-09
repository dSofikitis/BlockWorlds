#include "physics.h"
#include "world.h"
#include "chunk.h"
#include "registry.h"
#include <math.h>

#define PHYS_EPSILON 1e-4f

static int block_is_solid(block_t b) {
    return !block_passable(b);
}

static int box_overlaps_solid(vec3 pos, float half_w, float height,
                              const world_t *world) {
    int min_x = (int)floorf(pos.x - half_w);
    int max_x = (int)ceilf (pos.x + half_w) - 1;
    int min_y = (int)floorf(pos.y);
    int max_y = (int)ceilf (pos.y + height) - 1;
    int min_z = (int)floorf(pos.z - half_w);
    int max_z = (int)ceilf (pos.z + half_w) - 1;

    for (int y = min_y; y <= max_y; y++)
        for (int z = min_z; z <= max_z; z++)
            for (int x = min_x; x <= max_x; x++)
                if (block_is_solid(world_get_block(world, x, y, z))) return 1;
    return 0;
}

int aabb_overlaps_solid(vec3 pos, float half_w, float height,
                        const world_t *world) {
    return box_overlaps_solid(pos, half_w, height, world);
}

static int feet_supported(vec3 pos, float half_w, const world_t *world) {
    int min_x = (int)floorf(pos.x - half_w);
    int max_x = (int)ceilf (pos.x + half_w) - 1;
    int min_z = (int)floorf(pos.z - half_w);
    int max_z = (int)ceilf (pos.z + half_w) - 1;
    int y = (int)floorf(pos.y - 0.05f);

    for (int z = min_z; z <= max_z; z++)
        for (int x = min_x; x <= max_x; x++)
            if (block_is_solid(world_get_block(world, x, y, z))) return 1;
    return 0;
}

void aabb_move_resolve(aabb_move_t *m, float half_w, float height,
                       const world_t *world, float step_up, float dt) {
    m->hit_x = 0;
    m->hit_y = 0;
    m->hit_z = 0;

    int can_step = step_up > 0.0f;

    float dx = m->vel.x * dt;
    if (dx != 0.0f) {
        vec3 t = m->pos;
        t.x += dx;
        if (box_overlaps_solid(t, half_w, height, world)) {
            vec3 up = t;
            up.y += step_up;
            if (can_step && m->grounded &&
                !box_overlaps_solid(up, half_w, height, world)) {
                m->pos.x = t.x;
                m->pos.y = up.y;
            } else {
                if (dx > 0.0f) {
                    int b = (int)floorf(t.x + half_w);
                    m->pos.x = (float)b - half_w - PHYS_EPSILON;
                } else {
                    int b = (int)floorf(t.x - half_w);
                    m->pos.x = (float)(b + 1) + half_w + PHYS_EPSILON;
                }
                m->vel.x = 0.0f;
                m->hit_x = 1;
            }
        } else {
            m->pos.x = t.x;
        }
    }

    float dz = m->vel.z * dt;
    if (dz != 0.0f) {
        vec3 t = m->pos;
        t.z += dz;
        if (box_overlaps_solid(t, half_w, height, world)) {
            vec3 up = t;
            up.y += step_up;
            if (can_step && m->grounded &&
                !box_overlaps_solid(up, half_w, height, world)) {
                m->pos.z = t.z;
                m->pos.y = up.y;
            } else {
                if (dz > 0.0f) {
                    int b = (int)floorf(t.z + half_w);
                    m->pos.z = (float)b - half_w - PHYS_EPSILON;
                } else {
                    int b = (int)floorf(t.z - half_w);
                    m->pos.z = (float)(b + 1) + half_w + PHYS_EPSILON;
                }
                m->vel.z = 0.0f;
                m->hit_z = 1;
            }
        } else {
            m->pos.z = t.z;
        }
    }

    float dy = m->vel.y * dt;
    if (dy != 0.0f) {
        vec3 t = m->pos;
        t.y += dy;
        if (box_overlaps_solid(t, half_w, height, world)) {
            if (dy > 0.0f) {
                int b = (int)floorf(t.y + height);
                m->pos.y = (float)b - height - PHYS_EPSILON;
            } else {
                int b = (int)floorf(t.y);
                m->pos.y = (float)(b + 1) + PHYS_EPSILON;
            }
            m->vel.y = 0.0f;
            m->hit_y = 1;
        } else {
            m->pos.y = t.y;
        }
    }

    m->grounded = feet_supported(m->pos, half_w, world) && (m->vel.y <= 0.01f);
}
