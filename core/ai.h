/* ai.h - computer opponent. Pure C99, deterministic given rng state. */
#ifndef CARO_AI_H
#define CARO_AI_H

#include "rules.h"

typedef enum { DIFF_EASY = 0, DIFF_MEDIUM, DIFF_HARD, DIFF_COUNT } Difficulty;

/* Choose a move for `me` on board `b` (board is not modified).
 * Always returns a legal empty cell if one exists, otherwise {-1,-1}.
 * - EASY:   mostly random cell adjacent to existing stones; takes an immediate win ~50%,
 *           blocks an immediate loss ~50%. First move: near centre.
 * - MEDIUM: always wins/blocks immediate; otherwise 1-ply pattern scoring (attack + defence).
 * - HARD:   alpha-beta (iterative deepening, candidate cells within 2 of stones, top-K by
 *           heuristic), perfect play on 3x3, must return within ~300 ms on 10x10.
 * rng: xorshift state owned by caller (non-zero); used for tie-breaking/randomness. */
Pos ai_choose_move(const Board *b, Cell me, Difficulty d, unsigned *rng);

#endif
