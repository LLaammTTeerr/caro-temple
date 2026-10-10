/* storage.h - settings, player profiles and 10 manual save slots, as small text files.
 * Data dir: env CARO_DATA_DIR if set; else Windows %APPDATA%\caro, macOS
 * ~/Library/Application Support/caro, other $XDG_DATA_HOME/caro or ~/.local/share/caro.
 * Created on demand. Missing/corrupt files never crash: they load as defaults / empty slots. */
#ifndef CARO_STORAGE_H
#define CARO_STORAGE_H

#include <stdbool.h>
#include <stdint.h>
#include "game.h"
#include "i18n.h"

#define SAVE_SLOTS 10

typedef struct {
    Lang lang;
    float bgm_volume;     /* 0..1 */
    float sfx_volume;     /* 0..1 */
    bool bgm_muted;
    bool sfx_muted;
    bool reduced_motion;
    bool fullscreen;
    PlayerInfo profile[2];/* remembered names/avatars for P1 and P2 */
    GameMode last_mode;   /* last choices on the new-game setup screen */
    int last_board;       /* 3, 5 or 10 */
    Difficulty last_diff;
    bool block_rule;
} Settings;

typedef struct {
    bool used;
    int64_t saved_at;     /* unix time */
    GameMode mode;
    Difficulty diff;
    int board_n;
    int move_count;
    char p1[PLAYER_NAME_MAX];
    char p2[PLAYER_NAME_MAX];
} SlotInfo;

const char *storage_dir(void);             /* resolved data directory (no trailing slash) */
void storage_set_dir(const char *dir);     /* override (tests); NULL restores default resolution */

/* Defaults: LANG_EN, volumes 0.7/0.8, unmuted, no reduced motion, windowed,
 * profiles "Fireboy"/avatar 0 and "Watergirl"/avatar 1, MODE_PVE, board 10, DIFF_MEDIUM, block_rule false. */
void settings_default(Settings *s);
bool settings_load(Settings *s);           /* fills defaults first; false if no/invalid file */
bool settings_save(const Settings *s);

/* slot in [0, SAVE_SLOTS). Saving stores the full Game (history, clocks, players, mode,
 * difficulty, board size, block rule, start_time, elapsed, turn). AFK/undo state not saved.
 * Loading validates and rebuilds the board by replaying moves; AFK reset, undo cleared. */
bool storage_save_game(int slot, const Game *g);
bool storage_load_game(int slot, Game *g);
bool storage_delete_slot(int slot);
void storage_list_slots(SlotInfo out[SAVE_SLOTS]);
int  storage_first_free_slot(void);        /* -1 if all used */
int  storage_autosave_slot(void);          /* first free slot, else the oldest slot */
int  storage_latest_slot(void);            /* most recently saved slot, -1 if none */

#endif
