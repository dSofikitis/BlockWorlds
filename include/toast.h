#pragma once

#include <stdint.h>

#define TOAST_MAX_QUEUE 8

typedef enum {
    ACH_GET_WOOD, ACH_MAKE_PLANKS, ACH_CRAFTING_TABLE, ACH_MAKE_TOOL, ACH_MINE_STONE,
    ACH_GET_IRON, ACH_SMELT_IRON, ACH_MAKE_FURNACE, ACH_KILL_FIRST_MOB, ACH_PLANT_TORCH,
    ACH_MAKE_BED, ACH_SLEEP, ACH_DIAMONDS, ACH_LEVEL_UP, ACH_COUNT
} achievement_id_t;
_Static_assert(ACH_COUNT <= 32, "achievements must fit a u32 bitfield");

typedef struct toast_system_s toast_system_t;

toast_system_t *toast_create(void);
void toast_destroy(toast_system_t *ts);

int  toast_unlock(toast_system_t *ts, int achievement_id);
int  toast_is_unlocked(const toast_system_t *ts, int achievement_id);
uint32_t toast_bits(const toast_system_t *ts);
void toast_set_bits(toast_system_t *ts, uint32_t bits);

void toast_push(toast_system_t *ts, const char *title, const char *subtitle);

void toast_update(toast_system_t *ts, float dt);
void toast_render(const toast_system_t *ts, void *ui, void *text, unsigned int hud_atlas, float aspect);
