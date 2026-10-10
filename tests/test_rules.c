/* test_rules.c - win detection tests. */
#include <stdio.h>
#include <stdlib.h>
#include "../core/rules.h"

static int fails;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

static void put(Board *b, const char *rows[])
{
    for (int r = 0; r < b->n; r++)
        for (int c = 0; c < b->n; c++)
            b->c[r][c] = rows[r][c] == 'X' ? CELL_X : rows[r][c] == 'O' ? CELL_O : CELL_EMPTY;
}

int main(void)
{
    Board b;
    WinLine w;

    board_init(&b, 3, false);
    CHECK(b.win_len == 3);
    CHECK(board_place(&b, 0, 0, CELL_X));
    CHECK(!board_place(&b, 0, 0, CELL_O));
    CHECK(!board_place(&b, 3, 0, CELL_O));
    CHECK(!board_place(&b, 1, 1, CELL_EMPTY));
    board_place(&b, 1, 1, CELL_X);
    CHECK(!rules_find_win(&b, &w));
    board_place(&b, 2, 2, CELL_X);
    CHECK(rules_check_win_at(&b, 1, 1, &w));
    CHECK(w.found && w.who == CELL_X && w.len == 3);
    CHECK(w.cells[0].r == 0 && w.cells[0].c == 0 && w.cells[2].r == 2 && w.cells[2].c == 2);

    /* anti-diagonal on 3x3 */
    board_init(&b, 3, false);
    { const char *g[] = { "..O", ".O.", "O.." }; put(&b, g); }
    CHECK(rules_find_win(&b, &w) && w.who == CELL_O);

    /* 5x5 needs five; four is not enough */
    board_init(&b, 5, false);
    CHECK(b.win_len == 5);
    { const char *g[] = { "XXXX.", ".....", ".....", ".....", "....." }; put(&b, g); }
    CHECK(!rules_find_win(&b, NULL));
    b.c[0][4] = CELL_X;
    CHECK(rules_check_win_at(&b, 0, 2, &w) && w.len == 5);

    /* 10x10 vertical, overline of 6 still wins */
    board_init(&b, 10, false);
    for (int r = 2; r < 8; r++) b.c[r][3] = CELL_O;
    CHECK(rules_check_win_at(&b, 4, 3, &w) && w.len == 6 && w.who == CELL_O);

    /* block rule: both ends blocked by opponent -> no win; edge does not count */
    board_init(&b, 10, true);
    b.c[0][0] = CELL_O;
    for (int c = 1; c <= 5; c++) b.c[0][c] = CELL_X;
    b.c[0][6] = CELL_O;
    CHECK(!rules_check_win_at(&b, 0, 3, NULL));
    b.c[0][6] = CELL_EMPTY;
    CHECK(rules_check_win_at(&b, 0, 3, NULL));
    board_init(&b, 10, true);
    for (int c = 0; c < 5; c++) b.c[5][c] = CELL_X;  /* touches left edge */
    b.c[5][5] = CELL_O;
    CHECK(rules_check_win_at(&b, 5, 2, NULL));
    /* block rule does not apply on 3x3 */
    board_init(&b, 3, true);
    { const char *g[] = { "XXX", "...", "..." }; put(&b, g); }
    CHECK(rules_find_win(&b, NULL));

    /* full / count */
    board_init(&b, 3, false);
    { const char *g[] = { "XOX", "XOO", "OXX" }; put(&b, g); }
    CHECK(board_full(&b) && board_count(&b) == 9 && !rules_find_win(&b, NULL));
    CHECK(cell_opponent(CELL_X) == CELL_O && cell_opponent(CELL_EMPTY) == CELL_EMPTY);

    if (fails) { printf("test_rules: %d failure(s)\n", fails); return 1; }
    puts("test_rules: all passed");
    return 0;
}
