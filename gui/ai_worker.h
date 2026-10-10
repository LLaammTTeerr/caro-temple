/* ai_worker.h - runs ai_choose_move() on a background thread so a slow search (Hard on
 * 10x10 may take up to ~250 ms) never stalls rendering. One search at a time. */
#ifndef CARO_AI_WORKER_H
#define CARO_AI_WORKER_H

#include <stdbool.h>
#include "../core/ai.h"

/* Start a search on a private copy of the board. If a search is still running it is
 * cancelled first (its result is discarded). */
void ai_worker_start(const Board *b, Cell me, Difficulty d, unsigned rng_seed);
/* True once the current search has finished; *out receives the move (once). */
bool ai_worker_poll(Pos *out);
/* Discard any running search's result. Non-blocking. */
void ai_worker_cancel(void);
/* Wait for any running thread to exit (call at shutdown). */
void ai_worker_shutdown(void);

#endif
