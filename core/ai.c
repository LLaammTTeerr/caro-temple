/* STUB - replaced by unit "AI". */
#include "ai.h"
Pos ai_choose_move(const Board *b, Cell me, Difficulty d, unsigned *rng)
{
    (void)me; (void)d; (void)rng;
    for (int r = 0; r < b->n; r++) for (int c = 0; c < b->n; c++) if (!b->c[r][c]) return (Pos){r, c};
    return (Pos){-1, -1};
}
