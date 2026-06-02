#include "audio_internal.h"

#if defined(_WIN32)

#include <windows.h>
#include <mmsystem.h>
#include <stdlib.h>
#include <string.h>

#define WO_BUFFERS 4
#define WO_FRAMES  512

typedef struct {
    HWAVEOUT        h;
    WAVEHDR         hdr[WO_BUFFERS];
    short          *buf[WO_BUFFERS];
    HANDLE          event;
    HANDLE          thread;
    volatile int    running;
    struct audio_s *a;
} winmm_backend_t;

static void wo_fill(winmm_backend_t *b, int idx) {
    float mix[WO_FRAMES * 2];
    audio_mix(b->a, mix, WO_FRAMES);
    short *out = b->buf[idx];
    for (int i = 0; i < WO_FRAMES * 2; i++) {
        float v = mix[i];
        if (v >  1.0f) v =  1.0f;
        if (v < -1.0f) v = -1.0f;
        out[i] = (short)(v * 32767.0f);
    }
}

static DWORD WINAPI wo_thread(LPVOID arg) {
    winmm_backend_t *b = (winmm_backend_t *)arg;
    while (b->running) {
        WaitForSingleObject(b->event, 100);
        for (int i = 0; i < WO_BUFFERS; i++) {
            if (b->hdr[i].dwFlags & WHDR_DONE) {
                wo_fill(b, i);
                waveOutWrite(b->h, &b->hdr[i], sizeof b->hdr[i]);
            }
        }
    }
    return 0;
}

int audio_backend_start(struct audio_s *a) {
    winmm_backend_t *b = calloc(1, sizeof *b);
    if (!b) return 0;
    b->a = a;

    WAVEFORMATEX wfx;
    memset(&wfx, 0, sizeof wfx);
    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = 2;
    wfx.nSamplesPerSec  = (DWORD)AUDIO_SR;
    wfx.wBitsPerSample  = 16;
    wfx.nBlockAlign     = (WORD)(wfx.nChannels * wfx.wBitsPerSample / 8);
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    b->event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!b->event) { free(b); return 0; }

    if (waveOutOpen(&b->h, WAVE_MAPPER, &wfx, (DWORD_PTR)b->event, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        CloseHandle(b->event); free(b); return 0;
    }

    for (int i = 0; i < WO_BUFFERS; i++) {
        b->buf[i] = calloc(WO_FRAMES * 2, sizeof(short));
        if (!b->buf[i]) goto fail;
        memset(&b->hdr[i], 0, sizeof b->hdr[i]);
        b->hdr[i].lpData         = (LPSTR)b->buf[i];
        b->hdr[i].dwBufferLength = (DWORD)(WO_FRAMES * 2 * sizeof(short));
        if (waveOutPrepareHeader(b->h, &b->hdr[i], sizeof b->hdr[i]) != MMSYSERR_NOERROR) goto fail;
        wo_fill(b, i);
        waveOutWrite(b->h, &b->hdr[i], sizeof b->hdr[i]);
    }

    b->running = 1;
    b->thread = CreateThread(NULL, 0, wo_thread, b, 0, NULL);
    if (!b->thread) goto fail;

    a->backend = b;
    return 1;

fail:
    waveOutReset(b->h);
    for (int i = 0; i < WO_BUFFERS; i++) {
        if (b->buf[i]) {
            if (b->hdr[i].dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(b->h, &b->hdr[i], sizeof b->hdr[i]);
            free(b->buf[i]);
        }
    }
    waveOutClose(b->h);
    CloseHandle(b->event);
    free(b);
    return 0;
}

void audio_backend_stop(struct audio_s *a) {
    winmm_backend_t *b = (winmm_backend_t *)a->backend;
    if (!b) return;
    b->running = 0;
    SetEvent(b->event);
    WaitForSingleObject(b->thread, INFINITE);
    CloseHandle(b->thread);
    waveOutReset(b->h);
    for (int i = 0; i < WO_BUFFERS; i++) {
        waveOutUnprepareHeader(b->h, &b->hdr[i], sizeof b->hdr[i]);
        free(b->buf[i]);
    }
    waveOutClose(b->h);
    CloseHandle(b->event);
    free(b);
    a->backend = NULL;
}

#endif

typedef int audio_winmm_translation_unit;