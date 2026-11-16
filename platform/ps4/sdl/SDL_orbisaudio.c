// SPDX-License-Identifier: MIT
// Native SDL3 audio backend for Project Prime, outside the vendored SDL tree.
#include "SDL_internal.h"
#include "audio/SDL_sysaudio.h"
#include <orbis/AudioOut.h>

struct SDL_PrivateAudioData {
    int port;
    Uint8 *buffers;
    unsigned next;
};
static SDL_AtomicInt ownership_unconfirmed;

static bool OrbisOpen(SDL_AudioDevice *device) {
    if (SDL_GetAtomicInt(&ownership_unconfirmed))
        return SDL_SetError("Native audio shutdown failed; restart the application before reopening audio");
    if (device->recording) return SDL_SetError("Orbis recording is not supported");
    device->spec.freq = 48000;
    device->spec.channels = 2;
    device->spec.format = SDL_AUDIO_S16LE;
    device->sample_frames = 256;
    SDL_UpdatedAudioDeviceFormat(device);
    device->hidden = SDL_calloc(1, sizeof(*device->hidden));
    if (!device->hidden) return false;
    device->hidden->port = -1;
    // Alternate two output blocks. sceAudioOutOutput paces the producer; a
    // block queued by the last call is not overwritten by the next SDL mix.
    device->hidden->buffers = SDL_aligned_alloc(64, device->buffer_size * 2);
    if (!device->hidden->buffers) return false;
    SDL_memset(device->hidden->buffers, 0, device->buffer_size * 2);
    sceAudioOutInit();
    device->hidden->port = sceAudioOutOpen(0xff, ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0,
        device->sample_frames, 48000, ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
    if (device->hidden->port < 0)
        return SDL_SetError("sceAudioOutOpen failed: 0x%08x", (unsigned)device->hidden->port);
    return true;
}

static Uint8 *OrbisBuffer(SDL_AudioDevice *device, int *size) {
    *size = device->buffer_size;
    Uint8 *buffer = device->hidden->buffers + device->hidden->next * device->buffer_size;
    device->hidden->next ^= 1;
    return buffer;
}

static bool OrbisWait(SDL_AudioDevice *device) { (void)device; return true; }

static bool OrbisPlay(SDL_AudioDevice *device, const Uint8 *buffer, int size) {
    if (size != device->buffer_size) return SDL_SetError("Orbis audio block size changed");
    int r = sceAudioOutOutput(device->hidden->port, buffer);
    return r >= 0 ? true : SDL_SetError("sceAudioOutOutput failed: 0x%08x", (unsigned)r);
}

static void OrbisClose(SDL_AudioDevice *device) {
    if (!device->hidden) return;
    // SDL has joined its callback thread before CloseDevice. Stop the native
    // output before freeing the final submitted buffer.
    if (device->hidden->port >= 0) {
        const int stopped = sceAudioOutOutput(device->hidden->port, NULL);
        const int closed = sceAudioOutClose(device->hidden->port);
        if (closed < 0) {
            // A failed close does not establish that native audio has returned
            // the last block. Retain it until process teardown, never reuse it.
            SDL_LogCritical(SDL_LOG_CATEGORY_AUDIO,
                "Orbis audio ownership unconfirmed; retaining output blocks (stop=0x%08x close=0x%08x)",
                (unsigned)stopped, (unsigned)closed);
            SDL_SetAtomicInt(&ownership_unconfirmed, 1);
            device->hidden->buffers = NULL;
        }
    }
    SDL_aligned_free(device->hidden->buffers);
    SDL_free(device->hidden);
    device->hidden = NULL;
}

static bool OrbisInit(SDL_AudioDriverImpl *impl) {
    impl->OpenDevice = OrbisOpen;
    impl->GetDeviceBuf = OrbisBuffer;
    impl->WaitDevice = OrbisWait;
    impl->PlayDevice = OrbisPlay;
    impl->CloseDevice = OrbisClose;
    impl->OnlyHasDefaultPlaybackDevice = true;
    return true;
}

AudioBootStrap ORBISAUDIO_bootstrap = {"orbis", "PS4 native audio", OrbisInit, false, false};
