#pragma once

#define ATLAS_TILES   5
#define ATLAS_TILE_PX 16
#define ATLAS_SIZE_PX (ATLAS_TILES * ATLAS_TILE_PX)

unsigned int texture_create_atlas(const char *atlas_source);