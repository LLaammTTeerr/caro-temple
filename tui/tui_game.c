/* STUB - replaced by unit "TUI screens". */
#include "tui.h"
void tui_game_enter(void) {}
void tui_game_event(const TermEvent *ev) { (void)ev; }
void tui_game_update(double dt) { game_tick(&g_tui.game, dt, true); }
void tui_game_draw(void) {}
