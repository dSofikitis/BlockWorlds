#pragma once

#include "mesh.h"

struct world_s;
typedef struct world_s world_t;

void chunk_to_mesh(const world_t *world, int cx, int cy, int cz, mesh_buf_t *out);
