/*
 * Kitty terminal platform layer.
 *
 * Derived from the async framebuffer presenter in Chess Bash (MIT, 2026),
 * then reduced and renamed for Kilix Rancher.  Frames are RGB-packed,
 * zlib-compressed and swapped under Kitty image ids inside synchronized
 * terminal updates.  Encoding runs on a worker thread so simulation never
 * waits on a slow terminal connection.
 */
#include "kilix.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#include <zlib.h>

static volatile sig_atomic_t winch_flag;
static struct termios original_termios;
static bool raw_active;
static volatile int shutdown_claimed;
/* Set from an async-signal-safe restore path to fence the presenter thread:
 * once set, the presenter emits no further bytes, so a signal-time restore
 * cannot interleave its output with a fresh half-written graphics packet. */
static volatile sig_atomic_t presenter_disabled;

static pthread_t presenter_thread;
static pthread_mutex_t frame_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t frame_condition = PTHREAD_COND_INITIALIZER;
static uint8_t *pending_buffer;
static uint8_t *encoding_buffer;
static size_t pending_capacity;
static size_t encoding_capacity;
static int pending_width;
static int pending_height;
static bool frame_pending;
static bool presenter_running;
static bool clear_pending;
static char origin_sequence[32] = "\x1b[H";

static const char BASE64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void handle_winch(int signal_number)
{
    (void)signal_number;
    winch_flag = 1;
}

static void write_all(const char *data, size_t length)
{
    while (length) {
        ssize_t count = write(STDOUT_FILENO, data, length);
        if (count <= 0) return;
        data += count;
        length -= (size_t)count;
    }
}

static void write_text(const char *text)
{
    write_all(text, strlen(text));
}

static int read_byte_timeout(unsigned char *byte, int timeout_ms)
{
    /* Retry across signal interruptions (SIGWINCH is delivered constantly
     * while a window is dragged); otherwise an EINTR is misread as a timeout
     * and truncates a multi-byte escape sequence mid-parse, turning an arrow
     * key into a stray KEY_ESC plus loose bytes. */
    for (;;) {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(STDIN_FILENO, &read_set);
        struct timeval timeout = {
            .tv_sec = timeout_ms / 1000,
            .tv_usec = (timeout_ms % 1000) * 1000
        };
        int ready = select(STDIN_FILENO + 1, &read_set, NULL, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            return 0;
        }
        if (ready == 0)
            return 0;
        return read(STDIN_FILENO, byte, 1) == 1;
    }
}

static size_t base64_encode(const uint8_t *input, size_t length, char *output)
{
    size_t in = 0;
    size_t out = 0;
    while (in + 2 < length) {
        uint32_t value = ((uint32_t)input[in] << 16) |
                         ((uint32_t)input[in + 1] << 8) |
                         (uint32_t)input[in + 2];
        output[out++] = BASE64[(value >> 18) & 63];
        output[out++] = BASE64[(value >> 12) & 63];
        output[out++] = BASE64[(value >> 6) & 63];
        output[out++] = BASE64[value & 63];
        in += 3;
    }
    if (in + 1 == length) {
        uint32_t value = (uint32_t)input[in] << 16;
        output[out++] = BASE64[(value >> 18) & 63];
        output[out++] = BASE64[(value >> 12) & 63];
        output[out++] = '=';
        output[out++] = '=';
    } else if (in + 2 == length) {
        uint32_t value = ((uint32_t)input[in] << 16) |
                         ((uint32_t)input[in + 1] << 8);
        output[out++] = BASE64[(value >> 18) & 63];
        output[out++] = BASE64[(value >> 12) & 63];
        output[out++] = BASE64[(value >> 6) & 63];
        output[out++] = '=';
    }
    return out;
}

static bool measure_geometry(int *out_width, int *out_height)
{
    struct winsize size;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0) return false;

    int columns = size.ws_col > 0 ? size.ws_col : 100;
    int rows = size.ws_row > 1 ? size.ws_row - 1 : 29;
    int cell_width = size.ws_xpixel > 0 ? size.ws_xpixel / columns : 9;
    int cell_height = size.ws_ypixel > 0 ? size.ws_ypixel / (rows + 1) : 18;
    if (cell_width <= 0) cell_width = 9;
    if (cell_height <= 0) cell_height = 18;

    int width = columns * cell_width;
    int height = rows * cell_height;
    if (width < 640) width = 640;
    if (height < 360) height = 360;
    if (width > 1440) width = 1440;
    if (height > 900) height = 900;
    width -= width % cell_width;
    height -= height % cell_height;
    width &= ~1;
    height &= ~1;

    int image_columns = (width + cell_width - 1) / cell_width;
    int image_rows = (height + cell_height - 1) / cell_height;
    int origin_column = 1 + (columns - image_columns) / 2;
    int origin_row = 1 + (rows - image_rows) / 2;
    if (origin_column < 1) origin_column = 1;
    if (origin_row < 1) origin_row = 1;
    snprintf(origin_sequence, sizeof origin_sequence, "\x1b[%d;%dH",
             origin_row, origin_column);

    *out_width = width;
    *out_height = height;
    return true;
}

static bool kitty_probe(void)
{
    write_text("\x1b_Gi=71,a=q,t=d,f=24,s=1,v=1;AAAA\x1b\\\x1b[c");
    char response[512] = {0};
    size_t length = 0;
    bool graphics = false;
    while (length + 1 < sizeof response) {
        unsigned char byte;
        /* Give the first byte a generous window so a high-latency SSH link
         * (RTT > 400 ms) to a fully capable terminal is not misjudged as
         * lacking graphics support; once bytes flow they arrive quickly. */
        int wait_ms = length == 0 ? 2000 : 400;
        if (!read_byte_timeout(&byte, wait_ms)) break;
        response[length++] = (char)byte;
        response[length] = '\0';
        if (strstr(response, "\x1b_Gi=71")) graphics = true;
        if (byte == 'c' && strstr(response, "\x1b[?")) break;
    }
    return graphics;
}

bool term_init(int *out_width, int *out_height)
{
    /* Every failure path prints its own reason: a silent non-zero exit (e.g.
     * when stdout is redirected so TIOCGWINSZ fails) leaves the user with no
     * clue why the game did not start. */
    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "kilix-rancher: interactive play needs a terminal on "
                        "stdin. Use --selftest or --render-test for headless "
                        "checks.\n");
        return false;
    }
    if (!isatty(STDOUT_FILENO)) {
        fprintf(stderr, "kilix-rancher: stdout is not a terminal (its output "
                        "is redirected). Run the game directly in a terminal, "
                        "without piping or redirecting stdout.\n");
        return false;
    }
    if (!measure_geometry(out_width, out_height)) {
        fprintf(stderr, "kilix-rancher: could not read the terminal size "
                        "(TIOCGWINSZ failed on stdout).\n");
        return false;
    }
    if (tcgetattr(STDIN_FILENO, &original_termios) != 0) {
        fprintf(stderr, "kilix-rancher: could not read terminal attributes.\n");
        return false;
    }

    struct termios raw = original_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~OPOST;
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        fprintf(stderr, "kilix-rancher: could not switch the terminal to raw "
                        "mode.\n");
        return false;
    }
    raw_active = true;

    if (!getenv("KILIX_RANCHER_SKIP_PROBE") && !kitty_probe()) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
        raw_active = false;
        fprintf(stderr,
                "kilix-rancher: this terminal did not answer the Kitty "
                "graphics query.\nTry Kilix, Kitty, WezTerm, Konsole, or "
                "Ghostty (KILIX_RANCHER_SKIP_PROBE=1 overrides).\n");
        return false;
    }

    signal(SIGWINCH, handle_winch);
    write_text("\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H");
    return true;
}

bool term_check_resize(int *out_width, int *out_height)
{
    if (!winch_flag) return false;
    winch_flag = 0;
    pthread_mutex_lock(&frame_lock);
    bool measured = measure_geometry(out_width, out_height);
    if (measured) clear_pending = true;
    pthread_mutex_unlock(&frame_lock);
    return measured;
}

static void encode_and_write(const uint8_t *rgba, int width, int height,
                             const char *origin, bool clear_first)
{
    static uint8_t *rgb;
    static uint8_t *compressed;
    static char *encoded;
    static char *packet;
    static size_t rgb_capacity;
    static size_t compressed_capacity;
    static size_t packet_capacity;

    /* A signal-time restore has fenced us: emit nothing further. */
    if (presenter_disabled) return;

    /* Every growth uses a temporary so a failed realloc keeps the old buffer
     * and the old capacity: on the previous "cap = n; p = realloc(p, cap)"
     * pattern a single OOM left p == NULL while cap claimed it was large
     * enough, so every later frame skipped the realloc and returned early —
     * the picture froze forever and the old block leaked. */
    size_t pixels = (size_t)width * height;
    size_t raw_length = pixels * 3;
    if (raw_length > rgb_capacity) {
        uint8_t *grown = realloc(rgb, raw_length);
        if (!grown) return;
        rgb = grown;
        rgb_capacity = raw_length;
    }
    for (size_t i = 0; i < pixels; i++) {
        rgb[i * 3] = rgba[i * 4];
        rgb[i * 3 + 1] = rgba[i * 4 + 1];
        rgb[i * 3 + 2] = rgba[i * 4 + 2];
    }

    size_t needed = compressBound(raw_length);
    if (needed > compressed_capacity) {
        uint8_t *grown = realloc(compressed, needed);
        if (!grown) return;
        compressed = grown;
        char *grown_encoded = realloc(encoded, ((needed + 2) / 3) * 4 + 8);
        if (!grown_encoded) return;   /* compressed_capacity left unchanged: retried next frame */
        encoded = grown_encoded;
        compressed_capacity = needed;
    }
    uLongf compressed_length = (uLongf)compressed_capacity;
    if (compress2(compressed, &compressed_length, rgb, raw_length, 1) != Z_OK)
        return;

    size_t encoded_length = base64_encode(compressed, compressed_length, encoded);
    size_t chunks = (encoded_length + 4095) / 4096;
    size_t packet_needed = encoded_length + chunks * 96 + 512;
    if (packet_needed > packet_capacity) {
        char *grown = realloc(packet, packet_needed);
        if (!grown) return;
        packet = grown;
        packet_capacity = packet_needed;
    }

    static int shown_id = 2;
    int new_id = shown_id == 1 ? 2 : 1;
    char *output = packet;
    output += sprintf(output, "\x1b[?2026h%s%s",
                      clear_first ? "\x1b[2J" : "", origin);
    size_t offset = 0;
    bool first = true;
    while (offset < encoded_length) {
        size_t count = encoded_length - offset;
        if (count > 4096) count = 4096;
        int more = offset + count < encoded_length ? 1 : 0;
        if (first) {
            output += sprintf(output,
                              "\x1b_Ga=T,f=24,i=%d,q=2,o=z,s=%d,v=%d,m=%d;",
                              new_id, width, height, more);
            first = false;
        } else {
            output += sprintf(output, "\x1b_Gm=%d;", more);
        }
        memcpy(output, encoded + offset, count);
        output += count;
        *output++ = '\x1b';
        *output++ = '\\';
        offset += count;
    }
    output += sprintf(output,
                      "\x1b_Ga=d,d=I,i=%d,q=2\x1b\\\x1b[?2026l", shown_id);
    shown_id = new_id;
    /* Re-check right before the write: if a restore fenced us after the top
     * check, skip emitting this frame's packet entirely. */
    if (presenter_disabled) return;
    write_all(packet, (size_t)(output - packet));
}

static void *presenter_main(void *unused)
{
    (void)unused;
    for (;;) {
        pthread_mutex_lock(&frame_lock);
        while (!frame_pending && presenter_running)
            pthread_cond_wait(&frame_condition, &frame_lock);
        if (!presenter_running) {
            pthread_mutex_unlock(&frame_lock);
            break;
        }

        uint8_t *buffer = pending_buffer;
        pending_buffer = encoding_buffer;
        encoding_buffer = buffer;
        size_t capacity = pending_capacity;
        pending_capacity = encoding_capacity;
        encoding_capacity = capacity;
        int width = pending_width;
        int height = pending_height;
        char origin[32];
        snprintf(origin, sizeof origin, "%s", origin_sequence);
        bool clear_first = clear_pending;
        clear_pending = false;
        frame_pending = false;
        pthread_mutex_unlock(&frame_lock);

        encode_and_write(encoding_buffer, width, height, origin, clear_first);
    }
    return NULL;
}

void term_present(const uint8_t *rgba, int width, int height)
{
    size_t bytes = (size_t)width * height * 4;
    pthread_mutex_lock(&frame_lock);
    if (!presenter_running) {
        presenter_running = true;
        if (pthread_create(&presenter_thread, NULL, presenter_main, NULL) != 0) {
            presenter_running = false;
            pthread_mutex_unlock(&frame_lock);
            encode_and_write(rgba, width, height, origin_sequence, false);
            return;
        }
    }
    if (bytes > pending_capacity) {
        /* Temporary-pointer growth: a failed realloc must not poison the
         * capacity bookkeeping (see encode_and_write). */
        uint8_t *grown = realloc(pending_buffer, bytes);
        if (grown) {
            pending_buffer = grown;
            pending_capacity = bytes;
        }
    }
    if (pending_buffer && pending_capacity >= bytes) {
        memcpy(pending_buffer, rgba, bytes);
        pending_width = width;
        pending_height = height;
        frame_pending = true;
        pthread_cond_signal(&frame_condition);
    }
    pthread_mutex_unlock(&frame_lock);
}

static void stop_presenter(void)
{
    pthread_mutex_lock(&frame_lock);
    if (!presenter_running) {
        pthread_mutex_unlock(&frame_lock);
        return;
    }
    presenter_running = false;
    frame_pending = false;
    pthread_cond_signal(&frame_condition);
    pthread_mutex_unlock(&frame_lock);
    pthread_join(presenter_thread, NULL);
}

static bool claim_shutdown(void)
{
    if (!raw_active) return false;
    return !__sync_lock_test_and_set(&shutdown_claimed, 1);
}

static void restore_terminal(void)
{
    /* End any synchronized update the presenter may have opened (its packet
     * begins with ?2026h): a signal that truncated that packet before its
     * closing ?2026l would otherwise leave the terminal frozen. Do this first
     * so the restore writes below are not themselves buffered by a frozen
     * update. Then terminate any partial APC and delete only the two image
     * ids this game ever places (1 and 2) — the old d=A deleted every kitty
     * image in the terminal, wiping images another program had shown. */
    write_text("\x1b[?2026l");
    write_text("\x1b\\"
               "\x1b_Ga=d,d=i,i=1,q=2\x1b\\"
               "\x1b_Ga=d,d=i,i=2,q=2\x1b\\");
    write_text("\x1b[?25h\x1b[?1049l");
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
    raw_active = false;
}

void term_shutdown(void)
{
    if (!claim_shutdown()) return;
    stop_presenter();
    restore_terminal();
}

void term_emergency_restore(void)
{
    /* Fence the presenter first (async-signal-safe flag write): the thread
     * cannot be joined from a signal handler, so this stops it emitting new
     * bytes that would otherwise interleave with the restore sequence below. */
    presenter_disabled = 1;
    if (!claim_shutdown()) return;
    restore_terminal();
}

int term_poll_key(void)
{
    unsigned char byte;
    if (read(STDIN_FILENO, &byte, 1) != 1) return -1;
    if (byte == '\r' || byte == '\n') return KEY_ENTER;
    if (byte == 127 || byte == 8) return KEY_BACKSPACE;
    if (byte == '\t') return KEY_TAB;
    if (byte == 3) {
        G.quit = true;
        return -1;
    }
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
               sequence[1] == ';') {
            if (!read_byte_timeout(&sequence[1], 25)) break;
        }
        return -1;
    }
}
