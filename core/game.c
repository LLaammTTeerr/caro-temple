/* game.c - a single Caro match: turns, clocks, history, undo, surrender, AFK, result. */
#include <string.h>
#include <time.h>
#include "game.h"

/* AFK thresholds, overridable by frontends (env vars) for testing. */
double game_afk_idle_s = 120.0;
double game_afk_grace_s = 30.0;

/* Copy a name, truncating to PLAYER_NAME_MAX-1 bytes without splitting a UTF-8 sequence. */
static void copy_name(char *dst, const char *src)
{
    size_t len = src ? strlen(src) : 0;
    size_t n = len < PLAYER_NAME_MAX - 1 ? len : PLAYER_NAME_MAX - 1;
    if (n < len)
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
    memset(dst, 0, PLAYER_NAME_MAX);
    if (n) memcpy(dst, src, n);
}

/* Remove the last move from the history and the board. */
static void pop_last_move(Game *g)
{
    Pos p = g->moves[g->move_count - 1];
    board_clear_cell(&g->board, p.r, p.c);
    g->move_count--;
}

void game_new(Game *g, GameMode mode, Difficulty diff, int board_n, bool block_rule,
              const PlayerInfo players[2])
{
    memset(g, 0, sizeof *g);
    g->mode = mode;
    g->diff = diff;
    board_init(&g->board, board_n, block_rule);
    for (int i = 0; i < 2; i++) {
        copy_name(g->players[i].name, players[i].name);
        g->players[i].avatar = players[i].avatar;
    }
    g->ai_player = mode == MODE_PVE ? 1 : -1;
    g->turn = 0;
    g->clock[0] = g->clock[1] = GAME_CLOCK_SECONDS;
    g->start_time = (int64_t)time(NULL);
    g->status = GS_PLAYING;
    g->end_reason = END_NONE;
    g->winner = -1;
    g->undo_requester = -1;
    g->afk = AFK_NONE;
}

Cell game_cell_of(int player)
{
    return player == 0 ? CELL_X : CELL_O;
}

bool game_is_ai_turn(const Game *g)
{
    return g->mode == MODE_PVE && g->turn == g->ai_player && g->status == GS_PLAYING;
}

bool game_last_move(const Game *g, Pos *out)
{
    if (g->move_count <= 0) return false;
    if (out) *out = g->moves[g->move_count - 1];
    return true;
}

bool game_play(Game *g, int r, int c)
{
    /* AFK prompt is modal and AFK timeout awaits the frontend: no moves until continued. */
    if (g->status != GS_PLAYING || g->undo_requester >= 0 || g->afk != AFK_NONE) return false;
    if (!board_place(&g->board, r, c, game_cell_of(g->turn))) return false;

    /* Only human moves count as activity; the AI's move must not reset idle. */
    if (!game_is_ai_turn(g)) g->idle = 0;

    g->moves[g->move_count++] = (Pos){r, c};
    if (rules_check_win_at(&g->board, r, c, &g->win_line)) {
        g->status = GS_OVER;
        g->end_reason = END_LINE;
        g->winner = g->turn;
        return true;
    }
    if (board_full(&g->board)) {
        g->status = GS_OVER;
        g->end_reason = END_DRAW;
        g->winner = -1;
        return true;
    }
    g->turn ^= 1;
    return true;
}

void game_tick(Game *g, double dt, bool clocks_running)
{
    if (!(dt > 0) || g->status != GS_PLAYING) return;

    /* Thinking clock: paused during AFK prompt/timeout and while an undo is pending. */
    if (clocks_running && g->afk == AFK_NONE && g->undo_requester < 0) {
        g->clock[g->turn] -= dt;
        g->elapsed += dt;
        if (g->clock[g->turn] <= 0) {
            g->clock[g->turn] = 0;
            g->status = GS_OVER;
            g->end_reason = END_TIMEOUT;
            g->winner = 1 - g->turn;
            return;
        }
    }

    /* AFK tracking: runs whenever the match is playing. */
    if (g->afk == AFK_NONE) {
        if (!game_is_ai_turn(g)) g->idle += dt;
        if (g->idle >= game_afk_idle_s) {
            g->afk = AFK_PROMPT;
            g->afk_left = game_afk_grace_s;
        }
    } else if (g->afk == AFK_PROMPT) {
        g->afk_left -= dt;
        if (g->afk_left <= 0) {
            g->afk = AFK_TIMEOUT;
            g->afk_left = 0;
        }
    }
}

void game_activity(Game *g)
{
    if (g->afk == AFK_NONE) g->idle = 0;
}

void game_afk_continue(Game *g)
{
    g->afk = AFK_NONE;
    g->idle = 0;
}

bool game_can_request_undo(const Game *g, int player)
{
    if (g->status != GS_PLAYING || g->undo_requester >= 0 || g->move_count <= 0) return false;
    if (g->mode == MODE_PVE) {
        /* Human only; needs at least one human move on the board (human moves first). */
        return player == 0;
    }
    /* PvP: only the player who made the last move may ask. */
    return (g->move_count - 1) % 2 == player;
}

bool game_request_undo(Game *g, int player)
{
    if (!game_can_request_undo(g, player)) return false;
    if (g->mode == MODE_PVP) {
        g->undo_requester = player;
        return true;
    }
    /* PvE: the AI accepts immediately. Drop the AI reply if it is the last move,
     * then the human's last move, so the human is to move again. */
    if ((g->move_count - 1) % 2 == g->ai_player) pop_last_move(g);
    if (g->move_count > 0) pop_last_move(g);
    g->turn = 0;
    memset(&g->win_line, 0, sizeof g->win_line);
    return true;
}

void game_answer_undo(Game *g, bool accept)
{
    int requester = g->undo_requester;
    g->undo_requester = -1;
    if (!accept || requester < 0 || g->status != GS_PLAYING || g->move_count <= 0) return;
    pop_last_move(g);
    g->turn = requester;
    memset(&g->win_line, 0, sizeof g->win_line);
}

void game_surrender(Game *g, int player)
{
    if (g->status != GS_PLAYING || (player != 0 && player != 1)) return;
    g->status = GS_OVER;
    g->end_reason = END_SURRENDER;
    g->winner = 1 - player;
    g->undo_requester = -1;
}
