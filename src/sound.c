#include "kilix.h"
#include "pcmmix_bank.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static pcmmix_bank sound_bank;
static pcmmix mixer;
static bool mixer_started;

static bool variant_filename(const char *base, unsigned variant,
                             char *out, size_t size)
{
    if (variant == 0) return snprintf(out, size, "%s", base) < (int)size;
    const char *extension = strrchr(base, '.');
    if (!extension) return false;
    return snprintf(out, size, "%.*s_v%02u%s", (int)(extension - base), base,
                    variant + 1, extension) < (int)size;
}

bool sound_init(void)
{
    pcmmix_options options;
    char relative[96], error[256];

    (void)pcmmix_bank_init(&sound_bank, SFX_COUNT, 0x4b17c4e5u);
    if (G.headless) return false;
    for (int id = 0; id < SFX_COUNT; id++) {
        for (unsigned variant = 0; variant < SFX_VARIANTS[id]; variant++) {
            if (!variant_filename(SFX_FILES[id], variant, relative,
                                  sizeof relative))
                continue;
            const char *path = asset_path(relative);
            (void)pcmmix_bank_load_wav(&sound_bank, (uint32_t)id, variant,
                                       path, 1.0f, 1.0f,
                                       error, sizeof error);
        }
    }
    pcmmix_options_init(&options);
    if (!pcmmix_start(&mixer, &options)) {
        pcmmix_bank_clear(&sound_bank);
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
    (void)pcmmix_bank_play(&mixer, &sound_bank, (uint32_t)id,
                           1.0f, 1.0f);
}

void sound_shutdown(void)
{
    if (mixer_started) pcmmix_stop(&mixer);
    mixer_started = false;
    pcmmix_bank_clear(&sound_bank);
}
