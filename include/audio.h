#pragma once

#include "chunk.h"

typedef struct audio_s audio_t;

audio_t *audio_create(const char *dir);
void     audio_destroy(audio_t *a);
void     audio_reload(audio_t *a, const char *dir);

int          audio_wav_count(void);
const char  *audio_wav_name(int i);

typedef enum { AUDIO_CAT_UI, AUDIO_CAT_MOBS, AUDIO_CAT_PLAYER, AUDIO_CAT_ENV, AUDIO_CAT_COUNT } audio_category_t;

void audio_set_volume(audio_t *a, float v);
void audio_set_category_volume(audio_t *a, int category, float v);
void audio_play_rain(audio_t *a, float intensity);

void audio_play_break(audio_t *a, block_t b);
void audio_play_place(audio_t *a, block_t b);
void audio_play_step(audio_t *a, block_t ground);
void audio_play_water_step(audio_t *a);
void audio_play_block_land(audio_t *a);
void audio_play_hotbar(audio_t *a);
void audio_play_hover(audio_t *a);
void audio_play_splash(audio_t *a);
void audio_play_exit_water(audio_t *a);
void audio_play_jump(audio_t *a);
void audio_play_land(audio_t *a, float fall_speed);
void audio_play_swim(audio_t *a);
void audio_play_pickup(audio_t *a);
void audio_play_ui_click(audio_t *a);
void audio_play_item_break(audio_t *a);

enum {
    AUDIO_MOB_PIG,
    AUDIO_MOB_COW,
    AUDIO_MOB_CHICKEN,
    AUDIO_MOB_SHEEP,
    AUDIO_MOB_ZOMBIE,
    AUDIO_MOB_SKELETON,
    AUDIO_MOB_CREEPER,
    AUDIO_MOB_SPIDER,
};

void audio_play_hurt(audio_t *a);
void audio_play_eat(audio_t *a);
void audio_play_drink(audio_t *a);
void audio_play_burp(audio_t *a);
void audio_play_levelup(audio_t *a);
void audio_play_xp_pickup(audio_t *a);
void audio_play_anvil(audio_t *a);
void audio_play_door(audio_t *a, int open);
void audio_play_bow(audio_t *a);
void audio_play_arrow_hit(audio_t *a);
void audio_play_explosion(audio_t *a);
void audio_play_fuse(audio_t *a);
void audio_play_mob_idle(audio_t *a, int category);
void audio_play_mob_hurt(audio_t *a, int category);
void audio_play_mob_death(audio_t *a, int category);
