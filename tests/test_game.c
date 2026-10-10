/* test_game.c - unit tests for core/game.c (turns, clocks, AFK, undo, surrender). */
#include <stdio.h>
#include <string.h>
#include "game.h"

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

static int near(double a, double b) { return a - b < 1e-9 && b - a < 1e-9; }

static PlayerInfo make_player(const char *name, int avatar)
{
    PlayerInfo p;
    memset(&p, 0, sizeof p);
    strncpy(p.name, name, sizeof p.name - 1);
    p.avatar = avatar;
    return p;
}

static void new_pvp(Game *g, int n)
{
    PlayerInfo pl[2] = { make_player("Fire", 0), make_player("Water", 1) };
    game_new(g, MODE_PVP, DIFF_EASY, n, false, pl);
}

static void new_pve(Game *g, int n)
{
    PlayerInfo pl[2] = { make_player("Human", 0), make_player("Bot", 1) };
    game_new(g, MODE_PVE, DIFF_MEDIUM, n, false, pl);
}

static void test_new_defaults(void)
{
    Game g;
    new_pvp(&g, 3);
    CHECK(g.mode == MODE_PVP);
    CHECK(g.board.n == 3);
    CHECK(g.board.win_len == 3);
    CHECK(g.ai_player == -1);
    CHECK(g.turn == 0);
    CHECK(g.move_count == 0);
    CHECK(near(g.clock[0], GAME_CLOCK_SECONDS));
    CHECK(near(g.clock[1], GAME_CLOCK_SECONDS));
    CHECK(g.status == GS_PLAYING);
    CHECK(g.end_reason == END_NONE);
    CHECK(g.winner == -1);
    CHECK(g.undo_requester == -1);
    CHECK(g.afk == AFK_NONE);
    CHECK(near(g.idle, 0.0));
    CHECK(g.elapsed == 0.0);
    CHECK(strcmp(g.players[0].name, "Fire") == 0);
    CHECK(strcmp(g.players[1].name, "Water") == 0);
    CHECK(g.players[1].avatar == 1);
    CHECK(g.start_time > 0);

    /* Name longer than the buffer is truncated and NUL-terminated. */
    PlayerInfo pl[2] = { make_player("ABCDEFGHIJKLMNOPQRSTUVWXYZ", 2), make_player("B", 3) };
    game_new(&g, MODE_PVE, DIFF_HARD, 5, true, pl);
    CHECK(g.mode == MODE_PVE);
    CHECK(g.ai_player == 1);
    CHECK(g.diff == DIFF_HARD);
    CHECK(g.board.n == 5);
    CHECK(g.board.win_len == 5);
    CHECK(g.board.block_rule);
    CHECK(g.players[0].name[PLAYER_NAME_MAX - 1] == '\0');
    CHECK(strlen(g.players[0].name) == PLAYER_NAME_MAX - 1);
    CHECK(game_cell_of(0) == CELL_X);
    CHECK(game_cell_of(1) == CELL_O);
}

static void test_alternate_turns(void)
{
    Game g;
    new_pvp(&g, 3);
    CHECK(game_play(&g, 0, 0));
    CHECK(g.turn == 1);
    CHECK(g.board.c[0][0] == CELL_X);
    CHECK(g.move_count == 1);
    CHECK(game_play(&g, 1, 1));
    CHECK(g.turn == 0);
    CHECK(g.board.c[1][1] == CELL_O);
    CHECK(g.move_count == 2);
    Pos p;
    CHECK(game_last_move(&g, &p));
    CHECK(p.r == 1 && p.c == 1);

    Game fresh;
    new_pvp(&fresh, 3);
    CHECK(!game_last_move(&fresh, &p));
}

static void test_illegal_moves(void)
{
    Game g;
    new_pvp(&g, 3);
    CHECK(!game_play(&g, -1, 0));
    CHECK(!game_play(&g, 0, 3));
    CHECK(!game_play(&g, 3, 3));
    CHECK(g.move_count == 0);
    CHECK(g.turn == 0);
    CHECK(game_play(&g, 1, 1));
    CHECK(!game_play(&g, 1, 1));    /* occupied */
    CHECK(g.move_count == 1);
    CHECK(g.turn == 1);             /* rejected move did not switch turn */
}

static void test_win_3x3(void)
{
    Game g;
    new_pvp(&g, 3);
    CHECK(game_play(&g, 0, 0));  /* X */
    CHECK(game_play(&g, 1, 0));  /* O */
    CHECK(game_play(&g, 0, 1));  /* X */
    CHECK(game_play(&g, 1, 1));  /* O */
    CHECK(g.status == GS_PLAYING);
    CHECK(game_play(&g, 0, 2));  /* X wins row 0 */
    CHECK(g.status == GS_OVER);
    CHECK(g.end_reason == END_LINE);
    CHECK(g.winner == 0);
    CHECK(g.win_line.found);
    CHECK(g.win_line.who == CELL_X);
    CHECK(g.win_line.len == 3);
    CHECK(g.win_line.cells[0].r == 0 && g.win_line.cells[0].c == 0);
    /* Moves after game over are rejected. */
    CHECK(!game_play(&g, 2, 2));
    CHECK(g.move_count == 5);
}

static void test_draw_3x3(void)
{
    /* X O X
     * X O O
     * O X X   -- no line for either side */
    Game g;
    new_pvp(&g, 3);
    CHECK(game_play(&g, 0, 0));  /* X */
    CHECK(game_play(&g, 0, 1));  /* O */
    CHECK(game_play(&g, 0, 2));  /* X */
    CHECK(game_play(&g, 1, 1));  /* O */
    CHECK(game_play(&g, 1, 0));  /* X */
    CHECK(game_play(&g, 1, 2));  /* O */
    CHECK(game_play(&g, 2, 1));  /* X */
    CHECK(game_play(&g, 2, 0));  /* O */
    CHECK(g.status == GS_PLAYING);
    CHECK(game_play(&g, 2, 2));  /* X, board full */
    CHECK(g.status == GS_OVER);
    CHECK(g.end_reason == END_DRAW);
    CHECK(g.winner == -1);
    CHECK(!g.win_line.found);
    CHECK(!game_play(&g, 0, 0));
}

static void test_clocks(void)
{
    Game g;
    new_pvp(&g, 3);
    game_tick(&g, 5.0, true);
    CHECK(near(g.clock[0], GAME_CLOCK_SECONDS - 5.0));
    CHECK(near(g.clock[1], GAME_CLOCK_SECONDS));
    CHECK(near(g.elapsed, 5.0));

    CHECK(game_play(&g, 0, 0));
    game_tick(&g, 3.0, true);
    CHECK(near(g.clock[0], GAME_CLOCK_SECONDS - 5.0));
    CHECK(near(g.clock[1], GAME_CLOCK_SECONDS - 3.0));
    CHECK(near(g.elapsed, 8.0));

    /* Paused clocks do not move. */
    game_tick(&g, 100.0, false);
    CHECK(near(g.clock[1], GAME_CLOCK_SECONDS - 3.0));
    CHECK(g.status == GS_PLAYING);

    /* Timeout: current player runs out, opponent wins. */
    g.clock[1] = 1.0;
    game_tick(&g, 2.0, true);
    CHECK(g.status == GS_OVER);
    CHECK(g.end_reason == END_TIMEOUT);
    CHECK(g.winner == 0);
    CHECK(near(g.clock[1], 0.0));
    CHECK(!game_play(&g, 2, 2));

    /* A huge dt is handled gracefully: clock clamps to 0, no negative values. */
    Game h;
    new_pvp(&h, 3);
    game_tick(&h, 1e12, true);
    CHECK(h.status == GS_OVER);
    CHECK(h.end_reason == END_TIMEOUT);
    CHECK(h.winner == 1);
    CHECK(near(h.clock[0], 0.0));
}

static void test_afk(void)
{
    double old_idle = game_afk_idle_s, old_grace = game_afk_grace_s;
    game_afk_idle_s = 1.0;
    game_afk_grace_s = 0.5;

    Game g;
    new_pvp(&g, 3);
    game_tick(&g, 0.5, false);
    CHECK(g.afk == AFK_NONE);
    game_tick(&g, 0.5, false);           /* idle reaches 1.0 */
    CHECK(g.afk == AFK_PROMPT);
    CHECK(near(g.afk_left, 0.5));

    /* Activity does not dismiss the prompt. */
    game_activity(&g);
    CHECK(g.afk == AFK_PROMPT);

    /* Countdown: the prompt is a pause, so the clock of the player to move stays put. */
    game_tick(&g, 0.25, true);
    CHECK(g.afk == AFK_PROMPT);
    CHECK(near(g.afk_left, 0.25));
    CHECK(near(g.clock[0], GAME_CLOCK_SECONDS));
    game_tick(&g, 0.3, true);
    CHECK(g.afk == AFK_TIMEOUT);
    CHECK(near(g.afk_left, 0.0));
    CHECK(g.status == GS_PLAYING);
    CHECK(near(g.clock[0], GAME_CLOCK_SECONDS));

    /* Timeout stays until frontend handles it. */
    game_tick(&g, 10.0, false);
    CHECK(g.afk == AFK_TIMEOUT);

    /* "Yes, keep playing" from the prompt resets everything. */
    Game k;
    new_pvp(&k, 3);
    game_tick(&k, 1.0, false);
    CHECK(k.afk == AFK_PROMPT);
    game_afk_continue(&k);
    CHECK(k.afk == AFK_NONE);
    CHECK(near(k.idle, 0.0));
    game_tick(&k, 0.9, false);
    CHECK(k.afk == AFK_NONE);

    /* A human move resets idle. */
    Game m;
    new_pvp(&m, 3);
    game_tick(&m, 0.9, false);
    CHECK(game_play(&m, 0, 0));
    CHECK(near(m.idle, 0.0));
    game_tick(&m, 0.9, false);
    CHECK(m.afk == AFK_NONE);
    game_tick(&m, 0.2, false);
    CHECK(m.afk == AFK_PROMPT);

    /* In PvE the AI's turn does not accumulate idle time. */
    Game p;
    new_pve(&p, 3);
    CHECK(game_play(&p, 0, 0));          /* human move */
    CHECK(game_is_ai_turn(&p));
    game_tick(&p, 5.0, false);
    CHECK(p.afk == AFK_NONE);
    CHECK(near(p.idle, 0.0));
    CHECK(game_play(&p, 1, 1));          /* AI move does not reset idle either, but it's 0 */
    CHECK(near(p.idle, 0.0));
    game_tick(&p, 0.9, false);           /* now a human turn: idle counts */
    CHECK(p.afk == AFK_NONE);
    CHECK(near(p.idle, 0.9));

    /* Idle tracking stops once the game is over. */
    Game o;
    new_pvp(&o, 3);
    game_surrender(&o, 0);
    game_tick(&o, 500.0, true);
    CHECK(o.afk == AFK_NONE);

    game_afk_idle_s = old_idle;
    game_afk_grace_s = old_grace;
}

/* A click during the AFK prompt (modal) or after the AFK timeout must not place a stone. */
static void test_play_blocked_by_afk(void)
{
    double old_idle = game_afk_idle_s, old_grace = game_afk_grace_s;
    game_afk_idle_s = 1.0;
    game_afk_grace_s = 0.5;

    /* AFK_PROMPT: play refused, board, turn, history and idle untouched */
    Game g;
    new_pvp(&g, 3);
    game_tick(&g, 1.0, false);
    CHECK(g.afk == AFK_PROMPT);
    CHECK(!game_play(&g, 0, 0));
    CHECK(g.move_count == 0);
    CHECK(g.board.c[0][0] == CELL_EMPTY);
    CHECK(g.turn == 0);
    CHECK(near(g.idle, 1.0));                /* a refused click is not activity */

    /* after "keep playing" the same click works */
    game_afk_continue(&g);
    CHECK(game_play(&g, 0, 0));
    CHECK(g.move_count == 1 && g.turn == 1);

    /* AFK_TIMEOUT: stray click refused, and it stays refused until the frontend acts */
    Game t;
    new_pvp(&t, 3);
    game_tick(&t, 1.0, false);
    game_tick(&t, 0.5, false);
    CHECK(t.afk == AFK_TIMEOUT);
    CHECK(!game_play(&t, 1, 1));
    CHECK(t.move_count == 0 && t.board.c[1][1] == CELL_EMPTY && t.turn == 0);
    CHECK(t.status == GS_PLAYING);
    game_tick(&t, 10.0, false);
    CHECK(!game_play(&t, 1, 1));
    CHECK(t.move_count == 0);

    /* PvE: the human cannot play while the prompt is up, so the AI is not left in a
     * half-applied state either */
    Game p;
    new_pve(&p, 3);
    CHECK(game_play(&p, 0, 0));              /* human */
    game_tick(&p, 1.0, false);               /* AI turn: idle does not count, no prompt */
    CHECK(p.afk == AFK_NONE);
    CHECK(game_play(&p, 1, 1));              /* AI reply */
    game_tick(&p, 1.0, false);               /* human turn: prompt */
    CHECK(p.afk == AFK_PROMPT);
    CHECK(!game_play(&p, 0, 1));
    CHECK(p.move_count == 2 && p.turn == 0);

    game_afk_idle_s = old_idle;
    game_afk_grace_s = old_grace;
}

static void test_pvp_undo(void)
{
    Game g;
    new_pvp(&g, 3);
    CHECK(!game_can_request_undo(&g, 0));   /* no moves yet */
    CHECK(game_play(&g, 0, 0));             /* X */
    CHECK(game_play(&g, 1, 1));             /* O */
    CHECK(game_play(&g, 0, 1));             /* X: last mover is player 0 */

    CHECK(game_can_request_undo(&g, 0));
    CHECK(!game_can_request_undo(&g, 1));
    CHECK(!game_request_undo(&g, 1));       /* opponent of last mover may not ask */
    CHECK(g.undo_requester == -1);

    CHECK(game_request_undo(&g, 0));
    CHECK(g.undo_requester == 0);
    CHECK(!game_can_request_undo(&g, 0));   /* already pending */
    CHECK(!game_request_undo(&g, 0));
    CHECK(!game_play(&g, 2, 2));            /* pending blocks play */
    CHECK(g.move_count == 3);

    /* Decline: board unchanged, request cleared, turn unchanged. */
    game_answer_undo(&g, false);
    CHECK(g.undo_requester == -1);
    CHECK(g.move_count == 3);
    CHECK(g.board.c[0][1] == CELL_X);
    CHECK(g.turn == 1);
    CHECK(game_play(&g, 2, 2));             /* play resumes */
    CHECK(g.turn == 0);

    /* Accept: last move removed, turn returns to the requester. Last move (2,2) was O's. */
    CHECK(game_can_request_undo(&g, 1));
    CHECK(!game_can_request_undo(&g, 0));
    CHECK(game_request_undo(&g, 1));
    game_answer_undo(&g, true);
    CHECK(g.move_count == 3);
    CHECK(g.board.c[2][2] == CELL_EMPTY);
    CHECK(g.turn == 1);
    CHECK(g.undo_requester == -1);
    CHECK(g.status == GS_PLAYING);

    /* Accepted undo of X's last move. */
    Game h;
    new_pvp(&h, 3);
    CHECK(game_play(&h, 0, 0));
    CHECK(game_play(&h, 1, 1));
    CHECK(!game_request_undo(&h, 0));       /* X is not the last mover (O at (1,1)) */
    CHECK(game_request_undo(&h, 1));
    game_answer_undo(&h, true);
    CHECK(h.move_count == 1);
    CHECK(h.board.c[1][1] == CELL_EMPTY);
    CHECK(h.board.c[0][0] == CELL_X);
    CHECK(h.turn == 1);
    CHECK(h.undo_requester == -1);
    CHECK(game_can_request_undo(&h, 0));    /* X's move (0,0) is now the last one */
}

static void test_pve_undo(void)
{
    Game g;
    new_pve(&g, 3);
    CHECK(!game_can_request_undo(&g, 0));    /* no moves yet */
    CHECK(!game_request_undo(&g, 0));
    CHECK(game_play(&g, 0, 0));              /* human X */
    CHECK(game_play(&g, 2, 2));              /* AI O */
    CHECK(g.turn == 0);
    CHECK(game_can_request_undo(&g, 0));
    CHECK(!game_can_request_undo(&g, 1));    /* AI never asks */

    /* Applied immediately: AI reply and human move both removed. */
    CHECK(game_request_undo(&g, 0));
    CHECK(g.move_count == 0);
    CHECK(g.board.c[0][0] == CELL_EMPTY);
    CHECK(g.board.c[2][2] == CELL_EMPTY);
    CHECK(g.turn == 0);
    CHECK(g.undo_requester == -1);

    /* Human moved but AI has not yet replied (last move is the human's). */
    CHECK(game_play(&g, 1, 1));
    CHECK(game_request_undo(&g, 0));
    CHECK(g.move_count == 0);
    CHECK(g.turn == 0);

    /* Longer game: two human+AI pairs, then undo pops only the last pair. */
    CHECK(game_play(&g, 0, 0));
    CHECK(game_play(&g, 1, 1));
    CHECK(game_play(&g, 0, 1));
    CHECK(game_play(&g, 2, 0));
    CHECK(game_request_undo(&g, 0));
    CHECK(g.move_count == 2);
    CHECK(g.board.c[0][1] == CELL_EMPTY);
    CHECK(g.board.c[2][0] == CELL_EMPTY);
    CHECK(g.board.c[1][1] == CELL_O);
    CHECK(g.turn == 0);

    /* Undo not allowed once the game is over. */
    game_surrender(&g, 0);
    CHECK(!game_can_request_undo(&g, 0));
}

static void test_surrender(void)
{
    Game g;
    new_pvp(&g, 3);
    CHECK(game_play(&g, 0, 0));
    game_surrender(&g, 0);
    CHECK(g.status == GS_OVER);
    CHECK(g.end_reason == END_SURRENDER);
    CHECK(g.winner == 1);
    CHECK(!game_play(&g, 1, 1));
    CHECK(!game_request_undo(&g, 0));

    Game h;
    new_pvp(&h, 3);
    game_surrender(&h, 1);
    CHECK(h.winner == 0);
    CHECK(h.end_reason == END_SURRENDER);

    /* Surrendering after game over changes nothing. */
    game_surrender(&h, 0);
    CHECK(h.winner == 0);
    CHECK(h.end_reason == END_SURRENDER);
}

static void test_ai_turn(void)
{
    Game g;
    new_pve(&g, 3);
    CHECK(!game_is_ai_turn(&g));
    CHECK(game_play(&g, 0, 0));
    CHECK(game_is_ai_turn(&g));
    CHECK(game_play(&g, 1, 1));
    CHECK(!game_is_ai_turn(&g));

    Game p;
    new_pvp(&p, 3);
    CHECK(!game_is_ai_turn(&p));
}

int main(void)
{
    test_new_defaults();
    test_alternate_turns();
    test_illegal_moves();
    test_win_3x3();
    test_draw_3x3();
    test_clocks();
    test_afk();
    test_play_blocked_by_afk();
    CHECK(near(game_afk_idle_s, 120.0));
    CHECK(near(game_afk_grace_s, 30.0));
    test_pvp_undo();
    test_pve_undo();
    test_surrender();
    test_ai_turn();

    /* computer champions per difficulty */
    CHECK(game_champion_avatar(DIFF_EASY) == 6);
    CHECK(game_champion_avatar(DIFF_MEDIUM) == 5);
    CHECK(game_champion_avatar(DIFF_HARD) == 7);
    CHECK(game_champion_avatar(DIFF_HARD) < AVATAR_TOTAL);

    if (failures) {
        fprintf(stderr, "test_game: %d check(s) failed\n", failures);
        return 1;
    }
    puts("test_game: all passed");
    return 0;
}
