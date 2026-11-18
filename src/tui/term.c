#include "tui.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

// Alternate screen, hidden cursor, no line wrap; and the way back
#define ENTER_SEQ "\033[?1049h\033[?25l\033[?7l\033[H"
#define LEAVE_SEQ "\033[0m\033[?7h\033[?25h\033[?1049l"

// How long a lone ESC waits for the rest of an escape sequence
#define ESC_WAIT_MS 25

static struct termios orig_termios;
static int active = 0;
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
    active = 1;
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
            if (inbuf[1] == '[') {
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
