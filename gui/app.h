/* app.h - global application state for the raylib frontend. Implemented in gui/app.c (coordinator). */
#ifndef CARO_APP_H
#define CARO_APP_H

#include <stdbool.h>
#include "raylib.h"
#include "../core/game.h"
#include "../core/storage.h"

/* Logical resolution. Everything is drawn into a VIRTUAL_W x VIRTUAL_H render target and
 * scaled (letterboxed) to the window. Use these coordinates everywhere. */
#define VIRTUAL_W 1280
#define VIRTUAL_H 720

typedef enum { SAVES_LOAD = 0, SAVES_SAVE } SavesMode;

typedef struct {
    Settings settings;
    Game game;
    bool has_game;        /* a match is in memory (in progress or just finished) */
    SavesMode saves_mode; /* set before pushing SCENE_SAVES */
    bool quit;            /* set to exit the main loop */
    float time;           /* seconds since launch (for animations) */
    unsigned rng;         /* xorshift state for AI */
    char toast[160];      /* transient message, drawn by main */
    float toast_t;        /* seconds remaining */
} App;

extern App g_app;

void app_toast(const char *msg);       /* show a 2.5 s toast at the bottom of the screen */
void app_save_settings(void);          /* persist + re-apply language / audio volumes */
/* Create g_app.game from settings (last_mode/last_board/last_diff/block_rule/profile[]),
 * PvE: players[1] = the difficulty's champion (name tr(S_CHAMP_EASY + diff), game_champion_avatar). Sets has_game, goes to SCENE_GAME. */
void app_start_new_game(void);
/* Load slot into g_app.game; on success goes to SCENE_GAME, else toast S_LOAD_FAILED. */
bool app_load_slot(int slot);
/* Save g_app.game to slot and toast S_SAVED_FMT. */
bool app_save_slot(int slot);

#endif
