#include "mesh.h"
#include <stdio.h>
#include <stdlib.h>

void mesh_init(mesh_buf_t *m) {
    m->verts            = NULL;
    m->vert_count       = 0;
    m->vert_cap         = 0;
    m->indices          = NULL;
    m->idx_count        = 0;
    m->idx_cap          = 0;
    m->opaque_idx_count = 0;
    m->glass_idx_count  = 0;
}

void mesh_free(mesh_buf_t *m) {
    free(m->verts);
    free(m->indices);
    mesh_init(m);
}

void mesh_push_vertex(mesh_buf_t *m, mesh_vertex_t v) {
    if (m->vert_count >= m->vert_cap) {
        int new_cap = m->vert_cap ? m->vert_cap * 2 : 128;
        mesh_vertex_t *new_verts = realloc(m->verts, (size_t)new_cap * sizeof *new_verts);
        if (!new_verts) {
            fprintf(stderr, "mesh: out of memory growing vertex buffer\n");
            exit(EXIT_FAILURE);
        }
        m->verts    = new_verts;
        m->vert_cap = new_cap;
    }
    m->verts[m->vert_count++] = v;
}

void mesh_push_index(mesh_buf_t *m, unsigned int idx) {
    if (m->idx_count >= m->idx_cap) {
        int new_cap = m->idx_cap ? m->idx_cap * 2 : 256;
        unsigned int *new_idx = realloc(m->indices, (size_t)new_cap * sizeof *new_idx);
        if (!new_idx) {
            fprintf(stderr, "mesh: out of memory growing index buffer\n");
            exit(EXIT_FAILURE);
        }
        m->indices = new_idx;
        m->idx_cap = new_cap;
    }
    m->indices[m->idx_count++] = idx;
}
