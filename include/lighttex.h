#pragma once

struct world_s;
typedef struct world_s world_t;

typedef struct lighttex_s lighttex_t;

lighttex_t *lighttex_create(int dim);
void        lighttex_destroy(lighttex_t *lt);

void lighttex_invalidate(lighttex_t *lt);

void lighttex_mark_dirty(lighttex_t *lt);

void lighttex_update(lighttex_t *lt, world_t *w, float cx, float cy, float cz, float dt);

unsigned int lighttex_texture(const lighttex_t *lt);
int          lighttex_dim(const lighttex_t *lt);
int          lighttex_valid(const lighttex_t *lt);
void         lighttex_origin(const lighttex_t *lt, float *ox, float *oy, float *oz);
