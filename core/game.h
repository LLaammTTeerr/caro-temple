/* game.h - a single Caro match: turns, clocks, history, undo, surrender, AFK, result.
 * Pure C99, no I/O. Frontends call game_tick() every frame and render from the struct. */
#ifndef CARO_GAME_H
#define CARO_GAME_H

#include <stdbool.h>
#include <stdint.h>
#include "rules.h"
#include "ai.h"

#define GAME_CLOCK_SECONDS 600.0     /* 10 minutes per player per match */
#define GAME_MAX_MOVES (CARO_MAX_N * CARO_MAX_N)
#define PLAYER_NAME_MAX 24           /* bytes incl. NUL, UTF-8 */
#define AVATAR_COUNT 6      /* avatars a player can choose: 0..5 */
#define AVATAR_TOTAL 8      /* plus computer-only champions: 6 = Novice, 7 = Obsidian Warden */

/* AFK thresholds (seconds). Defaults 120 / 30. Frontends may override from env vars
 * CARO_AFK_IDLE / CARO_AFK_GRACE for testing. Defined in game.c. */
extern double game_afk_idle_s;
extern double game_afk_grace_s;

typedef enum { MODE_PVE = 0, MODE_PVP = 1 } GameMode;
typedef enum { GS_PLAYING = 0, GS_OVER } GameStatus;
typedef enum { END_NONE = 0, END_LINE, END_DRAW, END_TIMEOUT, END_SURRENDER } EndReason;
typedef enum { AFK_NONE = 0, AFK_PROMPT, AFK_TIMEOUT } AfkState;

typedef struct {
    char name[PLAYER_NAME_MAX];
    int avatar; /* 0..AVATAR_COUNT-1 for people; the computer uses game_champion_avatar() */
} PlayerInfo;

typedef struct {
    GameMode mode;
    Difficulty diff;          /* PvE only */
    Board board;
    PlayerInfo players[2];    /* players[0] plays X (fire) and moves first; players[1] plays O (water) */
    int ai_player;            /* PvE: index of the computer (always 1); PvP: -1 */
    int turn;                 /* index (0/1) of the player to move */
    Pos moves[GAME_MAX_MOVES];/* history; moves[i] was played by player (i % 2) */
    int move_count;
    double clock[2];          /* remaining thinking time per player, seconds */
    int64_t start_time;       /* wall-clock unix time when the match started */
    double elapsed;           /* seconds of play since start (only counts while ticking) */
    GameStatus status;
    EndReason end_reason;
    int winner;               /* 0/1, or -1 for draw / none */
    WinLine win_line;         /* valid when end_reason == END_LINE */
    int undo_requester;       /* PvP: player index waiting for opponent's consent, else -1 */
    AfkState afk;
    double idle;              /* seconds since last human activity */
    double afk_left;          /* seconds remaining in the AFK prompt countdown */
} Game;

/* Start a new match. players[] copied. start_time = time(NULL). Clocks = GAME_CLOCK_SECONDS. */
void game_new(Game *g, GameMode mode, Difficulty diff, int board_n, bool block_rule,
              const PlayerInfo players[2]);

/* Computer opponent ("champion") per difficulty, vs Computer mode:
 *   Easy -> 6 Novice, Medium -> 5 Automaton, Hard -> 7 Obsidian Warden.
 * Its display name is tr(S_CHAMP_EASY + diff), its title tr(S_CHAMP_EASY_TITLE + diff). */
int game_champion_avatar(Difficulty d);

/* Cell symbol for player index: 0 -> CELL_X, 1 -> CELL_O. */
Cell game_cell_of(int player);
bool game_is_ai_turn(const Game *g);   /* PvE and turn == ai_player and playing */
bool game_last_move(const Game *g, Pos *out); /* false if no moves */
/* Latest move made by `player` (0/1); false if that player has no move on the board. */
bool game_last_move_by(const Game *g, int player, Pos *out);
/* The move to highlight for the person about to act (PRD: "the opponent's latest move"):
 * PvE -> the computer's latest move (stays marked while the AI thinks);
 * PvP -> the latest move overall (always made by the opponent of the player to move). */
bool game_highlight_move(const Game *g, Pos *out);

/* Current player plays (r,c). Returns false if illegal / not playing / undo pending /
 * AFK prompt or timeout active.
 * Detects win (END_LINE) and draw (board full, END_DRAW), else switches turn. */
bool game_play(Game *g, int r, int c);

/* Note: game_play resets idle (and afk prompt state is untouched) only for HUMAN moves;
 * AI moves (game_is_ai_turn() true before the call) do not reset idle.
 *
 * Advance time by dt seconds. If clocks_running and status PLAYING and no undo pending and
 * no AFK prompt/timeout (the prompt is a pause), the current player's clock decreases; at 0 -> END_TIMEOUT, opponent wins.
 * AFK tracking runs whenever status PLAYING (even when clocks paused):
 * idle += dt; idle >= game_afk_idle_s -> AFK_PROMPT with afk_left = game_afk_grace_s;
 * in AFK_PROMPT afk_left -= dt; <= 0 -> AFK_TIMEOUT (frontend autosaves + goes Home). */
void game_tick(Game *g, double dt, bool clocks_running);

/* Any human input. Resets idle while afk == AFK_NONE. Does NOT dismiss the AFK prompt. */
void game_activity(Game *g);
/* The player clicked "Yes, keep playing" on the AFK prompt: afk = AFK_NONE, idle = 0. */
void game_afk_continue(Game *g);

/* Undo ("xin đi lại"):
 * PvP: only the player who made the last move may ask; sets undo_requester; the opponent
 *      then answers via game_answer_undo(). Accept: last move removed, turn back to requester.
 * PvE: human may ask when they have at least one move; the AI always accepts immediately:
 *      removes the AI reply (if the last move was the AI's) and the human's last move.
 * Not allowed when game over or a request is already pending. */
bool game_can_request_undo(const Game *g, int player);
bool game_request_undo(Game *g, int player);   /* returns true if request made / applied */
void game_answer_undo(Game *g, bool accept);

/* Player gives up: status OVER, END_SURRENDER, winner = opponent. */
void game_surrender(Game *g, int player);

#endif
