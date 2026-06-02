#pragma once

#include "audio.h"
#include <pthread.h>

#define AUDIO_SR          48000
#define AUDIO_VOICES      16
#define AUDIO_SOUND_COUNT 19

typedef struct {
    float *samples;
    int    frames;
} sound_t;

typedef struct {
    const float *samples;
    int    frames;
    int    pos;
    float  gain;
    int    active;
} voice_t;

struct audio_s {
    pthread_mutex_t mutex;
    int             have_unit;
    int             have_mutex;
    void           *backend;
    float           master_gain;
    sound_t         sounds[AUDIO_SOUND_COUNT];
    voice_t         voices[AUDIO_VOICES];
};

void audio_mix(struct audio_s *a, float *out_stereo, int frames);

int  audio_backend_start(struct audio_s *a);
void audio_backend_stop(struct audio_s *a);