/* rules.h - Caro board representation and win detection. Pure C99, no I/O. */
#ifndef CARO_RULES_H
#define CARO_RULES_H

#include <stdbool.h>

#define CARO_MAX_N 10

typedef enum { CELL_EMPTY = 0, CELL_X = 1, CELL_O = 2 } Cell; /* X = fire, O = water */

typedef struct { int r, c; } Pos;

typedef struct {
    int n;           /* board is n x n, 3 <= n <= CARO_MAX_N */
    int win_len;     /* 3 when n == 3, otherwise 5 */
    bool block_rule; /* if true (and win_len == 5): a run whose BOTH ends are blocked by
                        opponent stones does not win. Board edges do NOT count as blocks.
                        Runs longer than win_len (overlines) still win. */
    Cell c[CARO_MAX_N][CARO_MAX_N]; /* c[row][col] */
} Board;

typedef struct {
    bool found;
    Cell who;
    int len;                 /* number of cells in the winning run (>= win_len) */
    Pos cells[CARO_MAX_N];   /* cells of the run, in order from one end to the other */
} WinLine;

/* Initialise an empty n x n board. win_len derived from n. Clamps n to [3, CARO_MAX_N]. */
void board_init(Board *b, int n, bool block_rule);
bool board_in(const Board *b, int r, int c);
Cell board_get(const Board *b, int r, int c);           /* CELL_EMPTY when out of range */
bool board_place(Board *b, int r, int c, Cell who);      /* false if out of range / occupied / who empty */
void board_clear_cell(Board *b, int r, int c);
int  board_count(const Board *b);                        /* number of stones on board */
bool board_full(const Board *b);
Cell cell_opponent(Cell who);                            /* X<->O, EMPTY->EMPTY */

/* Check the 4 lines through (r,c) for a win by the stone at (r,c).
 * Fills *out (if non-NULL) with the winning run. */
bool rules_check_win_at(const Board *b, int r, int c, WinLine *out);
/* Scan the whole board for any winning run. */
bool rules_find_win(const Board *b, WinLine *out);

#endif
