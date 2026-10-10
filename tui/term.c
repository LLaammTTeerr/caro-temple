/* STUB - replaced by unit "TUI platform". Prints nothing useful. */
#include <stdio.h>
#include <time.h>
#include "term.h"
bool term_init(void) { return true; }
void term_shutdown(void) {}
void term_size(int *c, int *r) { *c = 120; *r = 40; }
double term_now(void) { return (double)clock() / CLOCKS_PER_SEC; }
bool term_poll(TermEvent *ev, int t) { (void)ev; (void)t; return false; }
void term_clear(TColor bg) { (void)bg; }
void term_put(int x, int y, uint32_t cp, TColor fg, TColor bg, int a) { (void)x; (void)y; (void)cp; (void)fg; (void)bg; (void)a; }
int term_print(int x, int y, const char *s, TColor fg, TColor bg, int a) { (void)x; (void)y; (void)fg; (void)bg; (void)a; return utf8_cell_width(s); }
void term_fill(int x, int y, int w, int h, uint32_t cp, TColor fg, TColor bg) { (void)x; (void)y; (void)w; (void)h; (void)cp; (void)fg; (void)bg; }
void term_box(int x, int y, int w, int h, int st, TColor fg, TColor bg) { (void)x; (void)y; (void)w; (void)h; (void)st; (void)fg; (void)bg; }
void term_present(void) {}
void term_invalidate(void) {}
int utf8_decode(const char *s, uint32_t *cp) { if (!*s) { *cp = 0; return 0; } *cp = (unsigned char)*s; return 1; }
int utf8_cell_width(const char *s) { int n = 0; while (*s) { if (((unsigned char)*s & 0xC0) != 0x80) n++; s++; } return n; }
