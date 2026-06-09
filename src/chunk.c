#include "chunk.h"
#include <stdio.h>
#include <string.h>

static inline int chunk_index(int x, int y, int z) {
    return x + z * CHUNK_DIM + y * CHUNK_DIM * CHUNK_DIM;
}

static inline int chunk_in_bounds(int x, int y, int z) {
    return x >= 0 && x < CHUNK_DIM
        && y >= 0 && y < CHUNK_DIM
        && z >= 0 && z < CHUNK_DIM;
}

void chunk_init(chunk_t *c) {
    memset(c->blocks, BLOCK_AIR, sizeof c->blocks);
}

block_t chunk_get(const chunk_t *c, int x, int y, int z) {
    if (!chunk_in_bounds(x, y, z)) return BLOCK_AIR;
    return c->blocks[chunk_index(x, y, z)];
}

void chunk_set(chunk_t *c, int x, int y, int z, block_t b) {
    if (!chunk_in_bounds(x, y, z)) return;
    c->blocks[chunk_index(x, y, z)] = b;
}

void chunk_print_slice_y(const chunk_t *c, int y) {
    static const char glyphs[] = ".#=:~,%S*Tl@oD";
    static const size_t n_glyphs = sizeof glyphs - 1;

    printf("--- Y=%d ---\n", y);
    for (int z = CHUNK_DIM - 1; z >= 0; z--) {
        for (int x = 0; x < CHUNK_DIM; x++) {
            block_t b = chunk_get(c, x, y, z);
            char g = (b < n_glyphs) ? glyphs[b] : '?';
            putchar(g);
            putchar(' ');
        }
        putchar('\n');
    }
}
