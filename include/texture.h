#pragma once

#include <stdint.h>

#define ATLAS_TILES   16
#define ATLAS_TILE_PX 16
#define ATLAS_SIZE_PX (ATLAS_TILES * ATLAS_TILE_PX)

#define ITEM_ATLAS_TILES   16
#define ITEM_ATLAS_SIZE_PX (ITEM_ATLAS_TILES * ATLAS_TILE_PX)
#define HUD_ATLAS_TILES    8
#define HUD_ATLAS_SIZE_PX  (HUD_ATLAS_TILES * ATLAS_TILE_PX)

#define MOB_ATLAS_TILES    16
#define MOB_ATLAS_SIZE_PX  (MOB_ATLAS_TILES * ATLAS_TILE_PX)

unsigned int texture_create_atlas(const char *atlas_source);
unsigned int texture_create_item_atlas(void);
unsigned int texture_create_hud_atlas(void);
unsigned int texture_create_mob_atlas(void);

unsigned int texture_upload_rgba(const uint8_t *data, int w, int h);
void texture_paint_extra_blocks(uint8_t *atlas);
