/* test_ai.c - unit tests for ai_choose_move. Returns non-zero on failure. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "ai.h"

static int g_checks = 0, g_fails = 0;
#define CHECK(cond) do { \
        g_checks++; \
        if (!(cond)) { g_fails++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static unsigned g_seed = 12345u;

static unsigned trand(void)
{
    unsigned x = g_seed;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g_seed = x;
    return x;
}

static void put(Board *b, int r, int c, Cell who)
{
    CHECK(board_place(b, r, c, who));
}

/* Random position with `stones` stones on an n x n board. */
static void random_position(Board *b, int n, bool block, int stones)
{
    int cells[CARO_MAX_N * CARO_MAX_N];
    board_init(b, n, block);
    for (int i = 0; i < n * n; i++) cells[i] = i;
    for (int i = n * n - 1; i > 0; i--) {
        int j = (int)(trand() % (unsigned)(i + 1));
        int t = cells[i]; cells[i] = cells[j]; cells[j] = t;
    }
    for (int i = 0; i < stones; i++)
        put(b, cells[i] / n, cells[i] % n, (trand() & 1u) ? CELL_X : CELL_O);
}

static bool is_legal(const Board *b, Pos p)
{
    return board_in(b, p.r, p.c) && board_get(b, p.r, p.c) == CELL_EMPTY;
}

/* Every difficulty returns a legal empty cell, leaves the input untouched, and
 * returns {-1,-1} on a full board. */
static void test_legal_moves(void)
{
    unsigned rng = 777u;
    static const int sizes[3] = { 3, 5, 10 };
    for (int s = 0; s < 3; s++) {
        int n = sizes[s];
        int trials = n == 10 ? 40 : 300;
        for (int t = 0; t < trials; t++) {
            Board b, copy;
            int stones = (int)(trand() % (unsigned)(n * n - 1));
            random_position(&b, n, (t & 1) != 0, stones);
            copy = b;
            Difficulty ds[2] = { DIFF_EASY, DIFF_MEDIUM };
            for (int k = 0; k < 2; k++) {
                Pos p = ai_choose_move(&b, (t & 2) ? CELL_O : CELL_X, ds[k], &rng);
                CHECK(is_legal(&b, p));
            }
            if (n != 10 || t < 8) {
                Pos p = ai_choose_move(&b, (t & 2) ? CELL_O : CELL_X, DIFF_HARD, &rng);
                CHECK(is_legal(&b, p));
            }
            CHECK(memcmp(&b, &copy, sizeof b) == 0);
        }
        /* full board */
        Board full;
        random_position(&full, n, false, n * n);
        for (int d = 0; d < DIFF_COUNT; d++) {
            Pos p = ai_choose_move(&full, CELL_X, (Difficulty)d, &rng);
            CHECK(p.r == -1 && p.c == -1);
        }
    }
    /* empty board: centre-ish move */
    Board e;
    board_init(&e, 10, false);
    Pos p = ai_choose_move(&e, CELL_X, DIFF_MEDIUM, &rng);
    CHECK(p.r == 5 && p.c == 5);
}

/* Immediate win and immediate block for Medium and Hard, on 3x3 and 10x10. */
static void test_win_and_block(void)
{
    unsigned rng = 99u;
    Difficulty ds[2] = { DIFF_MEDIUM, DIFF_HARD };
    for (int k = 0; k < 2; k++) {
        Board b;
        Pos p;

        /* 3x3: X X _ / O _ _ / _ _ O ; X to move wins at (0,2) */
        board_init(&b, 3, false);
        put(&b, 0, 0, CELL_X); put(&b, 0, 1, CELL_X);
        put(&b, 1, 0, CELL_O); put(&b, 2, 2, CELL_O);
        p = ai_choose_move(&b, CELL_X, ds[k], &rng);
        CHECK(p.r == 0 && p.c == 2);

        /* 3x3: same stones, O to move must block (0,2) */
        p = ai_choose_move(&b, CELL_O, ds[k], &rng);
        CHECK(p.r == 0 && p.c == 2);

        /* 10x10: X has four at row 3 cols 1..4, O blocks col 0; win/block cell is (3,5) */
        board_init(&b, 10, false);
        put(&b, 3, 0, CELL_O);
        put(&b, 3, 1, CELL_X); put(&b, 3, 2, CELL_X);
        put(&b, 3, 3, CELL_X); put(&b, 3, 4, CELL_X);
        put(&b, 8, 8, CELL_O);
        p = ai_choose_move(&b, CELL_X, ds[k], &rng);
        CHECK(p.r == 3 && p.c == 5);
        p = ai_choose_move(&b, CELL_O, ds[k], &rng);
        CHECK(p.r == 3 && p.c == 5);

        /* 10x10: X has a diagonal four, O must block the single open end */
        board_init(&b, 10, false);
        put(&b, 1, 1, CELL_X); put(&b, 2, 2, CELL_X);
        put(&b, 3, 3, CELL_X); put(&b, 4, 4, CELL_X);
        put(&b, 0, 0, CELL_O); put(&b, 7, 2, CELL_O);
        p = ai_choose_move(&b, CELL_O, ds[k], &rng);
        CHECK(p.r == 5 && p.c == 5);
    }
}

/* Exhaustive game-tree check on 3x3: Hard never loses against any opponent line. */
static long g_leaves, g_draws, g_hard_losses;

static void exhaust(Board *b, Cell hard, Cell turn, unsigned *rng)
{
    if (rules_find_win(b, NULL)) {
        WinLine wl;
        rules_find_win(b, &wl);
        g_leaves++;
        if (wl.who != hard) g_hard_losses++;
        return;
    }
    if (board_full(b)) { g_leaves++; g_draws++; return; }
    if (turn == hard) {
        Pos p = ai_choose_move(b, hard, DIFF_HARD, rng);
        CHECK(is_legal(b, p));
        if (!is_legal(b, p)) return;
        board_place(b, p.r, p.c, hard);
        exhaust(b, hard, cell_opponent(hard), rng);
        board_clear_cell(b, p.r, p.c);
    } else {
        for (int r = 0; r < b->n; r++)
            for (int c = 0; c < b->n; c++) {
                if (b->c[r][c] != CELL_EMPTY) continue;
                board_place(b, r, c, turn);
                exhaust(b, hard, cell_opponent(turn), rng);
                board_clear_cell(b, r, c);
            }
    }
}

static void test_hard_3x3_never_loses(void)
{
    unsigned rng = 4242u;
    Board b;
    g_leaves = g_draws = g_hard_losses = 0;
    board_init(&b, 3, false);
    exhaust(&b, CELL_X, CELL_X, &rng);   /* Hard as X, moves first */
    long leaves_x = g_leaves;
    CHECK(g_hard_losses == 0);
    g_leaves = g_draws = 0;
    board_init(&b, 3, false);
    exhaust(&b, CELL_O, CELL_X, &rng);   /* Hard as O, X opens */
    CHECK(g_hard_losses == 0);
    CHECK(leaves_x > 0 && g_leaves > 0);
    CHECK(g_draws > 0);
    fprintf(stderr, "  3x3 exhaustive: %ld leaves as O, Hard losses %ld\n", g_leaves, g_hard_losses);
}

/* Play one 10x10 game. Returns +1 if Hard wins, -1 if Hard loses, 0 for a draw.
 * Records the slowest Hard move in *max_hard_sec. */
static int play_game(Difficulty opp, Cell hard_side, unsigned *hrng, unsigned *orng,
                     double *max_hard_sec)
{
    Board b;
    board_init(&b, 10, false);
    Cell turn = CELL_X;
    for (int m = 0; m < 100; m++) {
        if (board_full(&b)) return 0;
        Pos p;
        if (turn == hard_side) {
            clock_t t0 = clock();
            p = ai_choose_move(&b, turn, DIFF_HARD, hrng);
            double sec = (double)(clock() - t0) / CLOCKS_PER_SEC;
            if (sec > *max_hard_sec) *max_hard_sec = sec;
        } else {
            p = ai_choose_move(&b, turn, opp, orng);
        }
        CHECK(is_legal(&b, p));
        if (!is_legal(&b, p)) return 0;
        board_place(&b, p.r, p.c, turn);
        if (rules_check_win_at(&b, p.r, p.c, NULL)) return turn == hard_side ? 1 : -1;
        turn = cell_opponent(turn);
    }
    return 0;
}

static void test_hard_vs_others(void)
{
    double max_sec = 0.0;
    int wins = 0, losses = 0;
    clock_t t0 = clock();

    /* Hard vs Easy: 20 games, alternating who starts */
    for (int i = 0; i < 20; i++) {
        unsigned hrng = 1000u + (unsigned)i * 7919u, orng = 5000u + (unsigned)i * 104729u;
        Cell hard_side = (i % 2 == 0) ? CELL_X : CELL_O;
        int r = play_game(DIFF_EASY, hard_side, &hrng, &orng, &max_sec);
        if (r > 0) wins++;
        else if (r < 0) losses++;
    }
    fprintf(stderr, "  Hard vs Easy 10x10: %d/20 wins, %d losses\n", wins, losses);
    CHECK(wins >= 18);
    CHECK(max_sec < 0.3);
    fprintf(stderr, "  slowest Hard move: %.3f s\n", max_sec);

    /* Hard vs Medium: 10 games */
    wins = losses = 0;
    for (int i = 0; i < 10; i++) {
        unsigned hrng = 3000u + (unsigned)i * 6151u, orng = 9000u + (unsigned)i * 2663u;
        Cell hard_side = (i % 2 == 0) ? CELL_X : CELL_O;
        int r = play_game(DIFF_MEDIUM, hard_side, &hrng, &orng, &max_sec);
        if (r > 0) wins++;
        else if (r < 0) losses++;
    }
    fprintf(stderr, "  Hard vs Medium 10x10: %d/10 wins, %d losses\n", wins, losses);
    CHECK(wins >= 6 && wins > losses);
    CHECK(max_sec < 0.3);
    fprintf(stderr, "  test_hard_vs_others CPU: %.2f s\n",
            (double)(clock() - t0) / CLOCKS_PER_SEC);
}

int main(void)
{
    test_legal_moves();
    test_win_and_block();
    test_hard_3x3_never_loses();
    test_hard_vs_others();

    printf("test_ai: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
