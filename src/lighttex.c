#include "glcompat.h"

#include "lighttex.h"
#include "world.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#define SNAP            32
#define DIRTY_COOLDOWN  0.20f

struct lighttex_s {
    int      dim;
    GLuint   tex;
    uint8_t *buf;
    int      ox, oy, oz;
    int      valid;
    int      dirty;
    float    cooldown;
};

static int snap_down(int v, int s) {
    int r = v % s;
    if (r < 0) r += s;
    return v - r;
}

lighttex_t *lighttex_create(int dim) {
    lighttex_t *lt = calloc(1, sizeof *lt);
    if (!lt) return NULL;
    lt->dim = dim;
    lt->buf = calloc((size_t)dim * (size_t)dim * (size_t)dim, 1);
    if (!lt->buf) { free(lt); return NULL; }

    glGenTextures(1, &lt->tex);
    glBindTexture(GL_TEXTURE_3D, lt->tex);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R8, dim, dim, dim, 0, GL_RED, GL_UNSIGNED_BYTE, lt->buf);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_3D, 0);

    lt->valid = 0;
    return lt;
}

void lighttex_destroy(lighttex_t *lt) {
    if (!lt) return;
    glDeleteTextures(1, &lt->tex);
    free(lt->buf);
    free(lt);
}

void lighttex_invalidate(lighttex_t *lt) { if (lt) { lt->valid = 0; lt->dirty = 0; } }
void lighttex_mark_dirty(lighttex_t *lt) { if (lt) lt->dirty = 1; }

static void rebuild(lighttex_t *lt, world_t *w, int ox, int oy, int oz) {
    world_build_light_volume(w, ox, oy, oz, lt->dim, lt->buf);
    lt->ox = ox; lt->oy = oy; lt->oz = oz;
    glBindTexture(GL_TEXTURE_3D, lt->tex);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, lt->dim, lt->dim, lt->dim,
                    GL_RED, GL_UNSIGNED_BYTE, lt->buf);
    glBindTexture(GL_TEXTURE_3D, 0);
    lt->valid    = 1;
    lt->dirty    = 0;
    lt->cooldown = DIRTY_COOLDOWN;
}

void lighttex_update(lighttex_t *lt, world_t *w, float cx, float cy, float cz, float dt) {
    if (!lt || !w) return;
    int half = lt->dim / 2;
    int ox = snap_down((int)floorf(cx) - half, SNAP);
    int oy = snap_down((int)floorf(cy) - half, SNAP);
    int oz = snap_down((int)floorf(cz) - half, SNAP);

    if (!lt->valid || ox != lt->ox || oy != lt->oy || oz != lt->oz) {
        rebuild(lt, w, ox, oy, oz);
        return;
    }
    if (lt->dirty) {
        lt->cooldown -= dt;
        if (lt->cooldown <= 0.0f) rebuild(lt, w, lt->ox, lt->oy, lt->oz);
    }
}

unsigned int lighttex_texture(const lighttex_t *lt) { return lt ? lt->tex : 0; }
int          lighttex_dim(const lighttex_t *lt)     { return lt ? lt->dim : 0; }
int          lighttex_valid(const lighttex_t *lt)   { return lt ? lt->valid : 0; }

void lighttex_origin(const lighttex_t *lt, float *ox, float *oy, float *oz) {
    if (!lt) { *ox = *oy = *oz = 0.0f; return; }
    *ox = (float)lt->ox;
    *oy = (float)lt->oy;
    *oz = (float)lt->oz;
}
