#include "kilix.h"
#include "pcm_mixer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SFX_VARIANTS 6

typedef struct {
    int16_t *data;
    size_t frames;
} Sample;

static const char *const SFX_FILES[SFX_COUNT] = {
    [SFX_MOVE] = "sfx/move.wav",
    [SFX_CONFIRM] = "sfx/confirm.wav",
    [SFX_TRAIN] = "sfx/train.wav",
    [SFX_HIT] = "sfx/hit.wav",
    [SFX_WIN] = "sfx/win.wav",
    [SFX_LOSE] = "sfx/lose.wav"
};

static const uint8_t SFX_VARIANTS[SFX_COUNT] = {
    [SFX_MOVE] = 4,
    [SFX_CONFIRM] = 3,
    [SFX_TRAIN] = 4,
    [SFX_HIT] = 6,
    [SFX_WIN] = 3,
    [SFX_LOSE] = 3,
};

static Sample samples[SFX_COUNT][MAX_SFX_VARIANTS];
static uint8_t last_variants[SFX_COUNT];
static uint32_t sound_rng = 0x4b17c4e5u;
static pcmmix mixer;
static bool mixer_started;

static uint32_t sound_random_u32(void)
{
    sound_rng ^= sound_rng << 13;
    sound_rng ^= sound_rng >> 17;
    sound_rng ^= sound_rng << 5;
    return sound_rng;
}

static unsigned choose_variant(SoundId id)
{
    unsigned count = SFX_VARIANTS[id];
    if (count <= 1) return 0;
    unsigned variant;
    do variant = sound_random_u32() % count;
    while (variant == last_variants[id]);
    last_variants[id] = (uint8_t)variant;
    return variant;
}

static bool variant_filename(const char *base, unsigned variant,
                             char *out, size_t size)
{
    if (variant == 0) return snprintf(out, size, "%s", base) < (int)size;
    const char *extension = strrchr(base, '.');
    if (!extension) return false;
    return snprintf(out, size, "%.*s_v%02u%s", (int)(extension - base), base,
                    variant + 1, extension) < (int)size;
}

static void free_samples(void)
{
    for (int id = 0; id < SFX_COUNT; id++)
        for (int variant = 0; variant < MAX_SFX_VARIANTS; variant++) {
            pcmmix_wav_free(samples[id][variant].data);
            samples[id][variant] = (Sample){0};
        }
}

bool sound_init(void)
{
    pcmmix_options options;
    char relative[96], error[256];

    memset(last_variants, 0xff, sizeof last_variants);
    if (G.headless) return false;
    for (int id = 0; id < SFX_COUNT; id++) {
        for (unsigned variant = 0; variant < SFX_VARIANTS[id]; variant++) {
            if (!variant_filename(SFX_FILES[id], variant, relative,
                                  sizeof relative))
                continue;
            const char *path = asset_path(relative);
            samples[id][variant].data = pcmmix_wav_load(
                path, &samples[id][variant].frames, error, sizeof error);
        }
    }
    pcmmix_options_init(&options);
    if (!pcmmix_start(&mixer, &options)) {
        free_samples();
        return false;
    }
    mixer_started = true;
    return true;
}

void sound_play(SoundId id)
{
    if (!mixer_started || G.headless || !G.sound_on ||
        (int)id < 0 || id >= SFX_COUNT)
        return;
    Sample *sample = &samples[id][choose_variant(id)];
    if (!sample->data) return;
    pcmmix_sample clip = {sample->data, sample->frames};
    (void)pcmmix_play(&mixer, &clip, 1.0f, 1.0f);
}

void sound_shutdown(void)
{
    if (mixer_started) pcmmix_stop(&mixer);
    mixer_started = false;
    free_samples();
}
