/* STUB - replaced by unit "TUI screens". */
#include "tui.h"
void tui_menu_enter(TuiScreen s) { (void)s; }
void tui_menu_event(const TermEvent *ev) { if (ev->type == TEV_KEY && (ev->key == TK_ESC || (ev->key == TK_CHAR && ev->ch == 'q'))) g_tui.quit = true; }
void tui_menu_update(double dt) { (void)dt; }
void tui_menu_draw(void) { term_print(2, 1, "CARO TEMPLE (stub) - q to quit", TC_GOLD, TC_BG, TATTR_BOLD); }
