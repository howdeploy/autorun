/* Copyright 2026 Wine-NX contributors. LGPL-2.1-or-later. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "initguid.h"

static int fail_realloc;

static void *test_realloc(void *pointer, size_t size)
{
    return fail_realloc ? NULL : realloc(pointer, size);
}

#define realloc test_realloc
#include "../../dlls/dsound/mixer.c"
#undef realloc

BOOL secondarybuffer_is_audible(IDirectSoundBufferImpl *buffer)
{
    (void)buffer;
    return TRUE;
}

static void expect_silence(const float *buffer, size_t count)
{
    for (size_t i = 0; i < count; ++i) assert(buffer[i] == 0.0f);
}

int main(void)
{
    WAVEFORMATEX input = {WAVE_FORMAT_PCM, 2, 48000, 192000, 4, 16, 0};
    WAVEFORMATEX output = {WAVE_FORMAT_IEEE_FLOAT, 2, 48000, 384000, 8, 32, 0};
    DirectSoundDevice device = {0};
    IDirectSoundBufferImpl buffer = {0};
    float *old;

    device.pwfx = &output;
    buffer.device = &device;
    buffer.pwfx = &input;
    buffer.state = STATE_PLAYING;
    buffer.playflags = DSBPLAY_LOOPING;
    buffer.buflen = 1024;
    buffer.mix_channels = 2;
    buffer.freqAdjustNum = buffer.freqAdjustDen = 48000;

    /* An empty filter input may have no circular buffer or converter yet. */
    get_samples(&buffer, NULL, 0, 0, 0, 0, sizeof(float), NULL, NULL);
    buffer.num_filters = 1;
    filter_samples(&buffer, NULL, 0, 0, 0, NULL);
    buffer.num_filters = 0;

    device.tmp_buffer = malloc(16);
    assert(device.tmp_buffer);
    device.tmp_buffer_len = 16;
    old = device.tmp_buffer;
    fail_realloc = 1;
    assert(!DSOUND_MixToTemporary(&buffer, 64));
    assert(device.tmp_buffer == old && device.tmp_buffer_len == 16);

    fail_realloc = 0;
    device.tmp_buffer = realloc(device.tmp_buffer, 128 * sizeof(float));
    assert(device.tmp_buffer);
    device.tmp_buffer_len = 128 * sizeof(float);
    fail_realloc = 1;
    memset(device.tmp_buffer, 0x55, device.tmp_buffer_len);
    assert(DSOUND_MixToTemporary(&buffer, 64));
    assert(!device.cp_buffer && !device.cp_buffer_len);
    expect_silence(device.tmp_buffer, 128);

    device.cp_buffer = malloc(sizeof(float));
    assert(device.cp_buffer);
    device.cp_buffer_len = sizeof(float);
    old = device.cp_buffer;
    buffer.freqAdjustNum = 44100;
    buffer.freqAccNum = 0;
    memset(device.tmp_buffer, 0x55, device.tmp_buffer_len);
    assert(DSOUND_MixToTemporary(&buffer, 64));
    assert(device.cp_buffer == old && device.cp_buffer_len == sizeof(float));
    assert(buffer.freqAccNum == (64 * 44100) % 48000);
    assert(!buffer.input_tail_valid);
    expect_silence(device.tmp_buffer, 128);

    buffer.num_filters = 1;
    buffer.freqAccNum = 0;
    memset(device.tmp_buffer, 0x55, device.tmp_buffer_len);
    assert(DSOUND_MixToTemporary(&buffer, 64));
    assert(!device.filter_buffer && !device.filter_buffer_len);
    assert(buffer.freqAccNum == (64 * 44100) % 48000);
    expect_silence(device.tmp_buffer, 128);

    free(device.cp_buffer);
    free(device.tmp_buffer);
    puts("DirectSound mixer: empty reads and allocation failure paths passed");
    return 0;
}
