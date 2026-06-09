#include "audio_internal.h"

#if defined(__linux__)

#include <alsa/asoundlib.h>
#include <pthread.h>
#include <stdlib.h>

#define ALSA_PERIOD 256

typedef struct {
    snd_pcm_t      *pcm;
    pthread_t       thread;
    volatile int    running;
    struct audio_s *a;
} alsa_backend_t;

static void *alsa_thread(void *arg) {
    alsa_backend_t *b = (alsa_backend_t *)arg;
    float buf[ALSA_PERIOD * 2];
    while (b->running) {
        audio_mix(b->a, buf, ALSA_PERIOD);
        snd_pcm_sframes_t n = snd_pcm_writei(b->pcm, buf, ALSA_PERIOD);
        if (n < 0) snd_pcm_recover(b->pcm, (int)n, 1);
    }
    return NULL;
}

int audio_backend_start(struct audio_s *a) {
    alsa_backend_t *b = calloc(1, sizeof *b);
    if (!b) return 0;
    b->a = a;
    if (snd_pcm_open(&b->pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        free(b); return 0;
    }
    if (snd_pcm_set_params(b->pcm, SND_PCM_FORMAT_FLOAT_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                           2, (unsigned)AUDIO_SR, 1, 40000) < 0) {
        snd_pcm_close(b->pcm); free(b); return 0;
    }
    b->running = 1;
    if (pthread_create(&b->thread, NULL, alsa_thread, b) != 0) {
        snd_pcm_close(b->pcm); free(b); return 0;
    }
    a->backend = b;
    return 1;
}

void audio_backend_stop(struct audio_s *a) {
    alsa_backend_t *b = (alsa_backend_t *)a->backend;
    if (!b) return;
    b->running = 0;
    pthread_join(b->thread, NULL);
    snd_pcm_drain(b->pcm);
    snd_pcm_close(b->pcm);
    free(b);
    a->backend = NULL;
}

#endif

typedef int audio_alsa_translation_unit;
