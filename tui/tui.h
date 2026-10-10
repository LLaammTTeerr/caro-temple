/* tui.h - terminal frontend application state and screen API.
 * tui/main.c (coordinator) owns the loop; tui_menu.c and tui_game.c implement screens. */
#ifndef CARO_TUI_H
#define CARO_TUI_H

#include <stdbool.h>
#include "term.h"
#include "../core/game.h"
#include "../core/storage.h"
#include "../core/actions.h"

#define TUI_MIN_COLS 80
#define TUI_MIN_ROWS 30

/* Theme (truecolor) */
#define TC_BG        0x181B11u
#define TC_PANEL     0x2B2F1Eu
#define TC_BORDER    0x565E3Au
#define TC_MOSS      0x5C7A2Eu
#define TC_GOLD      0xE8C25Au
#define TC_FIRE      0xFF5A1Fu
#define TC_FIRE_HI   0xFFB13Bu
#define TC_WATER     0x2FA8FFu
#define TC_WATER_HI  0x9BE7FFu
#define TC_TEXT      0xF4ECD0u
#define TC_DIM       0xB2AA8Cu
#define TC_DANGER    0xD9452Bu

typedef enum { TS_HOME = 0, TS_SETUP, TS_PROFILE, TS_SETTINGS, TS_SAVES, TS_GAME, TS_COUNT } TuiScreen;
typedef enum { TSAVES_LOAD = 0, TSAVES_SAVE } TuiSavesMode;

typedef struct {
    Settings settings;
    Game game;
    bool has_game;
    TuiScreen screen;
    TuiScreen prev_screen;     /* screen to return to from SETTINGS / SAVES */
    TuiSavesMode saves_mode;
    bool quit;
    unsigned rng;
    char toast[160];
    double toast_until;        /* term_now() deadline */
} TuiApp;

extern TuiApp g_tui;

/* main.c */
void tui_goto(TuiScreen s);          /* sets prev_screen, screen, calls the screen's enter */
void tui_toast(const char *msg);     /* 2.5 s message */
/* Map a key event to an action. player: 0 for WASD/Space, 1 for arrows/Enter, -1 for keys
 * shared by both (Esc=BACK, Home=HOME). Returns ACT_NONE if not an action key. */
ActionType tui_key_action(const TermEvent *ev, int *player);
void tui_save_settings(void);        /* persist + apply language */
void tui_start_new_game(void);       /* from settings like the GUI; goto TS_GAME */

/* tui_menu.c: HOME, SETUP, PROFILE, SETTINGS, SAVES */
void tui_menu_enter(TuiScreen s);
void tui_menu_event(const TermEvent *ev);
void tui_menu_update(double dt);
void tui_menu_draw(void);

/* tui_game.c: GAME screen incl. pause menu, undo/surrender/AFK/game-over popups */
void tui_game_enter(void);
void tui_game_event(const TermEvent *ev);
void tui_game_update(double dt);     /* game_tick, AI moves, AFK handling (autosave + TS_HOME) */
void tui_game_draw(void);

#endif
