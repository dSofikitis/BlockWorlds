#pragma once

#include "chunk.h"

typedef struct audio_s audio_t;

audio_t *audio_create(const char *dir);
void     audio_destroy(audio_t *a);
void     audio_reload(audio_t *a, const char *dir);

int          audio_wav_count(void);
const char  *audio_wav_name(int i);

void audio_set_volume(audio_t *a, float v);

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