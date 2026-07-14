#include "kilix.h"

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static char asset_root[768] = "assets";

float clampf(float value, float low, float high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

int clampi(int value, int low, int high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

unsigned rng_next(void)
{
    unsigned value = G.rng;
    if (!value) value = 0xA341316Cu;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    G.rng = value;
    return value;
}

int rng_range(int low, int high)
{
    if (high <= low) return low;
    return low + (int)(rng_next() % (unsigned)(high - low + 1));
}

void asset_paths_init(void)
{
    const char *override = getenv("KILIX_RANCHER_ASSETS");
    if (override && *override) {
        snprintf(asset_root, sizeof asset_root, "%s", override);
        return;
    }

    char executable[640];
    ssize_t length = readlink("/proc/self/exe", executable,
                              sizeof executable - 1);
    if (length <= 0) return;
    executable[length] = '\0';
    char *slash = strrchr(executable, '/');
    if (!slash) return;
    *slash = '\0';

    char candidate[768];
    snprintf(candidate, sizeof candidate, "%s/assets", executable);
    if (access(candidate, F_OK) == 0) {
        snprintf(asset_root, sizeof asset_root, "%s", candidate);
        return;
    }
    snprintf(candidate, sizeof candidate,
             "%s/../share/kilix-rancher/assets", executable);
    if (access(candidate, F_OK) == 0)
        snprintf(asset_root, sizeof asset_root, "%s", candidate);
}

const char *asset_path(const char *relative)
{
    static char buffers[6][1024];
    static int index;
    index = (index + 1) % 6;
    snprintf(buffers[index], sizeof buffers[index], "%s/%s",
             asset_root, relative);
    return buffers[index];
}

static double monotonic_seconds(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (double)time.tv_sec + time.tv_nsec / 1000000000.0;
}

static void sleep_seconds(double seconds)
{
    if (seconds <= 0.0) return;
    struct timespec delay;
    delay.tv_sec = (time_t)seconds;
    delay.tv_nsec = (long)((seconds - delay.tv_sec) * 1000000000.0);
    nanosleep(&delay, NULL);
}

static void handle_signal(int signal_number)
{
    (void)signal_number;
    term_emergency_restore();
    _exit(1);
}

/* Crash-class signals: restore the terminal, then re-raise with the default
 * disposition so the process still dies with the right signal (and can dump
 * core). Without this a segfault/abort in game logic leaves the tty in raw
 * mode + alternate screen, making the user's shell unusable until `reset`.
 * term_emergency_restore uses only async-signal-safe calls. */
static void handle_fatal_signal(int signal_number)
{
    term_emergency_restore();
    signal(signal_number, SIG_DFL);
    raise(signal_number);
}

static void install_signal_handlers(void)
{
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGHUP, handle_signal);
    signal(SIGSEGV, handle_fatal_signal);
    signal(SIGABRT, handle_fatal_signal);
    signal(SIGBUS, handle_fatal_signal);
    signal(SIGFPE, handle_fatal_signal);
    signal(SIGILL, handle_fatal_signal);
}

static bool ensure_directory(const char *path)
{
    if (mkdir(path, 0777) == 0) return true;
    if (errno != EEXIST) return false;
    struct stat status;
    return stat(path, &status) == 0 && S_ISDIR(status.st_mode);
}

static bool ensure_path(const char *path)
{
    char copy[768];
    if (strlen(path) >= sizeof copy) return false;
    snprintf(copy, sizeof copy, "%s", path);
    size_t length = strlen(copy);
    while (length > 1 && copy[length - 1] == '/') copy[--length] = '\0';
    for (char *cursor = copy + 1; *cursor; cursor++) {
        if (*cursor != '/') continue;
        *cursor = '\0';
        if (!ensure_directory(copy)) return false;
        *cursor = '/';
    }
    return ensure_directory(copy);
}

static bool dump_named(const char *directory, const char *name)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", directory, name);
    render_frame();
    if (!render_dump_ppm(path)) {
        fprintf(stderr, "render-test: could not write %s\n", path);
        return false;
    }
    return true;
}

static int render_test(const char *directory, unsigned seed)
{
    if (!ensure_path(directory)) {
        fprintf(stderr, "render-test: could not create %s: %s\n",
                directory, strerror(errno));
        return 1;
    }
    game_init(seed, true);
    G.headless = true;
    char temporary_save[768];
    snprintf(temporary_save, sizeof temporary_save,
             "%s/.render-test-save.dat", directory);
    if (strlen(temporary_save) >= sizeof G.save_path) {
        fprintf(stderr, "render-test: output path is too long\n");
        return 1;
    }
    memcpy(G.save_path, temporary_save, strlen(temporary_save) + 1);

    char error[256];
    if (!render_init(960, 540, error, sizeof error)) {
        fprintf(stderr, "render-test: %s\n", error);
        return 1;
    }
    G.screen_time = 1.0f;
    int failures = 0;
    failures += !dump_named(directory, "render_01_title.ppm");
    G.screen = SCREEN_NAMING;
    G.screen_time = 1.0f;
    snprintf(G.name_input, sizeof G.name_input, "EMBER");
    G.name_len = 5;
    failures += !dump_named(directory, "render_02_naming.ppm");

    game_new_ranch("Ember");
    G.screen_time = 1.0f;
    failures += !dump_named(directory, "render_03_ranch.ppm");
    G.toast[0] = '\0';
    G.toast_timer = 0.0f;
    G.screen = SCREEN_TRAINING;
    G.drill_cursor = 2;
    G.screen_time = 1.0f;
    failures += !dump_named(directory, "render_04_training.ppm");
    G.screen = SCREEN_CARE;
    G.care_cursor = 1;
    G.screen_time = 1.0f;
    failures += !dump_named(directory, "render_05_care.ppm");
    G.screen = SCREEN_ARENA;
    G.arena_cursor = G.kilix.rank;
    G.screen_time = 1.0f;
    failures += !dump_named(directory, "render_06_arena.ppm");

    game_start_battle(G.kilix.rank);
    G.screen_time = 1.0f;
    failures += !dump_named(directory, "render_07_battle_ready.ppm");
    G.battle.phase = BATTLE_ACTIVE;
    G.battle.player_guts = 82;
    G.battle.enemy_guts = 64;
    G.battle.distance = 0.38f;
    failures += !dump_named(directory, "render_08_battle_active.ppm");
    G.battle.phase = BATTLE_PLAYER_ATTACK;
    G.battle.phase_timer = 0.23f;
    G.battle.hit = true;
    G.battle.last_damage = 37;
    snprintf(G.battle.callout, sizeof G.battle.callout,
             "EMBER POUNCE!  37 HEART");
    failures += !dump_named(directory, "render_09_battle_impact.ppm");

    G.screen = SCREEN_EVENT;
    G.screen_time = 1.0f;
    G.event.kind = EVENT_TRAIN;
    G.event.index = 2;
    G.event.timer = 1.0f;
    G.event.duration = 1.5f;
    G.event.success = true;
    G.event.great = true;
    G.event.primary = DRILLS[2].primary;
    G.event.secondary = DRILLS[2].secondary;
    G.event.gain_primary = 9;
    G.event.gain_secondary = 4;
    snprintf(G.event.title, sizeof G.event.title, "BLAZING SUCCESS!");
    snprintf(G.event.detail, sizeof G.event.detail,
             "%s CHASED EVERY FIREFLY AND CAME BACK GLOWING.", G.kilix.name);
    failures += !dump_named(directory, "render_10_training_result.ppm");

    G.screen = SCREEN_JOURNAL;
    G.journal_page = 1;
    G.screen_time = 1.0f;
    failures += !dump_named(directory, "render_11_journal.ppm");
    G.screen = SCREEN_CHAMPION;
    G.screen_time = 1.0f;
    G.kilix.rank = RANK_COUNT - 1;
    G.kilix.total_wins = 12;
    failures += !dump_named(directory, "render_12_champion.ppm");

    /* One snapshot of each drill mini-game, mid-play. */
    G.screen = SCREEN_DRILL;
    G.screen_time = 1.0f;
    /* `drill` is the DRILLS[] index whose primary stat yields this minigame, so
     * each snapshot's signboard title matches the game shown. */
    struct { MinigameType type; int rounds; int drill; const char *file; } drills[] = {
        {MG_TIMING,   3, 1, "render_13_drill_timing.ppm"},
        {MG_REACTION, 3, 4, "render_14_drill_reaction.ppm"},
        {MG_MEMORY,   3, 5, "render_15_drill_memory.ppm"},
        {MG_MASH,     1, 2, "render_16_drill_mash.ppm"},
        {MG_HOLD,     1, 3, "render_17_drill_hold.ppm"},
        {MG_RHYTHM,   RHY_BEATS, 0, "render_18_drill_rhythm.ppm"},
    };
    for (size_t i = 0; i < sizeof drills / sizeof drills[0]; i++) {
        memset(&G.minigame, 0, sizeof G.minigame);
        G.minigame.phase = 1;
        G.minigame.type = drills[i].type;
        G.minigame.drill = drills[i].drill;
        G.minigame.rounds = drills[i].rounds;
        G.minigame.round = 1;
        G.minigame.clock = 0.3f;              /* within a memory reveal's lit window */
        G.minigame.marker = 0.56f;
        G.minigame.target = 0.5f;
        G.minigame.half = 0.14f;
        G.minigame.cue_live = true;
        G.minigame.showing = drills[i].type == MG_MEMORY;
        G.minigame.seq_len = 4;
        G.minigame.seq[0] = 0; G.minigame.seq[1] = 2;
        G.minigame.seq[2] = 1; G.minigame.seq[3] = 3;
        G.minigame.seq_pos = 2;
        G.minigame.taps = 13;
        G.minigame.taps_target = 26;
        G.minigame.feedback = 0.4f;
        snprintf(G.minigame.banner, sizeof G.minigame.banner, "GOOD");
        failures += !dump_named(directory, drills[i].file);
    }

    /* One snapshot of each drill's get-ready / instruction screen so the
     * signboard text-fit stays covered by the visual regression set. */
    struct { MinigameType type; int drill; const char *file; } info[] = {
        {MG_TIMING,   1, "render_19_info_timing.ppm"},
        {MG_REACTION, 4, "render_20_info_reaction.ppm"},
        {MG_MEMORY,   5, "render_21_info_memory.ppm"},
        {MG_MASH,     2, "render_22_info_mash.ppm"},
        {MG_HOLD,     3, "render_23_info_hold.ppm"},
        {MG_RHYTHM,   0, "render_24_info_rhythm.ppm"},
    };
    for (size_t i = 0; i < sizeof info / sizeof info[0]; i++) {
        memset(&G.minigame, 0, sizeof G.minigame);
        G.minigame.phase = 0;                 /* get-ready / instructions */
        G.minigame.type = info[i].type;
        G.minigame.drill = info[i].drill;
        G.minigame.clock = 0.3f;              /* countdown shows "3" */
        failures += !dump_named(directory, info[i].file);
    }

    render_shutdown();
    unlink(temporary_save);
    if (failures) {
        fprintf(stderr, "render-test: %d snapshot(s) failed\n", failures);
        return 1;
    }
    printf("PASS: wrote 24 visual snapshots to %s\n", directory);
    return 0;
}

static void print_help(void)
{
    printf("kilix-rancher - original graphical fire-kitten raising game\n\n"
           "Usage:\n"
           "  ./kilix-rancher                    Play in a Kitty-graphics terminal\n"
           "  ./kilix-rancher --selftest [seed] [weeks]\n"
           "  ./kilix-rancher --render-test [directory] [seed]\n"
           "  ./kilix-rancher --validate-assets\n"
           "  ./kilix-rancher --help\n\n"
           "Controls:\n"
           "  Arrows / W,S     Move the cursor; up/down pick a battle move\n"
           "  Left / Right     Change range during a live battle\n"
           "  Enter / Space    Confirm; call selected move\n"
           "  1-4              Call a battle move directly\n"
           "  Esc              Back; cancels the arena Ready prompt with no\n"
           "                   penalty, forfeits a match already underway\n"
           "  J                Journal (ranch/champion screen)\n"
           "  M                Toggle sound\n"
           "  Q                Quit from title/ranch/champion\n\n"
           "Environment:\n"
           "  KILIX_RANCHER_ASSETS=/path   Override asset directory\n"
           "  KILIX_RANCHER_SAVE=/path     Override save file\n"
           "  KILIX_RANCHER_SKIP_PROBE=1   Skip Kitty protocol probe\n");
}

int main(int argc, char **argv)
{
    asset_paths_init();
    unsigned seed = (unsigned)time(NULL);

    if (argc > 1 && !strcmp(argv[1], "--help")) {
        print_help();
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--validate-assets")) {
        char error[256];
        if (!render_validate_assets(error, sizeof error)) {
            fprintf(stderr, "FAIL: %s\n", error);
            return 1;
        }
        printf("PASS: all runtime image assets are present and valid\n");
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--selftest")) {
        if (argc > 2) seed = (unsigned)strtoul(argv[2], NULL, 10);
        int weeks = argc > 3 ? atoi(argv[3]) : 240;
        return game_selftest(seed, weeks);
    }
    if (argc > 1 && !strcmp(argv[1], "--render-test")) {
        const char *directory = argc > 2 ? argv[2] : ".render-test";
        if (argc > 3) seed = (unsigned)strtoul(argv[3], NULL, 10);
        return render_test(directory, seed);
    }
    if (argc > 1) {
        fprintf(stderr, "kilix-rancher: unknown option %s\n", argv[1]);
        print_help();
        return 2;
    }

    /* Install before term_init: term_init enters raw mode and can then block
     * in the graphics probe for up to ~2 s, and a signal (SSH drop, SIGTERM,
     * a crash) during that window must still restore the terminal.
     * term_emergency_restore no-ops safely until raw mode is actually on. */
    install_signal_handlers();
    int width, height;
    if (!term_init(&width, &height))
        return 1;   /* term_init prints a specific reason for every failure */

    game_init(seed, false);
    char error[256];
    if (!render_init(width, height, error, sizeof error)) {
        term_shutdown();
        fprintf(stderr, "kilix-rancher: %s\n", error);
        return 1;
    }
    sound_init();

    double previous = monotonic_seconds();
    double accumulator = 0.0;
    double next_frame = previous;
    while (!G.quit) {
        int key;
        while ((key = term_poll_key()) >= 0)
            game_handle_key(key);

        int resized_width, resized_height;
        if (term_check_resize(&resized_width, &resized_height))
            render_resize(resized_width, resized_height);

        double now = monotonic_seconds();
        double elapsed = now - previous;
        previous = now;
        if (elapsed > 0.15) elapsed = 0.15;
        accumulator += elapsed;
        int steps = 0;
        while (accumulator >= TICK_DT && steps++ < 8) {
            game_tick(TICK_DT);
            accumulator -= TICK_DT;
        }
        /* Drop any simulation debt the 8-step cap could not drain instead of
         * banking it: otherwise sustained slow frames accumulate time that
         * later replays as a fast-forward burst (a 60 s battle timer draining
         * in an instant, enemy attacks warping) once the load subsides. */
        if (accumulator > TICK_DT)
            accumulator = TICK_DT;
        if (now >= next_frame) {
            render_frame();
            term_present(render_fb(), G.W, G.H);
            next_frame = now + 1.0 / 30.0;
        }
        sleep_seconds(0.0015);
    }

    sound_shutdown();
    render_shutdown();
    term_shutdown();
    return 0;
}
