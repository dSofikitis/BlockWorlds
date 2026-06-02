#include "audio_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define AUDIO_PI              3.14159265358979323846f
#define AUDIO_MASTER_GAIN     0.5f
#define AUDIO_LAND_MIN_SPEED  9.0f
#define AUDIO_LAND_HARD_SPEED 15.0f

typedef enum {
    SND_BREAK_HARD,
    SND_BREAK_SOFT,
    SND_PLACE,
    SND_STEP,
    SND_SPLASH,
    SND_EXIT_WATER,
    SND_WATER_STEP,
    SND_JUMP,
    SND_LAND_SOFT,
    SND_LAND_HARD,
    SND_SWIM,
    SND_PICKUP,
    SND_UI_CLICK,
    SND_STEP_STONE,
    SND_STEP_SAND,
    SND_STEP_WOOD,
    SND_BLOCK_LAND,
    SND_HOTBAR,
    SND_HOVER,
    SND_COUNT,
} sound_id_t;

_Static_assert(SND_COUNT == AUDIO_SOUND_COUNT, "sound count must match audio_internal.h");

static float rnd_unit(void) {
    return (float)rand() / (float)RAND_MAX * 2.0f - 1.0f;
}

static void synth_impact(sound_t *s, float dur, float decay, float lp_alpha,
                         float tone_hz, float tone_mix, float gain) {
    int n = (int)(dur * (float)AUDIO_SR);
    float *buf = malloc((size_t)n * sizeof *buf);
    if (!buf) { s->samples = NULL; s->frames = 0; return; }
    float lp = 0.0f;
    for (int i = 0; i < n; i++) {
        float t   = (float)i / (float)AUDIO_SR;
        float env = expf(-t / decay);
        lp += lp_alpha * (rnd_unit() - lp);
        float v = lp * (1.0f - tone_mix);
        if (tone_hz > 0.0f) v += sinf(2.0f * AUDIO_PI * tone_hz * t) * tone_mix;
        buf[i] = v * env * gain;
    }
    s->samples = buf;
    s->frames  = n;
}

static uint16_t rd_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

static uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static float *resample_to_sr(float *src, uint32_t src_frames, uint32_t src_sr, int *out_frames) {
    if (src_sr == (uint32_t)AUDIO_SR) {
        *out_frames = (int)src_frames;
        return src;
    }
    double ratio = (double)AUDIO_SR / (double)src_sr;
    int dst_frames = (int)((double)src_frames * ratio);
    if (dst_frames <= 0) { free(src); return NULL; }
    float *dst = malloc((size_t)dst_frames * sizeof *dst);
    if (!dst) { free(src); return NULL; }
    for (int i = 0; i < dst_frames; i++) {
        double sp = (double)i / ratio;
        int i0 = (int)sp;
        float frac = (float)(sp - (double)i0);
        float a0 = src[i0];
        float a1 = (i0 + 1 < (int)src_frames) ? src[i0 + 1] : a0;
        dst[i] = a0 + (a1 - a0) * frac;
    }
    free(src);
    *out_frames = dst_frames;
    return dst;
}

static int load_wav(sound_t *s, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long flen = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (flen < 44) { fclose(f); return 0; }
    size_t len = (size_t)flen;
    uint8_t *buf = malloc(len);
    if (!buf) { fclose(f); return 0; }
    size_t got = fread(buf, 1, len, f);
    fclose(f);
    if (got != len) { free(buf); return 0; }

    if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) {
        free(buf); return 0;
    }

    uint16_t tag = 0, channels = 0, bits = 0;
    uint32_t rate = 0, data_len = 0;
    const uint8_t *data = NULL;

    size_t off = 12;
    while (off + 8 <= len) {
        const uint8_t *ck = buf + off;
        uint32_t ck_size = rd_u32(ck + 4);
        const uint8_t *body = ck + 8;
        size_t avail = len - (off + 8);
        if (ck_size > avail) ck_size = (uint32_t)avail;
        if (memcmp(ck, "fmt ", 4) == 0 && ck_size >= 16) {
            tag      = rd_u16(body + 0);
            channels = rd_u16(body + 2);
            rate     = rd_u32(body + 4);
            bits     = rd_u16(body + 14);
        } else if (memcmp(ck, "data", 4) == 0) {
            data = body;
            data_len = ck_size;
        }
        off += 8 + ck_size + (ck_size & 1u);
    }

    int ok_fmt = (tag == 1 && bits == 16) || (tag == 3 && bits == 32);
    if (!data || channels == 0 || rate == 0 || !ok_fmt) { free(buf); return 0; }

    uint32_t stride = (uint32_t)(bits / 8u) * channels;
    uint32_t frames = stride ? data_len / stride : 0;
    if (frames == 0) { free(buf); return 0; }

    float *mono = malloc((size_t)frames * sizeof *mono);
    if (!mono) { free(buf); return 0; }
    for (uint32_t i = 0; i < frames; i++) {
        const uint8_t *fr = data + (size_t)i * stride;
        float acc = 0.0f;
        for (uint32_t c = 0; c < channels; c++) {
            if (tag == 1) {
                int16_t v = (int16_t)rd_u16(fr + (size_t)c * 2);
                acc += (float)v / 32768.0f;
            } else {
                uint32_t raw = rd_u32(fr + (size_t)c * 4);
                float fv;
                memcpy(&fv, &raw, sizeof fv);
                acc += fv;
            }
        }
        mono[i] = acc / (float)channels;
    }
    free(buf);

    int out_frames = 0;
    float *samples = resample_to_sr(mono, frames, rate, &out_frames);
    if (!samples) return 0;
    s->samples = samples;
    s->frames  = out_frames;
    return 1;
}

static void load_or_synth(sound_t *s, const char *file,
                          float dur, float decay, float lp,
                          float tone_hz, float tone_mix, float gain) {
    if (file && load_wav(s, file)) return;
    synth_impact(s, dur, decay, lp, tone_hz, tone_mix, gain);
}

typedef struct {
    const char *name;
    float dur, decay, lp, tone_hz, tone_mix, gain;
} sound_def_t;

static const sound_def_t SOUND_DEFS[SND_COUNT] = {
    { "break_hard.wav", 0.18f, 0.05f, 0.50f,   0.0f, 0.00f, 0.50f },
    { "break_soft.wav", 0.16f, 0.06f, 0.12f,  80.0f, 0.25f, 0.55f },
    { "place.wav",      0.12f, 0.04f, 0.25f, 140.0f, 0.50f, 0.50f },
    { "step.wav",       0.08f, 0.025f,0.10f,   0.0f, 0.00f, 0.28f },
    { "splash.wav",     0.30f, 0.10f, 0.45f,   0.0f, 0.00f, 0.40f },
    { "exit_water.wav", 0.20f, 0.07f, 0.40f,   0.0f, 0.00f, 0.34f },
    { "water_step.wav", 0.13f, 0.045f,0.40f,   0.0f, 0.00f, 0.26f },
    { "jump.wav",       0.10f, 0.05f, 0.18f, 220.0f, 0.20f, 0.35f },
    { "land_soft.wav",  0.12f, 0.045f,0.10f,  70.0f, 0.15f, 0.55f },
    { "land_hard.wav",  0.22f, 0.08f, 0.08f,  55.0f, 0.20f, 0.90f },
    { "swim.wav",       0.22f, 0.08f, 0.50f,   0.0f, 0.00f, 0.30f },
    { "pickup.wav",     0.10f, 0.05f, 0.50f, 700.0f, 0.65f, 0.35f },
    { "ui_click.wav",   0.045f,0.012f,0.70f,   0.0f, 0.00f, 0.45f },
    { "step_stone.wav", 0.07f, 0.020f,0.40f,   0.0f, 0.00f, 0.30f },
    { "step_sand.wav",  0.09f, 0.030f,0.06f,   0.0f, 0.00f, 0.24f },
    { "step_wood.wav",  0.08f, 0.025f,0.25f, 180.0f, 0.18f, 0.30f },
    { "block_land.wav", 0.18f, 0.060f,0.12f,  50.0f, 0.10f, 0.55f },
    { "hotbar.wav",     0.03f, 0.008f,0.85f,   0.0f, 0.00f, 0.30f },
    { "hover.wav",      0.025f,0.006f,0.80f,   0.0f, 0.00f, 0.18f },
};

static void build_sounds(sound_t *dest, const char *dir) {
    for (int i = 0; i < SND_COUNT; i++) {
        const sound_def_t *d = &SOUND_DEFS[i];
        char path[256];
        const char *file = NULL;
        if (dir && dir[0]) {
            snprintf(path, sizeof path, "%s/%s", dir, d->name);
            file = path;
        }
        load_or_synth(&dest[i], file, d->dur, d->decay, d->lp, d->tone_hz, d->tone_mix, d->gain);
    }
}

int audio_wav_count(void) { return SND_COUNT; }

const char *audio_wav_name(int i) {
    if (i < 0 || i >= SND_COUNT) return "";
    return SOUND_DEFS[i].name;
}

static void free_sounds(audio_t *a) {
    for (int i = 0; i < SND_COUNT; i++) {
        free(a->sounds[i].samples);
        a->sounds[i].samples = NULL;
    }
}

void audio_mix(struct audio_s *a, float *o, int nframes) {
    for (int i = 0; i < nframes; i++) { o[2 * i] = 0.0f; o[2 * i + 1] = 0.0f; }

    pthread_mutex_lock(&a->mutex);
    for (int v = 0; v < AUDIO_VOICES; v++) {
        voice_t *vc = &a->voices[v];
        if (!vc->active) continue;
        for (int i = 0; i < nframes; i++) {
            if (vc->pos >= vc->frames) { vc->active = 0; break; }
            float smp = vc->samples[vc->pos++] * vc->gain;
            o[2 * i]     += smp;
            o[2 * i + 1] += smp;
        }
    }
    pthread_mutex_unlock(&a->mutex);

    for (int i = 0; i < 2 * nframes; i++) {
        if (o[i] >  1.0f) o[i] =  1.0f;
        if (o[i] < -1.0f) o[i] = -1.0f;
    }
}

static void audio_trigger(audio_t *a, sound_id_t id, float gain) {
    if (!a || !a->have_unit) return;
    sound_t *s = &a->sounds[id];
    if (!s->samples || s->frames == 0) return;
    pthread_mutex_lock(&a->mutex);
    int slot = -1;
    for (int v = 0; v < AUDIO_VOICES; v++) {
        if (!a->voices[v].active) { slot = v; break; }
    }
    if (slot < 0) slot = 0;
    a->voices[slot].samples = s->samples;
    a->voices[slot].frames  = s->frames;
    a->voices[slot].pos     = 0;
    a->voices[slot].gain    = gain * a->master_gain;
    a->voices[slot].active  = 1;
    pthread_mutex_unlock(&a->mutex);
}

audio_t *audio_create(const char *dir) {
    audio_t *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    a->master_gain = AUDIO_MASTER_GAIN;
    build_sounds(a->sounds, dir);

    if (pthread_mutex_init(&a->mutex, NULL) != 0) {
        free_sounds(a); free(a); return NULL;
    }
    a->have_mutex = 1;

    if (!audio_backend_start(a)) {
        pthread_mutex_destroy(&a->mutex);
        free_sounds(a); free(a); return NULL;
    }
    a->have_unit = 1;
    return a;
}

void audio_reload(audio_t *a, const char *dir) {
    if (!a) return;
    sound_t fresh[SND_COUNT];
    memset(fresh, 0, sizeof fresh);
    build_sounds(fresh, dir);

    sound_t old[SND_COUNT];
    pthread_mutex_lock(&a->mutex);
    for (int v = 0; v < AUDIO_VOICES; v++) a->voices[v].active = 0;
    memcpy(old, a->sounds, sizeof old);
    memcpy(a->sounds, fresh, sizeof fresh);
    pthread_mutex_unlock(&a->mutex);

    for (int i = 0; i < SND_COUNT; i++) free(old[i].samples);
}

void audio_destroy(audio_t *a) {
    if (!a) return;
    if (a->have_unit) audio_backend_stop(a);
    if (a->have_mutex) pthread_mutex_destroy(&a->mutex);
    free_sounds(a);
    free(a);
}

static int block_is_hard(block_t b) {
    return b == BLOCK_STONE || b == BLOCK_DEEPSTONE || b == BLOCK_GRAVEL ||
           b == BLOCK_COAL_ORE || b == BLOCK_IRON_ORE || b == BLOCK_DIAMOND_ORE;
}

static sound_id_t step_sound_for_block(block_t b) {
    switch (b) {
        case BLOCK_STONE: case BLOCK_DEEPSTONE: case BLOCK_GRAVEL:
        case BLOCK_COAL_ORE: case BLOCK_IRON_ORE: case BLOCK_DIAMOND_ORE:
        case BLOCK_GLOWSTONE: case BLOCK_QUARTZ: case BLOCK_CONCRETE: case BLOCK_GLASS:
            return SND_STEP_STONE;
        case BLOCK_SAND:
            return SND_STEP_SAND;
        case BLOCK_WOOD: case BLOCK_PLANKS:
            return SND_STEP_WOOD;
        default:
            return SND_STEP;
    }
}

void audio_play_break(audio_t *a, block_t b) {
    audio_trigger(a, block_is_hard(b) ? SND_BREAK_HARD : SND_BREAK_SOFT, 1.0f);
}
void audio_play_place(audio_t *a, block_t b) { (void)b; audio_trigger(a, SND_PLACE, 1.0f); }
void audio_play_step(audio_t *a, block_t ground) { audio_trigger(a, step_sound_for_block(ground), 1.0f); }
void audio_play_water_step(audio_t *a)       { audio_trigger(a, SND_WATER_STEP, 1.0f); }
void audio_play_splash(audio_t *a)           { audio_trigger(a, SND_SPLASH, 1.0f); }
void audio_play_exit_water(audio_t *a)       { audio_trigger(a, SND_EXIT_WATER, 1.0f); }
void audio_play_jump(audio_t *a)             { audio_trigger(a, SND_JUMP, 1.0f); }
void audio_play_swim(audio_t *a)             { audio_trigger(a, SND_SWIM, 1.0f); }
void audio_play_pickup(audio_t *a)           { audio_trigger(a, SND_PICKUP, 1.0f); }
void audio_play_ui_click(audio_t *a)         { audio_trigger(a, SND_UI_CLICK, 1.0f); }
void audio_play_block_land(audio_t *a)       { audio_trigger(a, SND_BLOCK_LAND, 1.0f); }
void audio_play_hotbar(audio_t *a)           { audio_trigger(a, SND_HOTBAR, 1.0f); }
void audio_play_hover(audio_t *a)            { audio_trigger(a, SND_HOVER, 1.0f); }

void audio_play_land(audio_t *a, float fall_speed) {
    if (fall_speed < AUDIO_LAND_MIN_SPEED) return;
    if (fall_speed >= AUDIO_LAND_HARD_SPEED) { audio_trigger(a, SND_LAND_HARD, 1.0f); return; }
    float t = (fall_speed - AUDIO_LAND_MIN_SPEED) / (AUDIO_LAND_HARD_SPEED - AUDIO_LAND_MIN_SPEED);
    audio_trigger(a, SND_LAND_SOFT, 0.55f + 0.45f * t);
}

void audio_set_volume(audio_t *a, float v) {
    if (!a) return;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    a->master_gain = v;
}