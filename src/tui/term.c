#include "tui.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

// Alternate screen, hidden cursor, no line wrap; and the way back
#define ENTER_SEQ "\033[?1049h\033[?25l\033[?7l\033[H"
#define LEAVE_SEQ "\033[0m\033[?1006l\033[?1002l\033[?1000l\033[?7h\033[?25h" \
                  "\033[?1049l"

// Mouse: clicks, drags with a button held, wheel; SGR encoding
#define MOUSE_ON  "\033[?1000h\033[?1002h\033[?1006h"
#define MOUSE_OFF "\033[?1006l\033[?1002l\033[?1000l"

// How long a lone ESC waits for the rest of an escape sequence
#define ESC_WAIT_MS 25

static struct termios orig_termios;
static int active = 0;
static int mouse_on = 0;
static MouseEvent last_mouse;
static int last_palette_index;
static uint32_t last_palette_rgb;
static int handlers_set = 0;
static int sig_pipe[2] = {-1, -1};

// Bytes read from stdin but not yet decoded into keys
static unsigned char inbuf[256];
static int in_len = 0;

// Prototype
static void enter_raw(void);
static void on_signal(int sig);
static void on_fatal(int sig);
static void set_handler(int sig, void (*fn)(int));
static void read_input(void);
static int input_ready(int timeout_ms);
static void consume(int n);
static int decode_csi(int *used);
static int decode_ss3(int *used);
static int decode_osc(int *used);
static int hex_channel(const char *s, int n);

// Raw input, no echo, no signals from Ctrl+C / Ctrl+Z (they arrive as keys)
static void enter_raw(void) {
    struct termios raw = orig_termios;
    raw.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(tcflag_t)OPOST;
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    if (write(STDOUT_FILENO, ENTER_SEQ, sizeof(ENTER_SEQ) - 1) < 0) {
        // Nothing useful to do if the terminal is gone
    }
    if (mouse_on && write(STDOUT_FILENO, MOUSE_ON, sizeof(MOUSE_ON) - 1) < 0) {
        // Same
    }
    active = 1;
}

void term_set_mouse(int on) {
    mouse_on = on;
    if (!active) {
        return;
    }
    const char *seq = on ? MOUSE_ON : MOUSE_OFF;
    if (write(STDOUT_FILENO, seq, strlen(seq)) < 0) {
        // Terminal gone
    }
}

void term_mouse(MouseEvent *ev) {
    *ev = last_mouse;
}

void term_query_palette(void) {
    char q[512];
    int n = 0;
    for (int i = 0; i < 16; i++) {
        n += snprintf(q + n, sizeof(q) - (size_t)n, "\033]4;%d;?\007", i);
    }
    // The default text and background colors too (OSC 10 and 11)
    n += snprintf(q + n, sizeof(q) - (size_t)n, "\033]10;?\007\033]11;?\007");
    if (active && write(STDOUT_FILENO, q, (size_t)n) < 0) {
        // Terminal gone
    }
}

void term_palette(int *index, uint32_t *rgb) {
    *index = last_palette_index;
    *rgb = last_palette_rgb;
}

// One channel of "rgb:RRRR/GGGG/BBBB": 1 to 4 hex digits, scaled to 0-255
static int hex_channel(const char *s, int n) {
    unsigned v = 0;
    for (int i = 0; i < n; i++) {
        char c = s[i];
        v = v * 16 + (unsigned)(c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
    }
    unsigned max = (1u << (4 * n)) - 1;
    return (int)((v * 255 + max / 2) / max);
}

// ESC ] ... BEL or ESC \: replies to OSC queries, such as
// "4;1;rgb:cccc/2424/1d1d" for palette color 1
static int decode_osc(int *used) {
    int end = -1, term_len = 0;
    for (int i = 2; i < in_len; i++) {
        if (inbuf[i] == 0x07) {
            end = i;
            term_len = 1;
            break;
        }
        if (inbuf[i] == 0x1B && i + 1 < in_len && inbuf[i + 1] == '\\') {
            end = i;
            term_len = 2;
            break;
        }
    }
    if (end < 0) {
        *used = in_len >= (int)sizeof(inbuf) ? in_len : 0;   // wait for more
        return KEY_NONE;
    }
    *used = end + term_len;

    char body[128];
    int len = end - 2 < (int)sizeof(body) - 1 ? end - 2 : (int)sizeof(body) - 1;
    memcpy(body, inbuf + 2, (size_t)len);
    body[len] = '\0';

    int index, which;
    char r[5], g[5], b[5];
    if (sscanf(body, "4;%d;rgb:%4[0-9a-fA-F]/%4[0-9a-fA-F]/%4[0-9a-fA-F]",
               &index, r, g, b) != 4) {
        if (sscanf(body, "%d;rgb:%4[0-9a-fA-F]/%4[0-9a-fA-F]/%4[0-9a-fA-F]",
                   &which, r, g, b) != 4 || (which != 10 && which != 11)) {
            return KEY_NONE;
        }
        index = which == 10 ? TERM_COLOR_FG : TERM_COLOR_BG;
    }
    last_palette_index = index;
    last_palette_rgb = (uint32_t)(hex_channel(r, (int)strlen(r)) << 16 |
                                  hex_channel(g, (int)strlen(g)) << 8 |
                                  hex_channel(b, (int)strlen(b)));
    return KEY_PALETTE;
}

// Signals only leave a byte in the pipe; the main loop does the work
static void on_signal(int sig) {
    int saved = errno;
    char c = sig == SIGWINCH ? 'w' : sig == SIGCONT ? 'c' : 'q';
    if (write(sig_pipe[1], &c, 1) < 0) {
        // Pipe full: a wake-up is already pending
    }
    errno = saved;
}

// Crashes still give the terminal back before the process dies
static void on_fatal(int sig) {
    if (active) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        if (write(STDOUT_FILENO, LEAVE_SEQ, sizeof(LEAVE_SEQ) - 1) < 0) {
            // Dying anyway
        }
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

static void set_handler(int sig, void (*fn)(int)) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = fn;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(sig, &sa, NULL);
}

int term_init(void) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        return -1;
    }
    if (tcgetattr(STDIN_FILENO, &orig_termios) != 0) {
        return -1;
    }

    if (!handlers_set) {
        if (pipe(sig_pipe) != 0) {
            return -1;
        }
        for (int i = 0; i < 2; i++) {
            fcntl(sig_pipe[i], F_SETFL, fcntl(sig_pipe[i], F_GETFL) | O_NONBLOCK);
            fcntl(sig_pipe[i], F_SETFD, FD_CLOEXEC);
        }
        set_handler(SIGWINCH, on_signal);
        set_handler(SIGCONT, on_signal);
        set_handler(SIGINT, on_signal);
        set_handler(SIGTERM, on_signal);
        set_handler(SIGHUP, on_signal);
        set_handler(SIGSEGV, on_fatal);
        set_handler(SIGBUS, on_fatal);
        set_handler(SIGFPE, on_fatal);
        set_handler(SIGABRT, on_fatal);
        atexit(term_restore);
        handlers_set = 1;
    }
    enter_raw();
    return 0;
}

void term_restore(void) {
    if (!active) {
        return;
    }
    if (write(STDOUT_FILENO, LEAVE_SEQ, sizeof(LEAVE_SEQ) - 1) < 0) {
        // Terminal gone, still reset the tty settings below
    }
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    active = 0;
}

void term_suspend(void) {
    term_restore();
    // Stops here until fg; SIGCONT then also wakes the loop to redraw
    raise(SIGSTOP);
    tcgetattr(STDIN_FILENO, &orig_termios);
    enter_raw();
}

void term_size(int *cols, int *rows, double *cell_aspect) {
    struct winsize ws;
    *cols = 80;
    *rows = 24;
    *cell_aspect = 2.0;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0) {
        return;
    }
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    if (ws.ws_xpixel > 0 && ws.ws_ypixel > 0) {
        double a = ((double)ws.ws_ypixel / ws.ws_row) /
                   ((double)ws.ws_xpixel / ws.ws_col);
        if (a > 1.0 && a < 4.0) {
            *cell_aspect = a;
        }
    }
}

// Move whatever stdin has into inbuf
static void read_input(void) {
    if (in_len >= (int)sizeof(inbuf)) {
        return;
    }
    ssize_t n = read(STDIN_FILENO, inbuf + in_len, sizeof(inbuf) - (size_t)in_len);
    if (n > 0) {
        in_len += (int)n;
    }
}

static int input_ready(int timeout_ms) {
    struct pollfd p = {STDIN_FILENO, POLLIN, 0};
    return poll(&p, 1, timeout_ms) > 0;
}

int term_wait(int timeout_ms, int extra_fd) {
    if (in_len > 0) {
        return TERM_EV_INPUT;
    }
    struct pollfd fds[3] = {
        {STDIN_FILENO, POLLIN, 0},
        {sig_pipe[0], POLLIN, 0},
        {extra_fd, POLLIN, 0},
    };
    if (poll(fds, extra_fd >= 0 ? 3 : 2, timeout_ms) <= 0) {
        return 0;
    }

    int ev = 0;
    if (extra_fd >= 0 && (fds[2].revents & POLLIN)) {
        ev |= TERM_EV_EXTRA;
    }
    if (fds[1].revents & POLLIN) {
        char c[32];
        ssize_t n;
        while ((n = read(sig_pipe[0], c, sizeof(c))) > 0) {
            for (ssize_t i = 0; i < n; i++) {
                ev |= c[i] == 'w' ? TERM_EV_RESIZE
                    : c[i] == 'c' ? TERM_EV_RESUME | TERM_EV_RESIZE
                                  : TERM_EV_QUIT;
            }
        }
    }
    if (fds[0].revents & (POLLIN | POLLHUP)) {
        read_input();
        ev |= TERM_EV_INPUT;
        if (fds[0].revents & POLLHUP && in_len == 0) {
            ev |= TERM_EV_QUIT;
        }
    }
    return ev;
}

static void consume(int n) {
    memmove(inbuf, inbuf + n, (size_t)(in_len - n));
    in_len -= n;
}

// ESC [ ... final byte: arrows, F keys, Home/End, PgUp/PgDn
static int decode_csi(int *used) {
    int i = 2;
    int params[4] = {0};
    int np = 0;
    int sgr_mouse = in_len > 2 && inbuf[2] == '<';
    while (i < in_len && inbuf[i] >= 0x30 && inbuf[i] <= 0x3F) {
        if (inbuf[i] == ';') {
            np = np < 3 ? np + 1 : np;
        } else if (inbuf[i] >= '0' && inbuf[i] <= '9') {
            params[np] = params[np] * 10 + (inbuf[i] - '0');
        }
        i++;
    }
    if (i >= in_len) {
        *used = 0;      // incomplete, wait for more
        return KEY_NONE;
    }
    unsigned char final = inbuf[i];
    *used = i + 1;

    // ESC [ < button ; x ; y M (press, motion) or m (release)
    if (sgr_mouse && (final == 'M' || final == 'm')) {
        int b = params[0];
        last_mouse.x = params[1] - 1;
        last_mouse.y = params[2] - 1;
        last_mouse.motion = (b & 32) != 0;
        last_mouse.button = b & ~(32 | 4 | 8 | 16);  // without motion/mods
        last_mouse.press = final == 'M';
        return KEY_MOUSE;
    }

    int shift = np >= 1 && params[1] == 2;
    switch (final) {
    case 'A':
        return shift ? KEY_SHIFT_UP : KEY_UP;
    case 'B':
        return shift ? KEY_SHIFT_DOWN : KEY_DOWN;
    case 'C':
        return shift ? KEY_SHIFT_RIGHT : KEY_RIGHT;
    case 'D':
        return shift ? KEY_SHIFT_LEFT : KEY_LEFT;
    case 'H':
        return KEY_HOME;
    case 'F':
        return KEY_END;
    case 'Z':
        return KEY_BACKTAB;
    case 'n':
        // Reply to ESC [5n: everything sent before it has been processed
        return params[0] == 0 ? KEY_TERM_OK : KEY_NONE;
    case '~':
        switch (params[0]) {
        case 1:
        case 7:
            return KEY_HOME;
        case 4:
        case 8:
            return KEY_END;
        case 3:
            return KEY_DELETE;
        case 5:
            return KEY_PAGE_UP;
        case 6:
            return KEY_PAGE_DOWN;
        case 11:
            return KEY_F1;
        case 12:
            return KEY_F2;
        }
        return KEY_NONE;
    }
    return KEY_NONE;
}

// ESC O x: F1/F2 and arrows in application cursor mode
static int decode_ss3(int *used) {
    if (in_len < 3) {
        *used = 0;
        return KEY_NONE;
    }
    *used = 3;
    switch (inbuf[2]) {
    case 'P':
        return KEY_F1;
    case 'Q':
        return KEY_F2;
    case 'A':
        return KEY_UP;
    case 'B':
        return KEY_DOWN;
    case 'C':
        return KEY_RIGHT;
    case 'D':
        return KEY_LEFT;
    case 'H':
        return KEY_HOME;
    case 'F':
        return KEY_END;
    }
    return KEY_NONE;
}

int term_read_key(void) {
    for (;;) {
        if (in_len == 0) {
            return KEY_NONE;
        }

        if (inbuf[0] == 0x1B) {
            // A lone ESC is the Esc key unless more bytes follow quickly
            if (in_len == 1) {
                if (!input_ready(ESC_WAIT_MS)) {
                    consume(1);
                    return KEY_ESC;
                }
                read_input();
                continue;
            }
            int used = 0, key;
            if (inbuf[1] == ']') {
                key = decode_osc(&used);
            } else if (inbuf[1] == '[') {
                key = decode_csi(&used);
            } else if (inbuf[1] == 'O') {
                key = decode_ss3(&used);
            } else {
                // Alt+key: report the key alone
                consume(1);
                continue;
            }
            if (used == 0) {
                if (!input_ready(ESC_WAIT_MS)) {
                    // Broken sequence: drop it
                    in_len = 0;
                    return KEY_ESC;
                }
                read_input();
                continue;
            }
            consume(used);
            if (key != KEY_NONE) {
                return key;
            }
            continue;
        }

        // UTF-8 text; wait for the rest of a split character
        uint32_t cp;
        int need = inbuf[0] >= 0xF0 ? 4 : inbuf[0] >= 0xE0 ? 3
                 : inbuf[0] >= 0xC0 ? 2 : 1;
        if (in_len < need) {
            if (!input_ready(ESC_WAIT_MS)) {
                in_len = 0;
                return KEY_NONE;
            }
            read_input();
            continue;
        }
        char tmp[5] = {0};
        memcpy(tmp, inbuf, (size_t)need);
        int used = utf8_decode(tmp, &cp);
        consume(used > 0 ? used : 1);
        if (cp == '\n') {
            return KEY_ENTER;
        }
        if (cp == 8) {
            return KEY_BACKSPACE;
        }
        return (int)cp;
    }
}

void term_write(const Buf *b) {
    size_t off = 0;
    while (off < b->len) {
        ssize_t n = write(STDOUT_FILENO, b->data + off, b->len - off);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            return;
        }
        off += (size_t)n;
    }
}
