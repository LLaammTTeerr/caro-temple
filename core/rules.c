/* STUB - replaced by unit "Rules". */
#include <string.h>
#include "rules.h"
void board_init(Board *b, int n, bool block_rule) { memset(b, 0, sizeof *b); if (n < 3) n = 3; if (n > CARO_MAX_N) n = CARO_MAX_N; b->n = n; b->win_len = n == 3 ? 3 : 5; b->block_rule = block_rule; }
bool board_in(const Board *b, int r, int c) { return r >= 0 && c >= 0 && r < b->n && c < b->n; }
Cell board_get(const Board *b, int r, int c) { return board_in(b, r, c) ? b->c[r][c] : CELL_EMPTY; }
bool board_place(Board *b, int r, int c, Cell who) { if (!board_in(b, r, c) || b->c[r][c] || !who) return false; b->c[r][c] = who; return true; }
void board_clear_cell(Board *b, int r, int c) { if (board_in(b, r, c)) b->c[r][c] = CELL_EMPTY; }
int board_count(const Board *b) { int k = 0; for (int r = 0; r < b->n; r++) for (int c = 0; c < b->n; c++) k += b->c[r][c] != 0; return k; }
bool board_full(const Board *b) { return board_count(b) == b->n * b->n; }
Cell cell_opponent(Cell w) { return w == CELL_X ? CELL_O : w == CELL_O ? CELL_X : CELL_EMPTY; }
bool rules_check_win_at(const Board *b, int r, int c, WinLine *out) { (void)b; (void)r; (void)c; if (out) memset(out, 0, sizeof *out); return false; }
bool rules_find_win(const Board *b, WinLine *out) { (void)b; if (out) memset(out, 0, sizeof *out); return false; }
