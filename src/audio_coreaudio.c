#include "audio_internal.h"

#if defined(__APPLE__)

#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    AudioComponentInstance unit;
} ca_backend_t;

static OSStatus ca_render_cb(void *ref,
                             AudioUnitRenderActionFlags *flags,
                             const AudioTimeStamp *ts,
                             UInt32 bus, UInt32 nframes,
                             AudioBufferList *io) {
    (void)flags; (void)ts; (void)bus;
    struct audio_s *a = (struct audio_s *)ref;
    audio_mix(a, (float *)io->mBuffers[0].mData, (int)nframes);
    return noErr;
}

int audio_backend_start(struct audio_s *a) {
    ca_backend_t *b = calloc(1, sizeof *b);
    if (!b) return 0;

    AudioComponentDescription desc;
    memset(&desc, 0, sizeof desc);
    desc.componentType         = kAudioUnitType_Output;
    desc.componentSubType      = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent comp = AudioComponentFindNext(NULL, &desc);
    if (!comp) { free(b); return 0; }
    if (AudioComponentInstanceNew(comp, &b->unit) != noErr) { free(b); return 0; }

    AudioStreamBasicDescription fmt;
    memset(&fmt, 0, sizeof fmt);
    fmt.mSampleRate       = (Float64)AUDIO_SR;
    fmt.mFormatID         = kAudioFormatLinearPCM;
    fmt.mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    fmt.mFramesPerPacket  = 1;
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel   = 32;
    fmt.mBytesPerFrame    = (UInt32)(2 * sizeof(float));
    fmt.mBytesPerPacket   = fmt.mBytesPerFrame;

    AURenderCallbackStruct cb;
    memset(&cb, 0, sizeof cb);
    cb.inputProc       = ca_render_cb;
    cb.inputProcRefCon = a;

    OSStatus st;
    st = AudioUnitSetProperty(b->unit, kAudioUnitProperty_StreamFormat,
                              kAudioUnitScope_Input, 0, &fmt, (UInt32)sizeof fmt);
    if (st != noErr) goto fail;
    st = AudioUnitSetProperty(b->unit, kAudioUnitProperty_SetRenderCallback,
                              kAudioUnitScope_Input, 0, &cb, (UInt32)sizeof cb);
    if (st != noErr) goto fail;
    if (AudioUnitInitialize(b->unit) != noErr) goto fail;
    if (AudioOutputUnitStart(b->unit) != noErr) {
        AudioUnitUninitialize(b->unit);
        goto fail;
    }
    a->backend = b;
    return 1;

fail:
    AudioComponentInstanceDispose(b->unit);
    free(b);
    return 0;
}

void audio_backend_stop(struct audio_s *a) {
    ca_backend_t *b = (ca_backend_t *)a->backend;
    if (!b) return;
    AudioOutputUnitStop(b->unit);
    AudioUnitUninitialize(b->unit);
    AudioComponentInstanceDispose(b->unit);
    free(b);
    a->backend = NULL;
}

#endif

typedef int audio_coreaudio_translation_unit;
