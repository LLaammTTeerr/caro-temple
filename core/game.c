/* STUB - replaced by unit "Game state". */
#include <string.h>
#include <time.h>
#include "game.h"
double game_afk_idle_s = 120.0;
double game_afk_grace_s = 30.0;
void game_new(Game *g, GameMode mode, Difficulty diff, int board_n, bool block_rule, const PlayerInfo players[2])
{
    memset(g, 0, sizeof *g);
    g->mode = mode; g->diff = diff; board_init(&g->board, board_n, block_rule);
    g->players[0] = players[0]; g->players[1] = players[1];
    g->ai_player = mode == MODE_PVE ? 1 : -1;
    g->clock[0] = g->clock[1] = GAME_CLOCK_SECONDS; g->start_time = (int64_t)time(NULL);
    g->winner = -1; g->undo_requester = -1;
}
Cell game_cell_of(int p) { return p == 0 ? CELL_X : CELL_O; }
bool game_is_ai_turn(const Game *g) { return g->mode == MODE_PVE && g->turn == g->ai_player && g->status == GS_PLAYING; }
bool game_last_move(const Game *g, Pos *out) { if (!g->move_count) return false; if (out) *out = g->moves[g->move_count - 1]; return true; }
bool game_play(Game *g, int r, int c)
{
    if (g->status != GS_PLAYING || !board_place(&g->board, r, c, game_cell_of(g->turn))) return false;
    g->moves[g->move_count++] = (Pos){r, c};
    if (rules_check_win_at(&g->board, r, c, &g->win_line)) { g->status = GS_OVER; g->end_reason = END_LINE; g->winner = g->turn; return true; }
    if (board_full(&g->board)) { g->status = GS_OVER; g->end_reason = END_DRAW; g->winner = -1; return true; }
    g->turn ^= 1; g->idle = 0; return true;
}
void game_tick(Game *g, double dt, bool clocks_running) { if (clocks_running && g->status == GS_PLAYING) { g->clock[g->turn] -= dt; g->elapsed += dt; } }
void game_activity(Game *g) { if (g->afk == AFK_NONE) g->idle = 0; }
void game_afk_continue(Game *g) { g->afk = AFK_NONE; g->idle = 0; }
bool game_can_request_undo(const Game *g, int player) { (void)g; (void)player; return false; }
bool game_request_undo(Game *g, int player) { (void)g; (void)player; return false; }
void game_answer_undo(Game *g, bool accept) { (void)accept; g->undo_requester = -1; }
void game_surrender(Game *g, int player) { g->status = GS_OVER; g->end_reason = END_SURRENDER; g->winner = 1 - player; }
