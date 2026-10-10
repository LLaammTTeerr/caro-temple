/* term.h - zero-dependency terminal layer (tui/term.c): only the C standard library plus the
 * OS console API (POSIX termios/ioctl/select on Unix, Win32 console API on Windows).
 * Back-buffered cell grid with diffed output; truecolor ANSI escapes; UTF-8. */
#ifndef CARO_TERM_H
#define CARO_TERM_H

#include <stdbool.h>
#include <stdint.h>

typedef uint32_t TColor;               /* 0xRRGGBB */
#define TCOL_DEFAULT 0xFF000000u       /* terminal default fg/bg */

enum { TATTR_NONE = 0, TATTR_BOLD = 1, TATTR_DIM = 2, TATTR_UNDERLINE = 4, TATTR_REVERSE = 8, TATTR_BLINK = 16 };

typedef enum {
    TK_NONE = 0,
    TK_CHAR,          /* printable codepoint in ev.ch (letters as typed, e.g. 'w','W',' ') */
    TK_UP, TK_DOWN, TK_LEFT, TK_RIGHT,
    TK_ENTER, TK_ESC, TK_BACKSPACE, TK_TAB, TK_HOME, TK_END, TK_DELETE,
    TK_PGUP, TK_PGDN
} TermKey;

typedef enum { TEV_NONE = 0, TEV_KEY, TEV_MOUSE, TEV_RESIZE } TermEventType;
typedef enum { TM_PRESS = 0, TM_RELEASE, TM_MOVE, TM_WHEEL_UP, TM_WHEEL_DOWN } TermMouseKind;

typedef struct {
    TermEventType type;
    TermKey key;      /* TEV_KEY */
    uint32_t ch;      /* TEV_KEY with TK_CHAR */
    TermMouseKind mkind; /* TEV_MOUSE */
    int mbutton;      /* 0 left, 1 middle, 2 right */
    int mx, my;       /* 0-based cell coords */
} TermEvent;

/* Enter raw mode, alternate screen, hide cursor, enable SGR mouse (1000+1002+1006),
 * UTF-8 output (Windows: SetConsoleOutputCP(CP_UTF8) + ENABLE_VIRTUAL_TERMINAL_PROCESSING),
 * install SIGINT/SIGTERM/atexit handlers that restore the terminal. */
bool term_init(void);
void term_shutdown(void);                 /* idempotent; restores everything */
void term_size(int *cols, int *rows);
double term_now(void);                    /* monotonic seconds */

/* Wait up to timeout_ms for one event (0 = poll). Lone ESC is reported after ~30 ms with no follow-up. */
bool term_poll(TermEvent *ev, int timeout_ms);

/* Drawing into the back buffer (clipped). Wide chars are treated as width 1 (avoid them). */
void term_clear(TColor bg);
void term_put(int x, int y, uint32_t cp, TColor fg, TColor bg, int attr);
int  term_print(int x, int y, const char *utf8, TColor fg, TColor bg, int attr); /* returns cells used */
void term_fill(int x, int y, int w, int h, uint32_t cp, TColor fg, TColor bg);
/* Box using Unicode box drawing: style 0 single, 1 double, 2 heavy, 3 rounded. bg fills inside if != TCOL_DEFAULT. */
void term_box(int x, int y, int w, int h, int style, TColor fg, TColor bg);
void term_present(void);                  /* flush only changed cells; full redraw after resize */
void term_invalidate(void);               /* force full redraw next present */

int utf8_cell_width(const char *utf8);    /* number of codepoints (each = 1 cell) */
int utf8_decode(const char *s, uint32_t *cp); /* bytes consumed (>=1), cp=U+FFFD on error, 0 at NUL */

#endif
