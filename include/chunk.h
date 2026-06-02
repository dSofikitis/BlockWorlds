#pragma once

#include <stdint.h>

#define CHUNK_DIM    16
#define CHUNK_VOLUME (CHUNK_DIM * CHUNK_DIM * CHUNK_DIM)

typedef uint8_t block_t;

#define BLOCK_AIR          0
#define BLOCK_STONE        1
#define BLOCK_DIRT         2
#define BLOCK_GRASS        3
#define BLOCK_WATER        4
#define BLOCK_GRAVEL       5
#define BLOCK_DEEPSTONE    6
#define BLOCK_SAND         7
#define BLOCK_SNOW         8
#define BLOCK_WOOD         9
#define BLOCK_LEAVES       10
#define BLOCK_COAL_ORE     11
#define BLOCK_IRON_ORE     12
#define BLOCK_DIAMOND_ORE  13
#define BLOCK_GLOWSTONE    14
#define BLOCK_PLANKS       15
#define BLOCK_QUARTZ       16
#define BLOCK_CONCRETE     17
#define BLOCK_GLASS        18

typedef struct {
    block_t blocks[CHUNK_VOLUME];
} chunk_t;

void    chunk_init(chunk_t *c);
block_t chunk_get(const chunk_t *c, int x, int y, int z);
void    chunk_set(chunk_t *c, int x, int y, int z, block_t b);

void chunk_print_slice_y(const chunk_t *c, int y);