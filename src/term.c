/* Game-facing terminal API over the shared Kitty framebuffer presenter. */
#include "kilix.h"
#include "kitty_framebuffer.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

static kittyfb_session framebuffer;
static bool framebuffer_active;
static volatile int shutdown_claimed;

static int read_byte_timeout(unsigned char *byte, int timeout_ms)
{
    for (;;) {
        fd_set read_set;
        struct timeval timeout = {timeout_ms / 1000,
                                  (timeout_ms % 1000) * 1000};
        FD_ZERO(&read_set);
        FD_SET(STDIN_FILENO, &read_set);
        int ready = select(STDIN_FILENO + 1, &read_set, NULL, NULL, &timeout);
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) return 0;
        return read(STDIN_FILENO, byte, 1) == 1;
    }
}

bool term_init(int *out_width, int *out_height)
{
    kittyfb_options options;

    kittyfb_session_init(&framebuffer);
    kittyfb_options_init(&options);
    options.min_width = 640;
    options.min_height = 360;
    options.max_width = 1440;
    options.max_height = 900;
    options.probe_timeout_ms = 2000;
    if (getenv("KILIX_RANCHER_SKIP_PROBE")) options.probe_graphics = false;
    if (kittyfb_start(&framebuffer, STDIN_FILENO, STDOUT_FILENO,
                      &options) != 0) {
        fprintf(stderr, "kilix-rancher: terminal setup failed: %s\n",
                strerror(errno));
        return false;
    }
    framebuffer_active = true;
    shutdown_claimed = 0;
    *out_width = kittyfb_width(&framebuffer);
    *out_height = kittyfb_height(&framebuffer);
    return true;
}

bool term_check_resize(int *out_width, int *out_height)
{
    return framebuffer_active &&
           kittyfb_check_resize(&framebuffer, out_width, out_height);
}

void term_present(const uint8_t *rgba, int width, int height)
{
    if (framebuffer_active)
        (void)kittyfb_present(&framebuffer, rgba, width, height);
}

static bool claim_shutdown(void)
{
    if (!framebuffer_active) return false;
    return !__sync_lock_test_and_set(&shutdown_claimed, 1);
}

void term_shutdown(void)
{
    if (!claim_shutdown()) return;
    kittyfb_stop(&framebuffer);
    framebuffer_active = false;
}

void term_emergency_restore(void)
{
    if (!claim_shutdown()) return;
    kittyfb_emergency_restore(&framebuffer);
}

int term_poll_key(void)
{
    unsigned char byte;
    if (read(STDIN_FILENO, &byte, 1) != 1) return -1;
    if (byte == '\r' || byte == '\n') return KEY_ENTER;
    if (byte == 127 || byte == 8) return KEY_BACKSPACE;
    if (byte == '\t') return KEY_TAB;
    if (byte == 3) { G.quit = true; return -1; }
    if (byte != 0x1b) return byte;

    unsigned char sequence[2];
    if (!read_byte_timeout(&sequence[0], 25)) return KEY_ESC;
    if (sequence[0] != '[' && sequence[0] != 'O') return KEY_ESC;
    if (!read_byte_timeout(&sequence[1], 25)) return KEY_ESC;
    switch (sequence[1]) {
    case 'A': return KEY_UP;
    case 'B': return KEY_DOWN;
    case 'C': return KEY_RIGHT;
    case 'D': return KEY_LEFT;
    default:
        while ((sequence[1] >= '0' && sequence[1] <= '9') ||
               sequence[1] == ';')
            if (!read_byte_timeout(&sequence[1], 25)) break;
        return -1;
    }
}
