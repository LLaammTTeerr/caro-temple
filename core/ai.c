/* ai.c - computer opponent: Easy (noisy), Medium (1-ply pattern), Hard (alpha-beta). */
#include <stdbool.h>
#include <time.h>
#include "ai.h"

#define WIN_SCORE 100000000
#define INF_SCORE 200000000
#define TOP_K 12                 /* candidates kept per search node */
#define MAX_DEPTH 5              /* iterative-deepening cap on boards larger than 3x3 */
#define HARD_BUDGET_SEC 0.25     /* CPU-time budget for one Hard move on large boards */
#define NCELLS (CARO_MAX_N * CARO_MAX_N)

static const int DIRS[4][2] = { {0, 1}, {1, 0}, {1, 1}, {1, -1} };
/* Pattern value of a window holding k own stones and no opponent stones. */
static const int SCORE3[4] = { 0, 1, 100, 50000 };
static const int SCORE5[6] = { 0, 1, 10, 100, 1000, 50000 };

typedef struct {
    Cell root;          /* side the search maximises for */
    clock_t start;
    bool limited;       /* false on 3x3: exhaustive, no time budget */
    bool abort;         /* budget exceeded; the current iteration is discarded */
    long nodes;
} Ctx;

static unsigned xs32(unsigned *s)
{
    unsigned x = *s ? *s : 1u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static int rnd_below(unsigned *s, int n)
{
    return (int)(xs32(s) % (unsigned)n);
}

static int wscore(int win_len, int k)
{
    return win_len == 3 ? SCORE3[k] : SCORE5[k];
}

/* Attack value of placing `who` at empty (r,c): sum of pattern values over all
 * win_len-windows through (r,c) that contain no opponent stone. */
static int line_value(const Board *w, int r, int c, Cell who)
{
    int L = w->win_len, total = 0;
    Cell opp = cell_opponent(who);
    for (int d = 0; d < 4; d++) {
        int dr = DIRS[d][0], dc = DIRS[d][1];
        for (int off = 0; off < L; off++) {
            int sr = r - off * dr, sc = c - off * dc;
            int er = sr + (L - 1) * dr, ec = sc + (L - 1) * dc;
            if (!board_in(w, sr, sc) || !board_in(w, er, ec)) continue;
            int k = 1;              /* the placed stone */
            bool blocked = false;
            for (int i = 0; i < L && !blocked; i++) {
                int rr = sr + i * dr, cc = sc + i * dc;
                if (rr == r && cc == c) continue;
                if (w->c[rr][cc] == who) k++;
                else if (w->c[rr][cc] == opp) blocked = true;
            }
            if (!blocked) total += wscore(L, k);
        }
    }
    return total;
}

/* Combined move value: own attack plus slightly discounted defence. */
static int cell_score(const Board *w, int r, int c, Cell side)
{
    return line_value(w, r, c, side) + line_value(w, r, c, cell_opponent(side)) * 9 / 10;
}

/* Static evaluation from `root`'s point of view over all windows on the board. */
static int eval_board(const Board *w, Cell root)
{
    int L = w->win_len, total = 0;
    Cell opp = cell_opponent(root);
    for (int r = 0; r < w->n; r++) {
        for (int c = 0; c < w->n; c++) {
            for (int d = 0; d < 4; d++) {
                int dr = DIRS[d][0], dc = DIRS[d][1];
                if (!board_in(w, r + (L - 1) * dr, c + (L - 1) * dc)) continue;
                int mine = 0, theirs = 0;
                for (int i = 0; i < L; i++) {
                    Cell x = w->c[r + i * dr][c + i * dc];
                    if (x == root) mine++;
                    else if (x == opp) theirs++;
                }
                if (theirs == 0) total += wscore(L, mine);
                else if (mine == 0) total -= wscore(L, theirs) * 11 / 10;
            }
        }
    }
    return total;
}

/* Empty cells within Chebyshev distance `dist` of a stone; the centre if the board is empty.
 * Returns the count written to out (at most NCELLS). */
static int gen_cands(const Board *w, int dist, Pos *out)
{
    int n = w->n, cnt = 0, stones = 0;
    for (int r = 0; r < n; r++)
        for (int c = 0; c < n; c++)
            if (w->c[r][c] != CELL_EMPTY) stones++;
    if (stones == 0) {
        out[0] = (Pos){ n / 2, n / 2 };
        return 1;
    }
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) {
            if (w->c[r][c] != CELL_EMPTY) continue;
            bool near = false;
            for (int dr = -dist; dr <= dist && !near; dr++)
                for (int dc = -dist; dc <= dist && !near; dc++)
                    if (board_get(w, r + dr, c + dc) != CELL_EMPTY) near = true;
            if (near) out[cnt++] = (Pos){ r, c };
        }
    }
    return cnt;
}

/* True if `who` placed at empty (r,c) completes a win. Board is restored. */
static bool wins_at(Board *w, int r, int c, Cell who)
{
    if (!board_place(w, r, c, who)) return false;
    bool ok = rules_check_win_at(w, r, c, NULL);
    board_clear_cell(w, r, c);
    return ok;
}

/* Collect the cells of list[] where `who` wins immediately. Returns the count. */
static int win_cells(Board *w, Cell who, const Pos *list, int n, Pos *out)
{
    int k = 0;
    for (int i = 0; i < n; i++)
        if (wins_at(w, list[i].r, list[i].c, who)) out[k++] = list[i];
    return k;
}

/* Best cell of a non-empty list by cell_score for `side`, random among equal best. */
static Pos best_by_score(const Board *w, Cell side, const Pos *list, int n, unsigned *rng)
{
    int best = -INF_SCORE, nt = 0;
    Pos ties[NCELLS];
    for (int i = 0; i < n; i++) {
        int s = cell_score(w, list[i].r, list[i].c, side);
        if (s > best) { best = s; nt = 0; }
        if (s == best) ties[nt++] = list[i];
    }
    return ties[rnd_below(rng, nt)];
}

/* Top TOP_K candidates for `side`, best first (partial selection sort). */
static int gen_ordered(const Board *w, Cell side, Pos *out)
{
    Pos cand[NCELLS];
    int sc[NCELLS];
    int n = gen_cands(w, 2, cand);
    for (int i = 0; i < n; i++) sc[i] = cell_score(w, cand[i].r, cand[i].c, side);
    int m = n < TOP_K ? n : TOP_K;
    for (int k = 0; k < m; k++) {
        int bi = k;
        for (int j = k + 1; j < n; j++)
            if (sc[j] > sc[bi]) bi = j;
        Pos tp = cand[k]; cand[k] = cand[bi]; cand[bi] = tp;
        int ts = sc[k]; sc[k] = sc[bi]; sc[bi] = ts;
        out[k] = cand[k];
    }
    return m;
}

/* Negamax-style alpha-beta. `side` is to move; values are from cx->root's view. */
static int ab(Board *w, Cell side, int depth, int alpha, int beta, int ply, Ctx *cx)
{
    if (cx->abort) return 0;
    if (cx->limited && (++cx->nodes & 63) == 0 &&
        (double)(clock() - cx->start) / CLOCKS_PER_SEC > HARD_BUDGET_SEC) {
        cx->abort = true;
        return 0;
    }
    if (depth == 0) return eval_board(w, cx->root);

    Pos moves[TOP_K];
    int n = gen_ordered(w, side, moves);
    if (n == 0) return 0;

    bool maxing = side == cx->root;
    int best = maxing ? -INF_SCORE : INF_SCORE;
    for (int i = 0; i < n; i++) {
        int r = moves[i].r, c = moves[i].c;
        board_place(w, r, c, side);
        int v;
        if (rules_check_win_at(w, r, c, NULL))
            v = maxing ? WIN_SCORE - ply : -(WIN_SCORE - ply);
        else
            v = ab(w, cell_opponent(side), depth - 1, alpha, beta, ply + 1, cx);
        board_clear_cell(w, r, c);
        if (cx->abort) return 0;
        if (maxing) {
            if (v > best) best = v;
            if (best > alpha) alpha = best;
        } else {
            if (v < best) best = v;
            if (best < beta) beta = best;
        }
        if (beta <= alpha) break;
    }
    return best;
}

/* Iterative deepening over the root candidates. Keeps the last completed iteration. */
static Pos search_root(Board *w, Cell me, bool limited, int max_depth, unsigned *rng)
{
    Pos root_moves[TOP_K];
    int n = gen_ordered(w, me, root_moves);
    Pos best = root_moves[0];
    int first = limited ? 2 : max_depth;
    clock_t start = clock();

    for (int d = first; d <= max_depth; d++) {
        Ctx cx = { me, start, limited, false, 0 };
        int best_v = -INF_SCORE, nt = 0;
        Pos ties[TOP_K];
        for (int i = 0; i < n; i++) {
            int r = root_moves[i].r, c = root_moves[i].c;
            /* alpha = best-1 keeps values equal to best exact (ties stay ties) */
            int alpha = best_v == -INF_SCORE ? -INF_SCORE : best_v - 1;
            board_place(w, r, c, me);
            int v = rules_check_win_at(w, r, c, NULL)
                        ? WIN_SCORE
                        : ab(w, cell_opponent(me), d - 1, alpha, INF_SCORE, 1, &cx);
            board_clear_cell(w, r, c);
            if (cx.abort) break;
            if (v > best_v) { best_v = v; nt = 0; }
            if (v == best_v) ties[nt++] = root_moves[i];
        }
        if (cx.abort) break;
        best = ties[rnd_below(rng, nt)];
        if (best_v >= WIN_SCORE / 2 || best_v <= -WIN_SCORE / 2) break; /* forced result */
    }
    return best;
}

static Pos easy_move(Board *w, Cell me, unsigned *rng)
{
    Cell opp = cell_opponent(me);
    Pos cand[NCELLS], wins[NCELLS], blocks[NCELLS], near[NCELLS];
    int n = gen_cands(w, 2, cand);
    int nw = win_cells(w, me, cand, n, wins);
    int nb = win_cells(w, opp, cand, n, blocks);
    if (nw && rnd_below(rng, 100) < 50) return wins[rnd_below(rng, nw)];
    if (nb && rnd_below(rng, 100) < 50) return blocks[rnd_below(rng, nb)];
    int nn = gen_cands(w, 1, near);
    return near[rnd_below(rng, nn)];
}

static Pos medium_move(Board *w, Cell me, unsigned *rng)
{
    Cell opp = cell_opponent(me);
    Pos cand[NCELLS], wins[NCELLS];
    int n = gen_cands(w, 2, cand);
    int nw = win_cells(w, me, cand, n, wins);
    if (nw) return best_by_score(w, me, wins, nw, rng);
    int nb = win_cells(w, opp, cand, n, wins);
    if (nb) return best_by_score(w, me, wins, nb, rng);
    return best_by_score(w, me, cand, n, rng);
}

static Pos hard_move(Board *w, Cell me, unsigned *rng)
{
    Cell opp = cell_opponent(me);
    Pos cand[NCELLS], list[NCELLS], ext[NCELLS];
    int n = gen_cands(w, 2, cand);

    /* 1. immediate win */
    int k = win_cells(w, me, cand, n, list);
    if (k) return best_by_score(w, me, list, k, rng);

    /* 2. block immediate loss */
    k = win_cells(w, opp, cand, n, list);
    if (k) return best_by_score(w, me, list, k, rng);

    /* 3. own double threat: a move leaving two distinct winning cells */
    int nd = 0;
    for (int i = 0; i < n; i++) {
        board_place(w, cand[i].r, cand[i].c, me);
        int ne = gen_cands(w, 1, ext);
        Pos tmp[NCELLS];
        int kk = win_cells(w, me, ext, ne, tmp);
        board_clear_cell(w, cand[i].r, cand[i].c);
        if (kk >= 2) list[nd++] = cand[i];
    }
    if (nd) return best_by_score(w, me, list, nd, rng);

    /* 4. block an opponent double-threat move (their next move would give two wins).
     * Not forced when a counter-four exists, so it is skipped on 3x3 (exact search there)
     * and only applied when it is the single such cell on larger boards. */
    bool full = w->win_len == 3;     /* 3x3: exhaustive */
    if (!full) {
        int nm = 0;
        for (int i = 0; i < n; i++) {
            board_place(w, cand[i].r, cand[i].c, opp);
            int ne = gen_cands(w, 1, ext);
            Pos tmp[NCELLS];
            int kk = win_cells(w, opp, ext, ne, tmp);
            board_clear_cell(w, cand[i].r, cand[i].c);
            if (kk >= 2) list[nm++] = cand[i];
        }
        if (nm == 1) return list[0];
    }

    /* 5. alpha-beta */
    int empties = 0;
    for (int r = 0; r < w->n; r++)
        for (int c = 0; c < w->n; c++)
            if (w->c[r][c] == CELL_EMPTY) empties++;
    if (full) return search_root(w, me, false, empties, rng);
    return search_root(w, me, true, MAX_DEPTH, rng);
}

Pos ai_choose_move(const Board *b, Cell me, Difficulty d, unsigned *rng)
{
    static unsigned fallback_rng = 1u;
    Pos none = { -1, -1 };
    if (!rng) rng = &fallback_rng;
    if (me != CELL_X && me != CELL_O) me = CELL_X;
    if (board_full(b)) return none;

    Board w = *b;   /* local copy: the search mutates it and restores it */
    switch (d) {
    case DIFF_EASY:   return easy_move(&w, me, rng);
    case DIFF_HARD:   return hard_move(&w, me, rng);
    case DIFF_MEDIUM:
    default:          return medium_move(&w, me, rng);
    }
}
