#define _POSIX_C_SOURCE 200809L

#include "kilix.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef enum {
    PLAYER_NONE,
    PLAYER_PW_PLAY,
    PLAYER_PAPLAY,
    PLAYER_APLAY,
    PLAYER_SOX_PLAY
} PlayerKind;

typedef struct {
    const char *command;
    PlayerKind kind;
} PlayerCandidate;

static const PlayerCandidate PLAYER_CANDIDATES[] = {
    { "pw-play", PLAYER_PW_PLAY },
    { "paplay",  PLAYER_PAPLAY },
    { "aplay",   PLAYER_APLAY },
    { "play",    PLAYER_SOX_PLAY }
};

static const char *const SFX_FILES[SFX_COUNT] = {
    [SFX_MOVE] = "sfx/move.wav",
    [SFX_CONFIRM] = "sfx/confirm.wav",
    [SFX_TRAIN] = "sfx/train.wav",
    [SFX_HIT] = "sfx/hit.wav",
    [SFX_WIN] = "sfx/win.wav",
    [SFX_LOSE] = "sfx/lose.wav"
};

static PlayerKind player_kind;
static char player_path[PATH_MAX];

static bool executable_in_path(const char *name, char *result, size_t result_size)
{
    const char *path = getenv("PATH");
    const char *part;
    size_t name_len;

    if (!name || !*name || !result || result_size == 0) return false;
    if (!path || !*path) path = "/usr/local/bin:/usr/bin:/bin";

    name_len = strlen(name);
    part = path;
    for (;;) {
        const char *end = strchr(part, ':');
        size_t dir_len = end ? (size_t)(end - part) : strlen(part);
        size_t needed = (dir_len ? dir_len : 1) + 1 + name_len + 1;

        if (needed <= result_size) {
            size_t offset = 0;

            if (dir_len) {
                memcpy(result, part, dir_len);
                offset = dir_len;
            } else {
                result[offset++] = '.';
            }
            result[offset++] = '/';
            memcpy(result + offset, name, name_len + 1);

            if (access(result, X_OK) == 0) return true;
        }

        if (!end) break;
        part = end + 1;
    }

    result[0] = '\0';
    return false;
}

static void silence_child_stdio(void)
{
    int null_fd = open("/dev/null", O_RDWR);

    if (null_fd < 0) return;
    (void)dup2(null_fd, STDIN_FILENO);
    (void)dup2(null_fd, STDOUT_FILENO);
    (void)dup2(null_fd, STDERR_FILENO);
    if (null_fd > STDERR_FILENO) (void)close(null_fd);
}

static void exec_player(const char *wav_path)
{
    switch (player_kind) {
    case PLAYER_PW_PLAY: {
        char *const argv[] = { player_path, (char *)wav_path, NULL };
        execv(player_path, argv);
        break;
    }
    case PLAYER_PAPLAY: {
        char *const argv[] = { player_path, (char *)wav_path, NULL };
        execv(player_path, argv);
        break;
    }
    case PLAYER_APLAY: {
        char *const argv[] = { player_path, "-q", (char *)wav_path, NULL };
        execv(player_path, argv);
        break;
    }
    case PLAYER_SOX_PLAY: {
        char *const argv[] = { player_path, "-q", (char *)wav_path, NULL };
        execv(player_path, argv);
        break;
    }
    case PLAYER_NONE:
        break;
    }

    _exit(127);
}

/*
 * A short-lived intermediate child is reaped immediately.  The detached
 * grandchild owns playback, so effects overlap without leaving zombies in the
 * game process or making the render loop wait for an audio command to finish.
 */
static void spawn_player(const char *wav_path)
{
    pid_t child = fork();

    if (child < 0) return;
    if (child == 0) {
        pid_t player = fork();

        if (player < 0) _exit(127);
        if (player > 0) _exit(0);

        (void)setsid();
        silence_child_stdio();
        exec_player(wav_path);
    }

    while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {
        /* Retry until the intermediate child has definitely been reaped. */
    }
}

bool sound_init(void)
{
    size_t i;

    player_kind = PLAYER_NONE;
    player_path[0] = '\0';
    if (G.headless) return false;

    for (i = 0; i < sizeof(PLAYER_CANDIDATES) / sizeof(PLAYER_CANDIDATES[0]); ++i) {
        if (executable_in_path(PLAYER_CANDIDATES[i].command,
                               player_path, sizeof(player_path))) {
            player_kind = PLAYER_CANDIDATES[i].kind;
            return true;
        }
    }

    return false;
}

void sound_play(SoundId id)
{
    const char *wav_path;

    if (G.headless || !G.sound_on || player_kind == PLAYER_NONE) return;
    if ((int)id < 0 || id >= SFX_COUNT || !SFX_FILES[id]) return;

    wav_path = asset_path(SFX_FILES[id]);
    if (!wav_path || access(wav_path, R_OK) != 0) return;
    spawn_player(wav_path);
}

void sound_shutdown(void)
{
    player_kind = PLAYER_NONE;
    player_path[0] = '\0';
}
