#pragma once

#include <stdint.h>
#include "registry.h"

struct world_s;
typedef struct world_s world_t;

#define CRAFT_GRID_MAX 9
typedef enum { RECIPE_SHAPED, RECIPE_SHAPELESS } recipe_kind_t;
typedef struct {
    item_id  out_id;
    int16_t  out_count;
    uint8_t  kind;
    uint8_t  w, h;
    item_id  cell[CRAFT_GRID_MAX];
    uint8_t  n_ing;
    uint8_t  requires_3x3;
    uint8_t  mirror_ok;
    uint8_t  forge_only;
} recipe_t;

extern const recipe_t RECIPES[];
extern const int RECIPE_COUNT;

const recipe_t *craft_match(const inv_slot_t *grid, int gw, int gh, int is_forge);
void craft_consume_one(inv_slot_t *grid, int gw, int gh, const recipe_t *r);

typedef enum { ST_NONE, ST_FURNACE, ST_FORGE, ST_CHEST, ST_BREWING } station_type_t;

typedef struct { inv_slot_t slot[3]; float burn_left, burn_max, cook_progress, xp_bank; } furnace_state_t;
typedef struct { inv_slot_t grid[9], repair_in, repair_mat; float burn_left, burn_max; } forge_state_t;
typedef struct { inv_slot_t slot[5]; float time_left, time_max; uint8_t fuel; } brewing_state_t;

typedef struct {
    int32_t x, y, z;
    uint8_t type;
    uint8_t active;
    union {
        furnace_state_t furnace;
        forge_state_t   forge;
        inv_slot_t      chest[27];
        brewing_state_t brewing;
    } u;
} station_t;

item_id brew_result(item_id input, item_id ingredient);

void       craft_stations_init(void);
station_t *craft_station_get(int x, int y, int z);
station_t *craft_station_create(int x, int y, int z, station_type_t type);
void       craft_station_remove(int x, int y, int z);
void       craft_stations_tick(world_t *world, float dt, float smelt_mult);
int        craft_stations_count(void);
station_t *craft_station_at_index(int i);
int        craft_station_is_lit(int x, int y, int z);

int craft_stations_save(void *f);
int craft_stations_load(void *f, unsigned version);

int anvil_combine(inv_slot_t left, inv_slot_t right, inv_slot_t *out, int *xp_cost);
int forge_repair(inv_slot_t *tool, inv_slot_t mat);
