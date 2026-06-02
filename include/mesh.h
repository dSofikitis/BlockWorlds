#pragma once

#include <stddef.h>

typedef struct {
    float x, y, z;
    float u, v;
    float tx, ty;
    float nx, ny, nz;
    float skylight;
    float blocklight;
    float ao;
} mesh_vertex_t;

typedef struct {
    mesh_vertex_t *verts;
    int            vert_count;
    int            vert_cap;
    unsigned int  *indices;
    int            idx_count;
    int            idx_cap;
    int            opaque_idx_count;
    int            glass_idx_count;
} mesh_buf_t;

void mesh_init(mesh_buf_t *m);
void mesh_free(mesh_buf_t *m);
void mesh_push_vertex(mesh_buf_t *m, mesh_vertex_t v);
void mesh_push_index(mesh_buf_t *m, unsigned int idx);