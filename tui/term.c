/* term.c - terminal platform layer for the zero-dependency TUI.
 * POSIX: termios raw mode, ioctl(TIOCGWINSZ), select() on stdin, SIGWINCH.
 * Windows: Win32 console (VT output, ReadConsoleInputW, UTF-8 code page).
 * Shared: cell back/front buffers with diffed output, UTF-8, escape/mouse parser. */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "term.h"

#ifdef _WIN32
#include <windows.h>
#ifndef ENABLE_PROCESSED_INPUT
#define ENABLE_PROCESSED_INPUT 0x0001
#endif
#ifndef ENABLE_LINE_INPUT
#define ENABLE_LINE_INPUT 0x0002
#endif
#ifndef ENABLE_ECHO_INPUT
#define ENABLE_ECHO_INPUT 0x0004
#endif
#ifndef ENABLE_WINDOW_INPUT
#define ENABLE_WINDOW_INPUT 0x0008
#endif
#ifndef ENABLE_MOUSE_INPUT
#define ENABLE_MOUSE_INPUT 0x0010
#endif
#ifndef ENABLE_QUICK_EDIT_MODE
#define ENABLE_QUICK_EDIT_MODE 0x0040
#endif
#ifndef ENABLE_EXTENDED_FLAGS
#define ENABLE_EXTENDED_FLAGS 0x0080
#endif
#ifndef ENABLE_PROCESSED_OUTPUT
#define ENABLE_PROCESSED_OUTPUT 0x0001
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#else
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

#define ESC_WAIT_S 0.03        /* lone ESC is reported after this long with no follow-up byte */
#define IN_CAP 256
#define Q_CAP 64
#define SENTINEL_CP 0xFFFFFFFFu /* front-buffer marker: never equal to a sanitised cell */

/* 1000 = button press/release, 1003 = any-motion (hover, also reports moves without a button),
 * 1006 = SGR coordinates. */
#define ENTER_SEQ "\x1b[?1049h\x1b[?25l\x1b[?1000h\x1b[?1003h\x1b[?1006h\x1b[?7l\x1b[0m\x1b[2J\x1b[H"
#define RESTORE_SEQ "\x1b[0m\x1b[?1006l\x1b[?1003l\x1b[?1000l\x1b[?25h\x1b[?7h\x1b[?1049l"

typedef struct {
    uint32_t cp;
    TColor fg;
    TColor bg;
    uint8_t attr;
} Cell;

static bool g_active = false;
static bool g_atexit_set = false;
static volatile sig_atomic_t g_tty_modified = 0;  /* terminal modes must be restored */
static volatile sig_atomic_t g_resize_flag = 0;   /* set by SIGWINCH (POSIX) */

static Cell *g_back = NULL;
static Cell *g_front = NULL;
static int g_bw = 0, g_bh = 0;
static bool g_need_clear = false;
static int g_cur_x = -1, g_cur_y = -1;            /* believed terminal cursor, -1 = unknown */
static TColor g_sty_fg = TCOL_DEFAULT, g_sty_bg = TCOL_DEFAULT;  /* believed terminal SGR state */
static uint8_t g_sty_attr = 0;

static char *g_out = NULL;
static size_t g_out_len = 0, g_out_cap = 0;

static unsigned char g_in[IN_CAP];
static size_t g_in_len = 0;
static bool g_in_eof = false;
static double g_pend_since = -1.0;                /* when the pending partial input arrived */

static TermEvent g_q[Q_CAP];
static int g_q_head = 0, g_q_count = 0;

static void on_fatal_signal(int sig);             /* defined below, used by plat_open */
static void parse_input(bool final);              /* defined below, used by plat_read_input */

/* ---------- event queue ---------- */

static void q_push(const TermEvent *e)
{
    /* Hover (any-motion) reports can arrive in bursts: consecutive moves collapse into the
     * newest one so they can never push a click or key out of the full queue. */
    if (e->type == TEV_MOUSE && e->mkind == TM_MOVE && g_q_count > 0) {
        TermEvent *last = &g_q[(g_q_head + g_q_count - 1) % Q_CAP];
        if (last->type == TEV_MOUSE && last->mkind == TM_MOVE) {
            *last = *e;
            return;
        }
    }
    if (g_q_count >= Q_CAP) return;               /* drop newest when full */
    g_q[(g_q_head + g_q_count) % Q_CAP] = *e;
    g_q_count++;
}

static bool q_pop(TermEvent *e)
{
    if (g_q_count == 0) return false;
    *e = g_q[g_q_head];
    g_q_head = (g_q_head + 1) % Q_CAP;
    g_q_count--;
    return true;
}

static void push_key(TermKey k, uint32_t ch)
{
    TermEvent e;
    memset(&e, 0, sizeof e);
    e.type = TEV_KEY;
    e.key = k;
    e.ch = ch;
    q_push(&e);
}

static void push_mouse(TermMouseKind kind, int button, int x, int y)
{
    TermEvent e;
    memset(&e, 0, sizeof e);
    e.type = TEV_MOUSE;
    e.mkind = kind;
    e.mbutton = button;
    e.mx = x;
    e.my = y;
    q_push(&e);
}

static void push_resize(void)
{
    TermEvent e;
    memset(&e, 0, sizeof e);
    e.type = TEV_RESIZE;
    q_push(&e);
}

/* ---------- UTF-8 ---------- */

/* Decode one sequence from p (avail bytes readable). Returns bytes used, 0 if the sequence is
 * incomplete (only when avail is finite), and U+FFFD with 1 byte consumed for invalid input. */
static size_t utf8_seq(const unsigned char *p, size_t avail, uint32_t *cp)
{
    uint32_t v = 0, min = 0;
    unsigned need = 0, c, i;

    if (avail == 0) return 0;
    c = p[0];
    if (c < 0x80u) { *cp = c; return 1; }
    if (c >= 0xC2u && c <= 0xDFu) { need = 1; v = c & 0x1Fu; min = 0x80u; }
    else if (c >= 0xE0u && c <= 0xEFu) { need = 2; v = c & 0x0Fu; min = 0x800u; }
    else if (c >= 0xF0u && c <= 0xF4u) { need = 3; v = c & 0x07u; min = 0x10000u; }
    else { *cp = 0xFFFDu; return 1; }
    for (i = 1; i <= need; i++) {
        if (i >= avail) return 0;
        if ((p[i] & 0xC0u) != 0x80u) { *cp = 0xFFFDu; return 1; }
        v = (v << 6) | (p[i] & 0x3Fu);
    }
    if (v < min || v > 0x10FFFFu || (v >= 0xD800u && v <= 0xDFFFu)) { *cp = 0xFFFDu; return 1; }
    *cp = v;
    return need + 1;
}

int utf8_decode(const char *s, uint32_t *cp)
{
    const unsigned char *p = (const unsigned char *)s;
    if (p[0] == 0) { *cp = 0; return 0; }
    return (int)utf8_seq(p, (size_t)-1, cp);
}

int utf8_cell_width(const char *s)
{
    int n = 0;
    uint32_t cp;
    if (!s) return 0;
    while (*s) {
        s += utf8_decode(s, &cp);
        n++;
    }
    return n;
}

static int utf8_encode(uint32_t cp, char *o)
{
    if (cp < 0x80u) {
        o[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800u) {
        o[0] = (char)(0xC0u | (cp >> 6));
        o[1] = (char)(0x80u | (cp & 0x3Fu));
        return 2;
    }
    if (cp < 0x10000u) {
        o[0] = (char)(0xE0u | (cp >> 12));
        o[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        o[2] = (char)(0x80u | (cp & 0x3Fu));
        return 3;
    }
    o[0] = (char)(0xF0u | (cp >> 18));
    o[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
    o[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    o[3] = (char)(0x80u | (cp & 0x3Fu));
    return 4;
}

static uint32_t sanitize_cp(uint32_t cp)
{
    if (cp == 0) return ' ';
    if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) return 0xFFFDu;
    return cp;
}

/* ---------- platform layer ---------- */

#ifndef _WIN32

static struct termios g_orig_tio;

static double plat_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void on_winch(int sig)
{
    (void)sig;
    g_resize_flag = 1;
}

/* Enter raw mode (ISIG kept so Ctrl+C raises SIGINT) and install handlers. */
static bool plat_open(void)
{
    struct termios raw;
    struct sigaction sa;

    if (!isatty(0) || !isatty(1)) return false;
    if (tcgetattr(0, &g_orig_tio) != 0) return false;
    raw = g_orig_tio;
    raw.c_iflag &= ~(tcflag_t)(IXON | ICRNL | INLCR | IGNCR | BRKINT | INPCK | ISTRIP);
    raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ECHONL | IEXTEN);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSANOW, &raw) != 0) return false;

    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = on_fatal_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    /* No SA_RESTART: select() returns EINTR so term_poll notices the resize at once. */
    sa.sa_handler = on_winch;
    sigaction(SIGWINCH, &sa, NULL);
    return true;
}

static void plat_restore_modes(void)
{
    tcsetattr(0, TCSANOW, &g_orig_tio);
}

static bool plat_query_size(int *c, int *r)
{
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0 || ws.ws_row == 0) return false;
    *c = ws.ws_col;
    *r = ws.ws_row;
    return true;
}

static void plat_write(const char *s, size_t n)
{
    while (n > 0) {
        ssize_t w = write(1, s, n);
        if (w > 0) {
            s += w;
            n -= (size_t)w;
            continue;
        }
        if (w < 0 && errno == EINTR) continue;
        if (w < 0 && errno == EAGAIN) {
            fd_set wf;
            struct timeval tv;
            FD_ZERO(&wf);
            FD_SET(1, &wf);
            tv.tv_sec = 0;
            tv.tv_usec = 100000;
            select(2, NULL, &wf, NULL, &tv);
            continue;
        }
        return;
    }
}

/* 1 = input readable, 0 = timeout, -1 = interrupted (EINTR) */
static int plat_wait(double sec)
{
    struct timeval tv;
    fd_set rf;
    int r;
    if (sec < 0) sec = 0;
    tv.tv_sec = (time_t)sec;
    tv.tv_usec = (suseconds_t)((sec - (double)tv.tv_sec) * 1e6);
    FD_ZERO(&rf);
    FD_SET(0, &rf);
    /* After EOF on stdin, only sleep: stdin would stay readable and spin the loop. */
    r = select(g_in_eof ? 0 : 1, g_in_eof ? NULL : &rf, NULL, NULL, &tv);
    if (r < 0) return -1;
    return r == 0 ? 0 : 1;
}

static void plat_read_input(void)
{
    ssize_t n;
    if (g_in_len >= IN_CAP) parse_input(true);     /* buffer full: drain it */
    n = read(0, g_in + g_in_len, IN_CAP - g_in_len);
    if (n > 0) g_in_len += (size_t)n;
    else if (n == 0 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) g_in_eof = true;
}

#else /* _WIN32 */

static HANDLE g_hin = NULL, g_hout = NULL;
static DWORD g_orig_in_mode = 0, g_orig_out_mode = 0;
static UINT g_orig_cp = 0;
static uint32_t g_win_hi = 0;          /* pending UTF-16 high surrogate */
static DWORD g_win_buttons = 0;        /* last known button state */
static LARGE_INTEGER g_qpc_freq;

static double plat_now(void)
{
    LARGE_INTEGER c;
    if (g_qpc_freq.QuadPart == 0) QueryPerformanceFrequency(&g_qpc_freq);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)g_qpc_freq.QuadPart;
}

static bool plat_open(void)
{
    DWORD in_mode, out_mode;
    g_hin = GetStdHandle(STD_INPUT_HANDLE);
    g_hout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (g_hin == NULL || g_hin == INVALID_HANDLE_VALUE) return false;
    if (g_hout == NULL || g_hout == INVALID_HANDLE_VALUE) return false;
    if (!GetConsoleMode(g_hin, &g_orig_in_mode)) return false;
    if (!GetConsoleMode(g_hout, &g_orig_out_mode)) return false;
    g_win_buttons = 0;
    g_win_hi = 0;

    in_mode = (g_orig_in_mode | ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS
               | ENABLE_PROCESSED_INPUT)
              & ~(DWORD)(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_QUICK_EDIT_MODE);
    out_mode = g_orig_out_mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;

    if (!SetConsoleMode(g_hout, out_mode)) return false;
    if (!SetConsoleMode(g_hin, in_mode)) {
        SetConsoleMode(g_hout, g_orig_out_mode);
        return false;
    }
    g_orig_cp = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);

    signal(SIGINT, on_fatal_signal);
    signal(SIGTERM, on_fatal_signal);
    return true;
}

static void plat_restore_modes(void)
{
    if (g_hin) SetConsoleMode(g_hin, g_orig_in_mode);
    if (g_hout) SetConsoleMode(g_hout, g_orig_out_mode);
    if (g_orig_cp) SetConsoleOutputCP(g_orig_cp);
}

static bool plat_query_size(int *c, int *r)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!g_hout || !GetConsoleScreenBufferInfo(g_hout, &csbi)) return false;
    *c = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    *r = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return *c > 0 && *r > 0;
}

static void plat_write(const char *s, size_t n)
{
    while (n > 0) {
        DWORD chunk = (DWORD)(n < 0x40000000u ? n : 0x40000000u);
        DWORD w = 0;
        if (!WriteFile(g_hout, s, chunk, &w, NULL) || w == 0) return;
        s += w;
        n -= w;
    }
}

/* 1 = input available, 0 = timeout, -1 = failure */
static int plat_wait(double sec)
{
    DWORD ms, r;
    if (sec < 0) sec = 0;
    ms = (DWORD)(sec * 1000.0);
    if (sec > 0 && ms == 0) ms = 1;
    r = WaitForSingleObject(g_hin, ms);
    if (r == WAIT_OBJECT_0) return 1;
    if (r == WAIT_TIMEOUT) return 0;
    return -1;
}

static void win_handle_record(const INPUT_RECORD *r)
{
    switch (r->EventType) {
    case KEY_EVENT: {
        const KEY_EVENT_RECORD *k = &r->Event.KeyEvent;
        TermKey key = TK_NONE;
        uint32_t u;
        if (!k->bKeyDown) return;
        switch (k->wVirtualKeyCode) {
        case VK_UP: key = TK_UP; break;
        case VK_DOWN: key = TK_DOWN; break;
        case VK_LEFT: key = TK_LEFT; break;
        case VK_RIGHT: key = TK_RIGHT; break;
        case VK_RETURN: key = TK_ENTER; break;
        case VK_ESCAPE: key = TK_ESC; break;
        case VK_BACK: key = TK_BACKSPACE; break;
        case VK_TAB: key = TK_TAB; break;
        case VK_HOME: key = TK_HOME; break;
        case VK_END: key = TK_END; break;
        case VK_DELETE: key = TK_DELETE; break;
        case VK_PRIOR: key = TK_PGUP; break;
        case VK_NEXT: key = TK_PGDN; break;
        default: break;
        }
        if (key != TK_NONE) {
            g_win_hi = 0;
            push_key(key, 0);
            return;
        }
        u = (uint32_t)k->uChar.UnicodeChar;
        if (u >= 0xD800u && u <= 0xDBFFu) {          /* high surrogate: wait for the low half */
            g_win_hi = u;
            return;
        }
        if (u >= 0xDC00u && u <= 0xDFFFu) {
            if (g_win_hi) push_key(TK_CHAR, 0x10000u + ((g_win_hi - 0xD800u) << 10) + (u - 0xDC00u));
            g_win_hi = 0;
            return;
        }
        g_win_hi = 0;
        if (u >= 0x20u && u != 0x7Fu) push_key(TK_CHAR, u);  /* Ctrl+letter (<0x20) is ignored */
        return;
    }
    case MOUSE_EVENT: {
        const MOUSE_EVENT_RECORD *m = &r->Event.MouseEvent;
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        int ox = 0, oy = 0, mx, my, b;
        DWORD st;
        static const DWORD bits[3] = { FROM_LEFT_1ST_BUTTON_PRESSED, FROM_LEFT_2ND_BUTTON_PRESSED,
                                       RIGHTMOST_BUTTON_PRESSED };
        if (m->dwEventFlags & MOUSE_WHEELED) {
            /* wheel: the high word of dwButtonState is the signed delta; buttons unchanged */
            short delta = (short)HIWORD(m->dwButtonState);
            if (GetConsoleScreenBufferInfo(g_hout, &csbi)) {
                mx = m->dwMousePosition.X - csbi.srWindow.Left;
                my = m->dwMousePosition.Y - csbi.srWindow.Top;
            } else {
                mx = m->dwMousePosition.X;
                my = m->dwMousePosition.Y;
            }
            if (mx < 0) mx = 0;
            if (my < 0) my = 0;
            push_mouse(delta > 0 ? TM_WHEEL_UP : TM_WHEEL_DOWN, 0, mx, my);
            return;
        }
        if (m->dwEventFlags & MOUSE_HWHEELED) return;
        st = m->dwButtonState & (FROM_LEFT_1ST_BUTTON_PRESSED | FROM_LEFT_2ND_BUTTON_PRESSED
                                 | RIGHTMOST_BUTTON_PRESSED);
        if (GetConsoleScreenBufferInfo(g_hout, &csbi)) {
            ox = csbi.srWindow.Left;
            oy = csbi.srWindow.Top;
        }
        mx = m->dwMousePosition.X - ox;
        my = m->dwMousePosition.Y - oy;
        if (mx < 0) mx = 0;
        if (my < 0) my = 0;
        /* button transitions are checked on every record (a release can ride on a move) */
        for (b = 0; b < 3; b++) {
            if ((st & bits[b]) != (g_win_buttons & bits[b]))
                push_mouse((st & bits[b]) ? TM_PRESS : TM_RELEASE, b, mx, my);
        }
        if (m->dwEventFlags & MOUSE_MOVED) push_mouse(TM_MOVE, 0, mx, my);
        g_win_buttons = st;
        return;
    }
    case WINDOW_BUFFER_SIZE_EVENT:
        push_resize();
        return;
    default:
        return;
    }
}

static void plat_read_input(void)
{
    INPUT_RECORD recs[32];
    DWORD n = 0, got = 0, i;
    if (!GetNumberOfConsoleInputEvents(g_hin, &n) || n == 0) return;
    if (n > 32) n = 32;
    if (!ReadConsoleInputW(g_hin, recs, n, &got)) return;
    for (i = 0; i < got; i++) win_handle_record(&recs[i]);
}

#endif /* _WIN32 */

/* ---------- shared terminal state ---------- */

static void restore_tty(void)
{
    if (!g_tty_modified) return;
    g_tty_modified = 0;
    plat_write(RESTORE_SEQ, strlen(RESTORE_SEQ));
    plat_restore_modes();
}

/* Fatal signals: restore the terminal (only async-signal-safe calls) and exit. */
static void on_fatal_signal(int sig)
{
    restore_tty();
    _exit(128 + sig);
}

static Cell cell_make(uint32_t cp, TColor fg, TColor bg, uint8_t attr)
{
    Cell c;
    c.cp = cp;
    c.fg = fg;
    c.bg = bg;
    c.attr = attr;
    return c;
}

/* Track the terminal size; on change reallocate both buffers and request a full redraw. */
static void ensure_size(void)
{
    int c = 0, r = 0;
    size_t n, i;
    Cell *nb, *nf;
    if (!plat_query_size(&c, &r)) return;
    if (c < 1 || r < 1) return;
    if (c == g_bw && r == g_bh && g_back) return;
    n = (size_t)c * (size_t)r;
    nb = malloc(n * sizeof *nb);
    nf = malloc(n * sizeof *nf);
    if (!nb || !nf) {
        free(nb);
        free(nf);
        return;
    }
    for (i = 0; i < n; i++) {
        nb[i] = cell_make(' ', TCOL_DEFAULT, TCOL_DEFAULT, 0);
        nf[i] = cell_make(SENTINEL_CP, TCOL_DEFAULT, TCOL_DEFAULT, 0);
    }
    free(g_back);
    free(g_front);
    g_back = nb;
    g_front = nf;
    g_bw = c;
    g_bh = r;
    g_need_clear = true;
    g_cur_x = g_cur_y = -1;
}

static void out_put(const char *s, size_t n)
{
    if (g_out_len + n > g_out_cap) {
        size_t nc = g_out_cap ? g_out_cap : 65536;
        char *p;
        while (nc < g_out_len + n) nc *= 2;
        p = realloc(g_out, nc);
        if (!p) return;
        g_out = p;
        g_out_cap = nc;
    }
    if (g_out_len + n > g_out_cap) return;
    memcpy(g_out + g_out_len, s, n);
    g_out_len += n;
}

static void sgr_add(char *p, size_t cap, size_t *n, const char *s)
{
    int w = snprintf(p + *n, cap - *n, "%s%s", *n ? ";" : "", s);
    if (w > 0 && (size_t)w < cap - *n) *n += (size_t)w;
}

/* Emit one SGR sequence that moves the terminal from the believed state to (fg,bg,attr). */
static void emit_style(TColor fg, TColor bg, uint8_t attr)
{
    static const struct { uint8_t bit; const char *code; } attrs[] = {
        { TATTR_BOLD, "1" }, { TATTR_DIM, "2" }, { TATTR_UNDERLINE, "4" },
        { TATTR_BLINK, "5" }, { TATTR_REVERSE, "7" }
    };
    char p[128], tmp[32];
    size_t n = 0, i;
    TColor cf, cb;
    uint8_t ca;

    if (fg == g_sty_fg && bg == g_sty_bg && attr == g_sty_attr) return;
    cf = g_sty_fg;
    cb = g_sty_bg;
    ca = g_sty_attr;
    p[0] = 0;
    if ((ca & (uint8_t)~attr) != 0) {          /* attributes decreased: reset, then re-add */
        sgr_add(p, sizeof p, &n, "0");
        cf = TCOL_DEFAULT;
        cb = TCOL_DEFAULT;
        ca = 0;
    }
    for (i = 0; i < sizeof attrs / sizeof attrs[0]; i++) {
        if ((attr & attrs[i].bit) && !(ca & attrs[i].bit)) sgr_add(p, sizeof p, &n, attrs[i].code);
    }
    if (fg != cf) {
        if (fg == TCOL_DEFAULT) {
            sgr_add(p, sizeof p, &n, "39");
        } else {
            snprintf(tmp, sizeof tmp, "38;2;%u;%u;%u", (fg >> 16) & 255u, (fg >> 8) & 255u, fg & 255u);
            sgr_add(p, sizeof p, &n, tmp);
        }
    }
    if (bg != cb) {
        if (bg == TCOL_DEFAULT) {
            sgr_add(p, sizeof p, &n, "49");
        } else {
            snprintf(tmp, sizeof tmp, "48;2;%u;%u;%u", (bg >> 16) & 255u, (bg >> 8) & 255u, bg & 255u);
            sgr_add(p, sizeof p, &n, tmp);
        }
    }
    if (n) {
        out_put("\x1b[", 2);
        out_put(p, n);
        out_put("m", 1);
    }
    g_sty_fg = fg;
    g_sty_bg = bg;
    g_sty_attr = attr;
}

/* ---------- input parsing ---------- */

/* Parse up to cnt ';'-separated decimal numbers. Missing values are 0. */
static void parse_nums(const unsigned char *p, size_t n, int *v, int cnt)
{
    int idx = 0, k;
    long acc = 0;
    size_t i;
    for (k = 0; k < cnt; k++) v[k] = 0;
    for (i = 0; i < n; i++) {
        unsigned char c = p[i];
        if (c >= '0' && c <= '9') {
            if (acc < 100000000L) acc = acc * 10 + (c - '0');
        } else if (c == ';') {
            if (idx < cnt) v[idx] = (int)acc;
            idx++;
            acc = 0;
        }
    }
    if (idx < cnt) v[idx] = (int)acc;
}

static void handle_csi(const unsigned char *p, size_t n, unsigned char fin)
{
    int v[3];
    if (n > 0 && p[0] == '<') {                 /* SGR mouse: ESC [ < b ; x ; y (M|m) */
        int b, x, y, mx, my, button;
        if (fin != 'M' && fin != 'm') return;
        parse_nums(p + 1, n - 1, v, 3);
        b = v[0];
        x = v[1];
        y = v[2];
        mx = x > 0 ? x - 1 : 0;
        my = y > 0 ? y - 1 : 0;
        if (b & 64) {                            /* wheel */
            if ((b & 3) == 0) push_mouse(TM_WHEEL_UP, 0, mx, my);
            else if ((b & 3) == 1) push_mouse(TM_WHEEL_DOWN, 0, mx, my);
            return;
        }
        button = b & 3;
        if (b & 32) {                            /* motion */
            push_mouse(TM_MOVE, button == 3 ? 0 : button, mx, my);
            return;
        }
        if (button == 3) return;
        push_mouse(fin == 'M' ? TM_PRESS : TM_RELEASE, button, mx, my);
        return;
    }
    if (n > 0 && (p[0] == '?' || p[0] == '=' || p[0] == '>')) return;  /* private replies */

    parse_nums(p, n, v, 3);
    switch (fin) {
    case 'A': push_key(TK_UP, 0); break;
    case 'B': push_key(TK_DOWN, 0); break;
    case 'C': push_key(TK_RIGHT, 0); break;
    case 'D': push_key(TK_LEFT, 0); break;
    case 'H': push_key(TK_HOME, 0); break;
    case 'F': push_key(TK_END, 0); break;
    case 'Z': push_key(TK_TAB, 0); break;    /* shift+tab */
    case '~':
        switch (v[0]) {
        case 1: case 7: push_key(TK_HOME, 0); break;
        case 4: case 8: push_key(TK_END, 0); break;
        case 3: push_key(TK_DELETE, 0); break;
        case 5: push_key(TK_PGUP, 0); break;
        case 6: push_key(TK_PGDN, 0); break;
        default: break;
        }
        break;
    default:
        break;
    }
}

/* Consume one ESC-introduced sequence at p. Returns bytes consumed, or 0 if more input is
 * needed (only when !final). With final=true every call consumes at least one byte. */
static size_t parse_esc(const unsigned char *p, size_t avail, bool final)
{
    unsigned char d;
    size_t j;

    if (avail == 1) {
        if (final) {
            push_key(TK_ESC, 0);
            return 1;
        }
        return 0;
    }
    d = p[1];
    if (d == '[') {                                    /* CSI */
        j = 2;
        while (j < avail && p[j] >= 0x20 && p[j] <= 0x3F) j++;
        if (j >= avail) return final ? avail : 0;
        if (p[j] >= 0x40 && p[j] <= 0x7E) {
            handle_csi(p + 2, j - 2, p[j]);
            return j + 1;
        }
        return j;                                      /* malformed: drop the partial, keep p[j] */
    }
    if (d == 'O') {                                    /* SS3 */
        if (avail < 3) return final ? avail : 0;
        switch (p[2]) {
        case 'A': push_key(TK_UP, 0); break;
        case 'B': push_key(TK_DOWN, 0); break;
        case 'C': push_key(TK_RIGHT, 0); break;
        case 'D': push_key(TK_LEFT, 0); break;
        case 'H': push_key(TK_HOME, 0); break;
        case 'F': push_key(TK_END, 0); break;
        default: break;
        }
        return 3;
    }
    if (d == ']' || d == 'P' || d == 'X' || d == '^' || d == '_') {  /* OSC/DCS/SOS/PM/APC */
        for (j = 2; j < avail; j++) {
            if (p[j] == 0x07) return j + 1;
            if (p[j] == 0x1B) {
                if (j + 1 >= avail) return final ? avail : 0;
                if (p[j + 1] == '\\') return j + 2;
                return j;
            }
        }
        return final ? avail : 0;
    }
    push_key(TK_ESC, 0);                               /* ESC + ordinary byte: report ESC alone */
    return 1;
}

static void parse_input(bool final)
{
    size_t i = 0, n = g_in_len;
    while (i < n) {
        unsigned char c = g_in[i];
        size_t k = 1;
        if (c == 0x1B) {
            k = parse_esc(g_in + i, n - i, final);
            if (k == 0) break;
        } else if (c == '\r') {
            push_key(TK_ENTER, 0);
            if (i + 1 < n && g_in[i + 1] == '\n') k = 2;
        } else if (c == '\n') {
            push_key(TK_ENTER, 0);
        } else if (c == 0x7F || c == 0x08) {
            push_key(TK_BACKSPACE, 0);
        } else if (c == '\t') {
            push_key(TK_TAB, 0);
        } else if (c < 0x20) {
            /* other control keys are ignored (Ctrl+C arrives as SIGINT) */
        } else if (c < 0x7F) {
            push_key(TK_CHAR, c);
        } else {
            uint32_t cp = 0;
            size_t used = utf8_seq(g_in + i, n - i, &cp);
            if (used == 0) {
                if (!final) break;                    /* split character: wait for the rest */
                used = 1;
            } else if (used == 1 && cp == 0xFFFDu) {
                /* invalid byte: dropped */
            } else {
                push_key(TK_CHAR, cp);
            }
            k = used;
        }
        i += k;
    }
    if (i > 0) memmove(g_in, g_in + i, n - i);
    g_in_len = n - i;
    if (g_in_len == 0) g_pend_since = -1.0;
    else if (g_pend_since < 0) g_pend_since = plat_now();
}

/* ---------- public API ---------- */

bool term_init(void)
{
    if (g_active) return true;
    if (!plat_open()) return false;
    g_tty_modified = 1;
    g_in_len = 0;
    g_in_eof = false;
    g_pend_since = -1.0;
    g_q_head = 0;
    g_q_count = 0;
    g_sty_fg = TCOL_DEFAULT;
    g_sty_bg = TCOL_DEFAULT;
    g_sty_attr = 0;
    g_cur_x = g_cur_y = -1;
    g_active = true;
    if (!g_atexit_set) {
        atexit(term_shutdown);
        g_atexit_set = true;
    }
    plat_write(ENTER_SEQ, strlen(ENTER_SEQ));
    ensure_size();
    term_invalidate();
    return true;
}

void term_shutdown(void)
{
    if (!g_active) return;
    g_active = false;
    restore_tty();
    free(g_back);
    free(g_front);
    free(g_out);
    g_back = NULL;
    g_front = NULL;
    g_out = NULL;
    g_out_len = 0;
    g_out_cap = 0;
    g_bw = 0;
    g_bh = 0;
    g_cur_x = g_cur_y = -1;
}

void term_size(int *cols, int *rows)
{
    int c, r;
    if (g_active) ensure_size();
    c = g_bw > 0 ? g_bw : 80;
    r = g_bh > 0 ? g_bh : 24;
    if (cols) *cols = c;
    if (rows) *rows = r;
}

double term_now(void)
{
    return plat_now();
}

bool term_poll(TermEvent *ev, int timeout_ms)
{
    double deadline, now, wait, lim;
    bool pend;
    int r;

    if (!g_active || !ev) return false;
    deadline = plat_now() + (timeout_ms > 0 ? timeout_ms : 0) / 1000.0;
    for (;;) {
        if (q_pop(ev)) return true;
        if (g_in_len > 0) {
            parse_input(false);
            if (g_q_count > 0) continue;
        }
        if (g_resize_flag) {
            g_resize_flag = 0;
            ensure_size();
            push_resize();
            continue;
        }
        now = plat_now();
        pend = g_in_len > 0;
        if (pend && now >= g_pend_since + ESC_WAIT_S) {  /* no follow-up: flush the partial */
            parse_input(true);
            continue;
        }
        wait = deadline - now;
        if (wait < 0) wait = 0;
        if (pend) {
            lim = g_pend_since + ESC_WAIT_S - now;
            if (lim < wait) wait = lim;
        }
        r = plat_wait(wait);
        if (r > 0) {
            plat_read_input();
            continue;
        }
        if (!pend && plat_now() >= deadline) return false;
    }
}

void term_clear(TColor bg)
{
    size_t i, n;
    if (g_active) ensure_size();
    if (!g_back) return;
    n = (size_t)g_bw * (size_t)g_bh;
    for (i = 0; i < n; i++) g_back[i] = cell_make(' ', TCOL_DEFAULT, bg, 0);
}

void term_put(int x, int y, uint32_t cp, TColor fg, TColor bg, int attr)
{
    Cell *c;
    if (!g_back || x < 0 || y < 0 || x >= g_bw || y >= g_bh) return;
    c = &g_back[(size_t)y * (size_t)g_bw + (size_t)x];
    c->cp = sanitize_cp(cp);
    c->fg = fg;
    c->bg = bg;
    c->attr = (uint8_t)(attr & 0x1F);
}

int term_print(int x, int y, const char *utf8, TColor fg, TColor bg, int attr)
{
    int n = 0;
    uint32_t cp = 0;
    if (!utf8) return 0;
    while (*utf8) {
        size_t used = (size_t)utf8_decode(utf8, &cp);
        if (used == 0) break;
        utf8 += used;
        term_put(x + n, y, cp, fg, bg, attr);
        n++;
    }
    return n;
}

void term_fill(int x, int y, int w, int h, uint32_t cp, TColor fg, TColor bg)
{
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            term_put(x + i, y + j, cp, fg, bg, 0);
}

void term_box(int x, int y, int w, int h, int style, TColor fg, TColor bg)
{
    /* tl, tr, bl, br, horizontal, vertical */
    static const uint32_t glyph[4][6] = {
        { 0x250C, 0x2510, 0x2514, 0x2518, 0x2500, 0x2502 },  /* single */
        { 0x2554, 0x2557, 0x255A, 0x255D, 0x2550, 0x2551 },  /* double */
        { 0x250F, 0x2513, 0x2517, 0x251B, 0x2501, 0x2503 },  /* heavy  */
        { 0x256D, 0x256E, 0x2570, 0x256F, 0x2500, 0x2502 },  /* rounded */
    };
    const uint32_t *g;
    if (w <= 0 || h <= 0) return;
    if (style < 0 || style > 3) style = 0;
    g = glyph[style];
    if (w < 2 || h < 2) {                       /* degenerate: a plain line */
        if (h == 1) term_fill(x, y, w, 1, g[4], fg, bg);
        else term_fill(x, y, 1, h, g[5], fg, bg);
        return;
    }
    if (bg != TCOL_DEFAULT) term_fill(x + 1, y + 1, w - 2, h - 2, ' ', fg, bg);
    term_fill(x + 1, y, w - 2, 1, g[4], fg, bg);
    term_fill(x + 1, y + h - 1, w - 2, 1, g[4], fg, bg);
    term_fill(x, y + 1, 1, h - 2, g[5], fg, bg);
    term_fill(x + w - 1, y + 1, 1, h - 2, g[5], fg, bg);
    term_put(x, y, g[0], fg, bg, 0);
    term_put(x + w - 1, y, g[1], fg, bg, 0);
    term_put(x, y + h - 1, g[2], fg, bg, 0);
    term_put(x + w - 1, y + h - 1, g[3], fg, bg, 0);
}

void term_present(void)
{
    int x, y;
    if (!g_active) return;
    ensure_size();
    if (!g_back) return;
    g_out_len = 0;
    if (g_need_clear) {
        out_put("\x1b[2J", 4);
        g_need_clear = false;
        g_cur_x = g_cur_y = -1;
    }
    for (y = 0; y < g_bh; y++) {
        for (x = 0; x < g_bw; x++) {
            size_t idx = (size_t)y * (size_t)g_bw + (size_t)x;
            const Cell *b = &g_back[idx];
            Cell *f = &g_front[idx];
            char t[32], u[4];
            int l, ul;
            if (b->cp == f->cp && b->fg == f->fg && b->bg == f->bg && b->attr == f->attr) continue;
            if (g_cur_x != x || g_cur_y != y) {
                l = snprintf(t, sizeof t, "\x1b[%d;%dH", y + 1, x + 1);
                out_put(t, (size_t)l);
            }
            emit_style(b->fg, b->bg, b->attr);
            ul = utf8_encode(b->cp, u);
            out_put(u, (size_t)ul);
            *f = *b;
            g_cur_x = x + 1;
            g_cur_y = y;
        }
    }
    if (g_out_len) plat_write(g_out, g_out_len);
}

void term_invalidate(void)
{
    size_t i, n;
    if (!g_front) return;
    n = (size_t)g_bw * (size_t)g_bh;
    for (i = 0; i < n; i++) g_front[i] = cell_make(SENTINEL_CP, TCOL_DEFAULT, TCOL_DEFAULT, 0);
    g_need_clear = true;
}
