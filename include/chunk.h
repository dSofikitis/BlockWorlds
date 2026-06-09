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
#define BLOCK_TORCH          19
#define BLOCK_CRAFTING_TABLE 20
#define BLOCK_FURNACE        21
#define BLOCK_FORGE          22
#define BLOCK_ANVIL          23
#define BLOCK_CHEST          24
#define BLOCK_BED_FOOT       25
#define BLOCK_BED_HEAD       26
#define BLOCK_FARMLAND       27
#define BLOCK_WHEAT_CROP     28
#define BLOCK_SAPLING        29
#define BLOCK_IRON_BLOCK     30
#define BLOCK_DIAMOND_BLOCK  31
#define BLOCK_COAL_BLOCK     32
#define BLOCK_COBBLESTONE    33
#define BLOCK_WOOL           34
#define BLOCK_LEAVES_PINE    35
#define BLOCK_LEAVES_ACACIA  36
#define BLOCK_LEAVES_SWAMP   37
#define BLOCK_LEAVES_JUNGLE  38
#define BLOCK_TALL_GRASS     39
#define BLOCK_WATER_SHALLOW  40
#define BLOCK_BREWING_STAND  41
#define BLOCK_ICE            42
#define BLOCK_COUNT          43

static inline int block_is_water(block_t b) {
    return b == BLOCK_WATER || b == BLOCK_WATER_SHALLOW;
}

typedef struct {
    block_t blocks[CHUNK_VOLUME];
} chunk_t;

void    chunk_init(chunk_t *c);
block_t chunk_get(const chunk_t *c, int x, int y, int z);
void    chunk_set(chunk_t *c, int x, int y, int z, block_t b);

void chunk_print_slice_y(const chunk_t *c, int y);
