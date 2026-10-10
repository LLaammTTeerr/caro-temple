/* rules.c - Caro board representation and win detection. */
#include <string.h>
#include "rules.h"

static const int DIRS[4][2] = { {0, 1}, {1, 0}, {1, 1}, {1, -1} };

void board_init(Board *b, int n, bool block_rule)
{
    memset(b, 0, sizeof *b);
    if (n < 3) n = 3;
    if (n > CARO_MAX_N) n = CARO_MAX_N;
    b->n = n;
    b->win_len = n == 3 ? 3 : 5;
    b->block_rule = block_rule;
}

bool board_in(const Board *b, int r, int c)
{
    return r >= 0 && c >= 0 && r < b->n && c < b->n;
}

Cell board_get(const Board *b, int r, int c)
{
    return board_in(b, r, c) ? b->c[r][c] : CELL_EMPTY;
}

bool board_place(Board *b, int r, int c, Cell who)
{
    if (who != CELL_X && who != CELL_O) return false;
    if (!board_in(b, r, c) || b->c[r][c] != CELL_EMPTY) return false;
    b->c[r][c] = who;
    return true;
}

void board_clear_cell(Board *b, int r, int c)
{
    if (board_in(b, r, c)) b->c[r][c] = CELL_EMPTY;
}

int board_count(const Board *b)
{
    int k = 0;
    for (int r = 0; r < b->n; r++)
        for (int c = 0; c < b->n; c++)
            k += b->c[r][c] != CELL_EMPTY;
    return k;
}

bool board_full(const Board *b)
{
    return board_count(b) == b->n * b->n;
}

Cell cell_opponent(Cell who)
{
    return who == CELL_X ? CELL_O : who == CELL_O ? CELL_X : CELL_EMPTY;
}

bool rules_check_win_at(const Board *b, int r, int c, WinLine *out)
{
    Cell who = board_get(b, r, c);
    if (out) memset(out, 0, sizeof *out);
    if (who == CELL_EMPTY) return false;
    Cell opp = cell_opponent(who);

    for (int d = 0; d < 4; d++) {
        int dr = DIRS[d][0], dc = DIRS[d][1];
        /* walk back to the start of the run */
        int sr = r, sc = c;
        while (board_get(b, sr - dr, sc - dc) == who && board_in(b, sr - dr, sc - dc)) { sr -= dr; sc -= dc; }
        int len = 0;
        int er = sr, ec = sc;
        while (board_in(b, er, ec) && b->c[er][ec] == who) { len++; er += dr; ec += dc; }
        if (len < b->win_len) continue;
        if (b->block_rule && b->win_len == 5) {
            bool blocked_start = board_in(b, sr - dr, sc - dc) && b->c[sr - dr][sc - dc] == opp;
            bool blocked_end = board_in(b, er, ec) && b->c[er][ec] == opp;
            if (blocked_start && blocked_end) continue;
        }
        if (out) {
            out->found = true;
            out->who = who;
            out->len = len;
            for (int i = 0; i < len && i < CARO_MAX_N; i++)
                out->cells[i] = (Pos){ sr + dr * i, sc + dc * i };
        }
        return true;
    }
    return false;
}

bool rules_find_win(const Board *b, WinLine *out)
{
    for (int r = 0; r < b->n; r++)
        for (int c = 0; c < b->n; c++)
            if (b->c[r][c] != CELL_EMPTY && rules_check_win_at(b, r, c, out))
                return true;
    if (out) memset(out, 0, sizeof *out);
    return false;
}
