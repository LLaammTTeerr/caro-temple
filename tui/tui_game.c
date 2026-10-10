/* tui_game.c - GAME screen: board, HUD panels and the popups (pause, undo consent, surrender,
 * leave, AFK prompt, game over). Match state lives in g_tui.game; this file owns only visuals,
 * cursors and popup focus. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "tui.h"
#include "../core/i18n.h"

void tui_draw_mascot(int x, int y, int avatar, TColor bg, bool bob, bool flicker); /* tui_menu.c */
void tui_fx_frame(int key, int cols, int rows);  /* tui_menu.c: screen/popup wipe, call last */

enum { POP_NONE, POP_PAUSE, POP_UNDO, POP_SURRENDER, POP_LEAVE, POP_AFK, POP_OVER };
enum { PAUSE_RESUME, PAUSE_SAVE, PAUSE_UNDO, PAUSE_SETTINGS, PAUSE_SURRENDER, PAUSE_HOME };
enum { REQ_ACCEPT, REQ_DECLINE };
enum { YN_YES, YN_NO };
enum { OVER_REMATCH, OVER_HOME };

#define MAX_PITEMS 8
#define HUD_CARD_W 20
#define HUD_CARD_H 15
#define BTN_W 16
#define BAR_N 4                /* bottom bar: undo, surrender, menu, home */

typedef struct {
    int id;
    const char *label;
    bool enabled;
    int x, y, w, h;
} PItem;

static PItem pit[MAX_PITEMS];
static int npit;
static int pfocus;
static int popup = POP_NONE;
static char ptitle[64], pline1[160], pline2[160];
static int pbx, pby, pbw, pbh;
static int surrender_who;

static Pos cur[2];             /* cursor per player index (PvE uses cur[0] for the human) */
static Pos flash_cell;
static double flash_until;     /* term_now() deadline for the red invalid-move flash */
static bool ai_pending;
static double ai_wait;
static double over_timer;
static TColor g_bg = TC_BG;    /* this frame's board backdrop (depends on the board size) */
static int bar_hot = -1;       /* bottom bar button under the mouse pointer, -1 none */

static bool seen_valid;       /* identifies the match the visual state belongs to */
static int64_t seen_start;
static int seen_n;

/* ---------------------------------------------------------------- helpers */

static void put_centred(int x, int w, int y, const char *s, TColor fg, TColor bg, int attr)
{
    int n = utf8_cell_width(s);
    int sx = x + (w - n) / 2;
    if (sx < x) sx = x;
    term_print(sx, y, s, fg, bg, attr);
}

/* Copy at most maxc cells (codepoints) of s into out, never splitting a UTF-8 sequence. */
static void clip_cells(char *out, size_t sz, const char *s, int maxc)
{
    size_t o = 0;
    int n = 0;
    while (*s && n < maxc) {
        uint32_t cp;
        int k = utf8_decode(s, &cp);
        if (k <= 0 || o + (size_t)k + 1 > sz) break;
        memcpy(out + o, s, (size_t)k);
        o += (size_t)k;
        s += k;
        n++;
    }
    out[o] = '\0';
}

static void fmt_clock(char *buf, size_t sz, double secs)
{
    int s = (int)(secs + 0.999);
    if (s < 0) s = 0;
    snprintf(buf, sz, "%02d:%02d", s / 60, s % 60);
}

/* Cell of the board under screen position (mx,my); false on separators / outside. */
static bool board_geo(int *bx, int *by, int *bw, int *bh, int *cw, int *ch, int *sep)
{
    Game *g = &g_tui.game;
    int n = g->board.n;
    int cols, rows;
    term_size(&cols, &rows);
    if (n == 3) { *cw = 11; *ch = 5; *sep = 1; }
    else if (n == 5) { *cw = 6; *ch = 3; *sep = 1; }
    else { *cw = 3; *ch = 1; *sep = 0; }
    *bw = n * (*cw) + (n - 1) * (*sep);
    *bh = n * (*ch) + (n - 1) * (*sep);
    *bx = (cols - *bw) / 2;
    *by = 2 + (21 - *bh) / 2;
    if (*by < 2) *by = 2;
    return n > 0;
}

static bool cell_at(int mx, int my, Pos *out)
{
    int bx, by, bw, bh, cw, ch, sep;
    board_geo(&bx, &by, &bw, &bh, &cw, &ch, &sep);
    int dx = mx - bx, dy = my - by;
    if (dx < 0 || dy < 0 || dx >= bw || dy >= bh) return false;
    int stepx = cw + sep, stepy = ch + sep;
    int c = dx / stepx, r = dy / stepy;
    if (dx - c * stepx >= cw || dy - r * stepy >= ch) return false;
    if (r >= g_tui.game.board.n || c >= g_tui.game.board.n) return false;
    out->r = r;
    out->c = c;
    return true;
}

static bool is_pve(void) { return g_tui.game.mode == MODE_PVE; }

/* Which player's cursor and turn the key/mouse controls act for. -1 when none. */
static int human_index(int pl)
{
    if (is_pve()) return 0;
    return pl;
}

static int undo_requester(void)
{
    Game *g = &g_tui.game;
    if (is_pve()) return 0;
    return g->move_count > 0 ? (g->move_count - 1) % 2 : 0;
}

/* ---------------------------------------------------------------- state */

static void reset_visual(void)
{
    Game *g = &g_tui.game;
    int mid = g->board.n / 2;
    cur[0].r = cur[0].c = mid;
    cur[1] = cur[0];
    flash_until = 0;
    ai_pending = false;
    over_timer = 0;
    popup = POP_NONE;
    pfocus = 0;
}

static void track_match(void)
{
    Game *g = &g_tui.game;
    seen_valid = true;
    seen_start = g->start_time;
    seen_n = g->board.n;
}

static void open_popup(int p)
{
    popup = p;
    bar_hot = -1;
    /* destructive confirmations default to "No" so a stray Enter does nothing */
    pfocus = (p == POP_SURRENDER || p == POP_LEAVE) ? 1 : 0;
}

static void request_undo_now(void)
{
    Game *g = &g_tui.game;
    int who = undo_requester();
    if (g->status != GS_PLAYING || !game_can_request_undo(g, who)) {
        tui_toast(tr(S_UNDO_NOT_ALLOWED));
        return;
    }
    if (is_pve()) {
        if (game_request_undo(g, who)) tui_toast(tr(S_UNDO_DONE));
        else tui_toast(tr(S_UNDO_NOT_ALLOWED));
    } else {
        game_request_undo(g, who);   /* opponent gets the consent popup on the next update */
    }
}

static void open_surrender(void)
{
    Game *g = &g_tui.game;
    if (g->status != GS_PLAYING) return;
    surrender_who = is_pve() ? 0 : g->turn;
    open_popup(POP_SURRENDER);
}

static void rematch(void)
{
    Game *g = &g_tui.game;
    PlayerInfo p[2];
    GameMode mode = g->mode;
    Difficulty diff = g->diff;
    int n = g->board.n;
    bool block = g->board.block_rule;
    p[0] = g->players[0];
    p[1] = g->players[1];
    game_new(g, mode, diff, n, block, p);
    g_tui.has_game = true;
    reset_visual();
    track_match();
}

static void place_at(int idx, Pos p)
{
    Game *g = &g_tui.game;
    if (g->status != GS_PLAYING || g->turn != idx) return;
    if (!game_play(g, p.r, p.c)) {
        flash_cell = p;
        flash_until = term_now() + 0.3;
    }
}

/* ---------------------------------------------------------------- popups */

static void padd(int id, const char *label, bool enabled)
{
    if (npit >= MAX_PITEMS) return;
    pit[npit].id = id;
    pit[npit].label = label;
    pit[npit].enabled = enabled;
    npit++;
}

/* Lays out the popup for the current popup type; both drawing and hit-testing use this. */
static void popup_build(int cols, int rows)
{
    Game *g = &g_tui.game;
    bool vertical = false;
    int nlines = 0;
    npit = 0;
    ptitle[0] = pline1[0] = pline2[0] = '\0';
    switch (popup) {
    case POP_PAUSE:
        snprintf(ptitle, sizeof ptitle, "%s", tr(S_PAUSED));
        padd(PAUSE_RESUME, tr(S_RESUME), true);
        padd(PAUSE_SAVE, tr(S_SAVE_GAME), true);
        padd(PAUSE_UNDO, tr(S_UNDO), g->status == GS_PLAYING && game_can_request_undo(g, undo_requester()));
        padd(PAUSE_SETTINGS, tr(S_SETTINGS), true);
        padd(PAUSE_SURRENDER, tr(S_SURRENDER), g->status == GS_PLAYING);
        padd(PAUSE_HOME, tr(S_HOME), true);
        vertical = true;
        break;
    case POP_UNDO:
        snprintf(ptitle, sizeof ptitle, "%s", tr(S_UNDO));
        snprintf(pline1, sizeof pline1, tr(S_UNDO_REQUEST_FMT),
                 g->players[g->undo_requester >= 0 ? g->undo_requester : undo_requester()].name);
        nlines = 1;
        padd(REQ_ACCEPT, tr(S_ACCEPT), true);
        padd(REQ_DECLINE, tr(S_DECLINE), true);
        break;
    case POP_SURRENDER:
        snprintf(ptitle, sizeof ptitle, "%s", tr(S_SURRENDER));
        snprintf(pline1, sizeof pline1, tr(S_SURRENDER_Q_FMT), g->players[surrender_who].name);
        nlines = 1;
        padd(YN_YES, tr(S_YES), true);
        padd(YN_NO, tr(S_NO), true);
        break;
    case POP_LEAVE:
        snprintf(ptitle, sizeof ptitle, "%s", tr(S_HOME));
        snprintf(pline1, sizeof pline1, "%s", tr(S_LEAVE_GAME_Q));
        nlines = 1;
        padd(YN_YES, tr(S_YES), true);
        padd(YN_NO, tr(S_NO), true);
        break;
    case POP_AFK: {
        snprintf(ptitle, sizeof ptitle, "%s", tr(S_AFK_TITLE));
        snprintf(pline1, sizeof pline1, "%s", tr(S_AFK_Q));
        int secs = (int)(g->afk_left + 0.999);
        if (secs < 0) secs = 0;
        snprintf(pline2, sizeof pline2, tr(S_AFK_COUNTDOWN_FMT), secs);
        nlines = 2;
        padd(YN_YES, tr(S_YES), true);
        break;
    }
    case POP_OVER: {
        snprintf(ptitle, sizeof ptitle, "%s", tr(S_GAME_OVER));
        if (g->end_reason == END_DRAW || g->winner < 0) {
            snprintf(pline1, sizeof pline1, "%s", tr(S_DRAW));
        } else if (is_pve()) {
            snprintf(pline1, sizeof pline1, "%s", tr(g->winner == 0 ? S_YOU_WIN : S_YOU_LOSE));
        } else {
            snprintf(pline1, sizeof pline1, tr(S_WINS_FMT), g->players[g->winner].name);
        }
        StrId reason = S_REASON_DRAW;
        if (g->end_reason == END_LINE) reason = S_REASON_LINE;
        else if (g->end_reason == END_TIMEOUT) reason = S_REASON_TIMEOUT;
        else if (g->end_reason == END_SURRENDER) reason = S_REASON_SURRENDER;
        snprintf(pline2, sizeof pline2, "%s", tr(reason));
        nlines = 2;
        padd(OVER_REMATCH, tr(S_REMATCH), true);
        padd(OVER_HOME, tr(S_HOME), true);
        break;
    }
    default:
        return;
    }
    int lines_w = utf8_cell_width(ptitle);
    if (utf8_cell_width(pline1) > lines_w) lines_w = utf8_cell_width(pline1);
    if (utf8_cell_width(pline2) > lines_w) lines_w = utf8_cell_width(pline2);
    int items_w = vertical ? 24 : npit * 16 + (npit - 1) * 2;
    int w = (lines_w > items_w ? lines_w : items_w) + 8;
    if (w < 40) w = 40;
    if (w > cols - 2) w = cols - 2;
    int item_rows = vertical ? npit : 1;
    int h = 3 + nlines + (nlines ? 1 : 0) + item_rows + 2;
    pbx = (cols - w) / 2;
    pby = (rows - h) / 2;
    if (pby < 0) pby = 0;
    pbw = w;
    pbh = h;
    int ys = pby + 3 + nlines + (nlines ? 1 : 0);
    for (int i = 0; i < npit; i++) {
        if (vertical) {
            pit[i].x = pbx + (w - 24) / 2; pit[i].y = ys + i; pit[i].w = 24; pit[i].h = 1;
        } else {
            pit[i].x = pbx + (w - items_w) / 2 + i * 18; pit[i].y = ys; pit[i].w = 16; pit[i].h = 1;
        }
    }
    if (pfocus < 0 || pfocus >= npit || !pit[pfocus].enabled) {
        pfocus = 0;
        for (int i = 0; i < npit; i++) if (pit[i].enabled) { pfocus = i; break; }
    }
}

static void popup_activate(int idx)
{
    Game *g = &g_tui.game;
    if (idx < 0 || idx >= npit || !pit[idx].enabled) return;
    int id = pit[idx].id;
    switch (popup) {
    case POP_PAUSE:
        switch (id) {
        case PAUSE_RESUME: popup = POP_NONE; break;
        case PAUSE_SAVE:
            popup = POP_NONE;
            g_tui.saves_mode = TSAVES_SAVE;
            tui_goto(TS_SAVES);
            break;
        case PAUSE_UNDO: popup = POP_NONE; request_undo_now(); break;
        case PAUSE_SETTINGS: popup = POP_NONE; tui_goto(TS_SETTINGS); break;
        case PAUSE_SURRENDER: open_surrender(); break;
        case PAUSE_HOME: popup = POP_NONE; tui_goto(TS_HOME); break;
        default: break;
        }
        break;
    case POP_UNDO:
        popup = POP_NONE;
        if (id == REQ_ACCEPT) {
            game_answer_undo(g, true);
            tui_toast(tr(S_UNDO_DONE));
        } else {
            game_answer_undo(g, false);
            tui_toast(tr(S_UNDO_DECLINED));
        }
        break;
    case POP_SURRENDER:
        popup = POP_NONE;
        if (id == YN_YES) game_surrender(g, surrender_who);
        break;
    case POP_LEAVE:
        popup = POP_NONE;
        if (id == YN_YES) tui_goto(TS_HOME);   /* the match stays in memory for Continue */
        break;
    case POP_AFK:
        if (id == YN_YES) {
            game_afk_continue(g);
            popup = POP_NONE;
        }
        break;
    case POP_OVER:
        if (id == OVER_REMATCH) rematch();
        else { g_tui.has_game = false; popup = POP_NONE; tui_goto(TS_HOME); }
        break;
    default:
        break;
    }
}

static void popup_cancel(void)
{
    Game *g = &g_tui.game;
    switch (popup) {
    case POP_PAUSE: case POP_SURRENDER: case POP_LEAVE:
        popup = POP_NONE;
        break;
    case POP_UNDO:
        popup = POP_NONE;
        game_answer_undo(g, false);
        tui_toast(tr(S_UNDO_DECLINED));
        break;
    case POP_OVER:
        g_tui.has_game = false;
        popup = POP_NONE;
        tui_goto(TS_HOME);
        break;
    default:   /* AFK prompt: only its Yes button continues */
        break;
    }
}

static void popup_key(const TermEvent *ev)
{
    int pl = -1;
    ActionType a = tui_key_action(ev, &pl);
    if (ev->key == TK_CHAR && (ev->ch == 'p' || ev->ch == 'P') && popup == POP_PAUSE) {
        popup = POP_NONE;
        return;
    }
    switch (a) {
    case ACT_LEFT: case ACT_UP: case ACT_RIGHT: case ACT_DOWN: {
        int dir = (a == ACT_RIGHT || a == ACT_DOWN) ? 1 : -1;
        for (int k = 1; k <= npit; k++) {
            int i = ((pfocus + dir * k) % npit + npit) % npit;
            if (pit[i].enabled) { pfocus = i; break; }
        }
        break;
    }
    case ACT_CONFIRM: popup_activate(pfocus); break;
    case ACT_BACK: popup_cancel(); break;
    default: break;
    }
}

/* Hover moves the popup focus onto the button under the pointer; a left click activates it. */
static void popup_mouse(const TermEvent *ev)
{
    bool press = ev->mkind == TM_PRESS && ev->mbutton == 0;
    if (ev->mkind != TM_MOVE && !press) return;
    for (int i = 0; i < npit; i++) {
        if (!pit[i].enabled) continue;
        if (ev->mx >= pit[i].x && ev->mx < pit[i].x + pit[i].w && ev->my == pit[i].y) {
            pfocus = i;
            if (press) popup_activate(i);
            return;
        }
    }
}

/* ---------------------------------------------------------------- input */

static void game_key(const TermEvent *ev)
{
    Game *g = &g_tui.game;
    int pl = -1;
    ActionType a = tui_key_action(ev, &pl);
    if (ev->key == TK_CHAR) {
        switch (ev->ch) {
        case 'p': case 'P': open_popup(POP_PAUSE); return;
        case 'u': case 'U': request_undo_now(); return;
        case 'g': case 'G': open_surrender(); return;
        default: break;
        }
    }
    switch (a) {
    case ACT_BACK: open_popup(POP_PAUSE); break;
    case ACT_HOME: popup = POP_NONE; tui_goto(TS_HOME); break; /* match kept for Continue */
    case ACT_UP: case ACT_DOWN: case ACT_LEFT: case ACT_RIGHT: {
        int idx = human_index(pl);
        if (idx < 0) break;
        int dr = a == ACT_UP ? -1 : (a == ACT_DOWN ? 1 : 0);
        int dc = a == ACT_LEFT ? -1 : (a == ACT_RIGHT ? 1 : 0);
        int n = g->board.n;
        cur[idx].r = cur[idx].r + dr < 0 ? 0 : (cur[idx].r + dr >= n ? n - 1 : cur[idx].r + dr);
        cur[idx].c = cur[idx].c + dc < 0 ? 0 : (cur[idx].c + dc >= n ? n - 1 : cur[idx].c + dc);
        break;
    }
    case ACT_CONFIRM: {
        int idx = human_index(pl);
        if (idx >= 0) place_at(idx, cur[idx]);
        break;
    }
    default:
        break;
    }
}

static void board_click(Pos p)
{
    Game *g = &g_tui.game;
    if (g->status != GS_PLAYING) return;
    int idx = is_pve() ? 0 : g->turn;
    if (is_pve() && g->turn != 0) return;   /* the computer is thinking */
    cur[idx] = p;
    place_at(idx, p);
}

/* Bottom bar buttons: 0 undo, 1 surrender, 2 menu (pause), 3 home (to the main menu). */
static bool button_rect(int i, int *x, int *y)
{
    int cols, rows;
    term_size(&cols, &rows);
    int total = BAR_N * BTN_W + (BAR_N - 1) * 2;
    *x = (cols - total) / 2 + i * (BTN_W + 2);
    *y = rows - 7;
    return true;
}

static int bar_index(int mx, int my)
{
    for (int i = 0; i < BAR_N; i++) {
        int x, y;
        button_rect(i, &x, &y);
        if (my == y && mx >= x && mx < x + BTN_W) return i;
    }
    return -1;
}

static void bar_click(int mx, int my)
{
    int i = bar_index(mx, my);
    if (i == 0) request_undo_now();
    else if (i == 1) open_surrender();
    else if (i == 2) open_popup(POP_PAUSE);
    else if (i == 3) { popup = POP_NONE; tui_goto(TS_HOME); }
}

/* Hover (no popup open): highlights the bottom-bar button under the pointer and moves the
 * human's cursor onto the cell under it (PvE: the human is always player 0; PvP: the player to
 * move). Pure visual; does not place a stone and does not count as activity. */
static void game_hover(int mx, int my)
{
    Game *g = &g_tui.game;
    Pos p;
    bar_hot = bar_index(mx, my);
    if (g->status == GS_PLAYING && cell_at(mx, my, &p)) cur[is_pve() ? 0 : g->turn] = p;
}

/* ---------------------------------------------------------------- public API */

void tui_game_enter(void)
{
    if (!g_tui.has_game) { tui_goto(TS_HOME); return; }
    Game *g = &g_tui.game;
    /* a match with no moves yet can start in the same second as the previous one */
    bool same = seen_valid && g->start_time == seen_start && g->board.n == seen_n && g->move_count > 0;
    if (!same) reset_visual();     /* a new match: clear visuals; returning from menus keeps them */
    track_match();
}

void tui_game_event(const TermEvent *ev)
{
    if (!g_tui.has_game) return;
    Game *g = &g_tui.game;
    if (ev->type == TEV_MOUSE) {
        if (ev->mkind == TM_PRESS || ev->mkind == TM_WHEEL_UP || ev->mkind == TM_WHEEL_DOWN) game_activity(g);
        int cols, rows;
        term_size(&cols, &rows);
        if (popup != POP_NONE) {
            popup_build(cols, rows);
            popup_mouse(ev);
            return;
        }
        if (ev->mkind == TM_MOVE) { game_hover(ev->mx, ev->my); return; }
        if (ev->mkind != TM_PRESS || ev->mbutton != 0) return;
        Pos p;
        if (cell_at(ev->mx, ev->my, &p)) board_click(p);
        else bar_click(ev->mx, ev->my);
        return;
    }
    if (ev->type != TEV_KEY) return;
    game_activity(g);
    if (popup != POP_NONE) {
        /* PRD: Home / gamepad Start goes to the main screen from anywhere in a match, popups
         * included - except the AFK prompt, which must be answered with "Yes". After game over
         * the match is dropped, otherwise it stays in memory for Continue. */
        if (ev->key == TK_HOME && popup != POP_AFK) {
            if (g->status == GS_OVER) g_tui.has_game = false;
            popup = POP_NONE;
            tui_goto(TS_HOME);
            return;
        }
        int cols, rows;
        term_size(&cols, &rows);
        popup_build(cols, rows);
        popup_key(ev);
        return;
    }
    game_key(ev);
}

void tui_game_update(double dt)
{
    if (!g_tui.has_game) return;
    Game *g = &g_tui.game;
    game_tick(g, dt, popup == POP_NONE);

    if (g->afk == AFK_TIMEOUT) {
        int free_slot = storage_first_free_slot();     /* -1: every slot is taken before saving */
        int slot = storage_autosave_slot();
        bool ok = slot >= 0 && storage_save_game(slot, g);
        char b[120];
        if (ok) {
            if (free_slot < 0) snprintf(b, sizeof b, tr(S_AFK_OVERWROTE_FMT), slot + 1);
            else snprintf(b, sizeof b, tr(S_AFK_SAVED_FMT), slot + 1);
            tui_toast(b);
        } else {
            tui_toast(tr(S_LOAD_FAILED));
        }
        g_tui.has_game = false;
        popup = POP_NONE;
        tui_goto(TS_HOME);
        return;
    }
    if (g->afk == AFK_PROMPT && popup != POP_AFK) open_popup(POP_AFK);
    if (g->undo_requester >= 0 && popup != POP_UNDO && popup != POP_AFK) open_popup(POP_UNDO);

    /* computer move: waits for a short "thinking" delay and only while no popup is open */
    if (popup == POP_NONE && game_is_ai_turn(g)) {
        if (!ai_pending) {
            ai_pending = true;
            ai_wait = 0.45;
        }
        ai_wait -= dt;
        if (ai_wait <= 0) {
            ai_pending = false;
            Pos p = ai_choose_move(&g->board, game_cell_of(g->ai_player), g->diff, &g_tui.rng);
            if (p.r >= 0) game_play(g, p.r, p.c);
        }
    } else {
        ai_pending = false;
    }

    /* game over popup after a short beat so the winning line is seen */
    if (g->status == GS_OVER && popup == POP_NONE) {
        over_timer += dt;
        if (over_timer >= 0.8) open_popup(POP_OVER);
    } else {
        over_timer = 0;
    }
}

/* ---------------------------------------------------------------- drawing */

/* Player (0/1) who made the move at p; the highlight may point at either side in PvE. */
static int owner_of(const Game *g, Pos p)
{
    for (int i = g->move_count - 1; i >= 0; i--)
        if (g->moves[i].r == p.r && g->moves[i].c == p.c) return i % 2;
    return 0;
}

/* ---------------------------------------------------------------- backdrop */

/* Per-board backdrop (PRD: every board size has its own background). Painted first; the HUD
 * cards and the board are drawn over it, so the decorations only show in the free margins.
 * Decorations are dim and sparse so the text keeps its contrast. Animated unless reduced motion. */
static TColor backdrop_color(int n)
{
    if (n == 3) return 0x2B1410u;   /* Fire Shrine: dark ember red-brown */
    if (n == 5) return 0x10261Au;   /* Forest Temple: dark moss green */
    return 0x0B1F3Au;               /* Water Sanctum: deep blue */
}

static void draw_backdrop(int cols, int rows, int n)
{
    int tick = g_tui.settings.reduced_motion ? 0 : (int)(term_now() * 4.0);
    g_bg = backdrop_color(n);
    term_fill(0, 0, cols, rows, ' ', TC_TEXT, g_bg);
    if (n == 3) {
        /* ember dots: about 1 cell in 32, each flickering between two embers */
        for (int y = 0; y < rows; y++) {
            for (int x = 0; x < cols; x++) {
                uint32_t h = (uint32_t)(x * 73u + y * 151u) * 2654435761u;
                if ((h >> 27) != 0) continue;
                TColor fg = ((((h >> 8) & 3u) + (uint32_t)tick) & 1u) ? 0xE2642Cu : 0x7A2F14u;
                term_put(x, y, 0x2022u, fg, g_bg, 0);            /* • */
            }
        }
    } else if (n == 5) {
        /* vines hanging down both margins, swaying slowly */
        for (int y = 0; y < rows; y++) {
            int s = y + tick / 3;
            uint32_t cp = (s & 2) ? 0x2503u : 0x257Fu;          /* ┃ / ╿ */
            TColor fg = (s & 1) ? 0x3F8C45u : 0x2E6B34u;
            term_put(0, y, cp, fg, g_bg, 0);
            term_put(cols - 1, y, cp, fg, g_bg, 0);
        }
    } else {
        /* falling water: dashed columns at both margins, moving downwards */
        for (int y = 0; y < rows; y++) {
            for (int side = 0; side < 2; side++) {
                int x = side ? cols - 1 : 0;
                int ph = ((y - tick + side * 2) % 4 + 4) % 4;
                if (ph == 0) term_put(x, y, 0x250Au, side ? 0x5FB8E8u : 0x2A6FA8u, g_bg, 0);   /* ┊ */
            }
        }
    }
}

static void draw_board(void)
{
    Game *g = &g_tui.game;
    int bx, by, bw, bh, cw, ch, sep;
    board_geo(&bx, &by, &bw, &bh, &cw, &ch, &sep);
    int n = g->board.n;
    Pos last;
    bool has_last = game_highlight_move(g, &last);
    bool anim = !g_tui.settings.reduced_motion;
    double now = term_now();
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) {
            int x = bx + c * (cw + sep), y = by + r * (ch + sep);
            Cell v = g->board.c[r][c];
            bool is_last = has_last && last.r == r && last.c == c;
            bool is_win = false;
            if (g->end_reason == END_LINE)
                for (int k = 0; k < g->win_line.len; k++)
                    if (g->win_line.cells[k].r == r && g->win_line.cells[k].c == c) is_win = true;
            bool flashing = flash_until > now && flash_cell.r == r && flash_cell.c == c;
            TColor bg = g_bg;
            if (flashing) bg = TC_DANGER;
            else if (is_win) bg = TC_MOSS;
            else if (is_last) bg = TC_GOLD;
            term_fill(x, y, cw, ch, ' ', TC_BG, bg);
            int phase = (r * 3 + c * 7) & 3;
            if (v == CELL_EMPTY) {
                if (ch == 1) term_put(x + cw / 2, y, 0x00B7u, TC_BORDER, bg, 0);  /* faint dot */
                continue;
            }
            TColor fg;
            if (v == CELL_X) {
                fg = TC_FIRE;
                if (anim && (((int)(now * 5.0) + phase) & 1)) fg = TC_FIRE_HI;
            } else {
                fg = TC_WATER;
                if (anim && (((int)(now * 2.5) + phase) & 1)) fg = TC_WATER_HI;
            }
            if (is_last && !flashing) fg = TC_BG;
            if (ch == 1) {
                term_put(x + cw / 2, y, v == CELL_X ? 0x2573u : 0x25EFu, fg, bg, TATTR_BOLD);
            } else if (v == CELL_X) {
                int gx = x + (cw - 3) / 2, gy = y + (ch - 3) / 2;
                term_print(gx, gy, "\xE2\x95\xB2 \xE2\x95\xB1", fg, bg, TATTR_BOLD);       /* ╲ ╱ */
                term_print(gx, gy + 1, " \xE2\x95\xB3 ", fg, bg, TATTR_BOLD);              /*  ╳  */
                term_print(gx, gy + 2, "\xE2\x95\xB1 \xE2\x95\xB2", fg, bg, TATTR_BOLD);   /* ╱ ╲ */
            } else {
                int gx = x + (cw - 3) / 2, gy = y + (ch - 3) / 2;
                term_print(gx, gy, "\xE2\x95\xAD\xE2\x94\x80\xE2\x95\xAE", fg, bg, TATTR_BOLD); /* ╭─╮ */
                term_print(gx, gy + 1, "\xE2\x94\x82 \xE2\x94\x82", fg, bg, TATTR_BOLD);        /* │ │ */
                term_print(gx, gy + 2, "\xE2\x95\xB0\xE2\x94\x80\xE2\x95\xAF", fg, bg, TATTR_BOLD); /* ╰─╯ */
            }
        }
    }
    /* grid lines: verticals, then horizontals, then the crossings (no overwrite conflicts) */
    if (sep) {
        for (int k = 1; k < n; k++) {
            int xs = bx + k * (cw + 1) - 1;
            for (int y = by; y < by + bh; y++)
                if ((y - by) % (ch + 1) != ch) term_put(xs, y, 0x2502u, TC_BORDER, g_bg, 0);
        }
        for (int k = 1; k < n; k++) {
            int ys = by + k * (ch + 1) - 1;
            for (int x = bx; x < bx + bw; x++)
                if ((x - bx) % (cw + 1) != cw) term_put(x, ys, 0x2500u, TC_BORDER, g_bg, 0);
        }
        for (int i = 1; i < n; i++)
            for (int j = 1; j < n; j++)
                term_put(bx + i * (cw + 1) - 1, by + j * (ch + 1) - 1, 0x253Cu, TC_BORDER, g_bg, 0);
    }
    /* cursors: P1 (fire) top corners, P2 (water) bottom corners; 10x10 uses [ and ] */
    int nc = is_pve() ? 1 : 2;
    for (int p = 0; p < nc; p++) {
        int x = bx + cur[p].c * (cw + sep), y = by + cur[p].r * (ch + sep);
        TColor col = p == 0 ? TC_FIRE : TC_WATER;
        if (ch == 1) {
            term_put(x, y, '[', col, g_bg, TATTR_BOLD);
            term_put(x + cw - 1, y, ']', col, g_bg, TATTR_BOLD);
        } else if (p == 0) {
            term_put(x, y, 0x250Cu, col, g_bg, TATTR_BOLD);
            term_put(x + cw - 1, y, 0x2510u, col, g_bg, TATTR_BOLD);
        } else {
            term_put(x, y + ch - 1, 0x2514u, col, g_bg, TATTR_BOLD);
            term_put(x + cw - 1, y + ch - 1, 0x2518u, col, g_bg, TATTR_BOLD);
        }
    }
    /* invalid-move flash and win line are drawn through bg above */
}

static void draw_player_card(int x, int y, int p)
{
    Game *g = &g_tui.game;
    bool active = g->status == GS_PLAYING && g->turn == p;
    TColor edge = active ? (p == 0 ? TC_FIRE : TC_WATER) : TC_BORDER;
    TColor name_col = p == 0 ? TC_FIRE_HI : TC_WATER_HI;
    term_box(x, y, HUD_CARD_W, HUD_CARD_H, active ? 1 : 3, edge, TC_PANEL);
    bool bob = active && !g_tui.settings.reduced_motion && (((int)(term_now() * 2.0)) & 1);
    tui_draw_mascot(x + 2, y + 1, g->players[p].avatar, TC_PANEL, bob, p == 0);
    char nm[40];
    clip_cells(nm, sizeof nm, g->players[p].name, HUD_CARD_W - 2);
    put_centred(x, HUD_CARD_W, y + 9, nm, name_col, TC_PANEL, TATTR_BOLD);
    put_centred(x, HUD_CARD_W, y + 10, tr(p == 0 ? S_PLAYS_FIRE : S_PLAYS_WATER), TC_DIM, TC_PANEL, 0);
    put_centred(x, HUD_CARD_W, y + 11, tr(S_TIME_LEFT), TC_DIM, TC_PANEL, 0);
    char clock[16];
    fmt_clock(clock, sizeof clock, g->clock[p]);
    TColor cc = g->clock[p] < 60.0 ? TC_DANGER : TC_TEXT;
    put_centred(x, HUD_CARD_W, y + 12, clock, cc, TC_PANEL, TATTR_BOLD);
    if (p == 1 && is_pve()) {
        if (ai_pending) put_centred(x, HUD_CARD_W, y + 13, tr(S_THINKING), TC_WATER_HI, TC_PANEL, 0);
    } else if (active) {
        put_centred(x, HUD_CARD_W, y + 13, tr(S_YOUR_TURN), TC_GOLD, TC_PANEL, TATTR_BOLD);
    }
}

static void draw_header(int cols)
{
    Game *g = &g_tui.game;
    char b[64];
    if (g->start_time) {
        time_t t = (time_t)g->start_time;
        struct tm *tmv = localtime(&t);
        char hm[16] = "--:--";
        if (tmv) strftime(hm, sizeof hm, "%H:%M", tmv);
        snprintf(b, sizeof b, tr(S_STARTED_AT_FMT), hm);
        term_print(2, 0, b, TC_DIM, g_bg, 0);
    }
    int el = (int)g->elapsed;
    snprintf(b, sizeof b, "%s %02d:%02d", tr(S_ELAPSED), el / 60, el % 60);
    term_print(cols - 2 - utf8_cell_width(b), 0, b, TC_DIM, g_bg, 0);
    if (g->status == GS_OVER) {
        put_centred(0, cols, 0, tr(S_GAME_OVER), TC_GOLD, g_bg, TATTR_BOLD);
    } else {
        snprintf(b, sizeof b, tr(S_TURN_FMT), g->players[g->turn].name);
        put_centred(0, cols, 0, b, g->turn == 0 ? TC_FIRE_HI : TC_WATER_HI, g_bg, TATTR_BOLD);
    }
}

static void draw_side_info(int cols, int rows)
{
    Game *g = &g_tui.game;
    (void)rows;
    char b[64];
    int lx = 2, y = 2 + HUD_CARD_H + 1;
    snprintf(b, sizeof b, "%d x %d", g->board.n, g->board.n);
    term_print(lx, y, b, TC_DIM, g_bg, 0);
    snprintf(b, sizeof b, tr(S_MOVES_FMT), g->move_count);
    term_print(lx, y + 1, b, TC_DIM, g_bg, 0);
    term_print(lx, y + 2, tr(is_pve() ? S_MODE_PVE : S_MODE_PVP), TC_DIM, g_bg, 0);
    if (is_pve()) {
        term_print(lx, y + 3, tr(g->diff == DIFF_EASY ? S_DIFF_EASY :
                                 (g->diff == DIFF_HARD ? S_DIFF_HARD : S_DIFF_MEDIUM)), TC_DIM, g_bg, 0);
    }
    if (g->board.n >= 5) {
        snprintf(b, sizeof b, "%s: %s", tr(S_RULE_BLOCKED), tr(g->board.block_rule ? S_ON : S_OFF));
        term_print(lx, y + 4, b, TC_DIM, g_bg, 0);
    }
    int rx = cols - 1 - HUD_CARD_W;
    term_print(rx, y, tr(S_LAST_MOVE), TC_DIM, g_bg, 0);
    Pos lm;
    if (game_highlight_move(g, &lm)) {
        int who = owner_of(g, lm);
        snprintf(b, sizeof b, "%c%d  %.*s", 'A' + lm.c, lm.r + 1, PLAYER_NAME_MAX - 1, g->players[who].name);
        term_print(rx, y + 1, b, who == 0 ? TC_FIRE_HI : TC_WATER_HI, g_bg, 0);
    }
}

static void draw_bottom_bar(int cols, int rows)
{
    Game *g = &g_tui.game;
    bool playing = g->status == GS_PLAYING;
    char lbl[BAR_N][40];
    snprintf(lbl[0], sizeof lbl[0], "[U] %s", tr(S_UNDO));
    snprintf(lbl[1], sizeof lbl[1], "[G] %s", tr(S_SURRENDER));
    snprintf(lbl[2], sizeof lbl[2], "[Esc] %s", tr(S_MENU));
    snprintf(lbl[3], sizeof lbl[3], "[\xE2\x8C\x82 %s]", tr(S_HOME));
    bool can_undo = playing && game_can_request_undo(g, undo_requester());
    for (int i = 0; i < BAR_N; i++) {
        int x, y;
        button_rect(i, &x, &y);
        bool enabled = i >= 2 || (i == 0 ? can_undo : playing);
        bool hot = enabled && i == bar_hot;
        TColor bgc = hot ? 0x3D4329u : TC_PANEL;
        TColor fg = !enabled ? TC_BORDER : (hot ? TC_GOLD : (i == 1 ? TC_DANGER : TC_TEXT));
        term_fill(x, y, BTN_W, 1, ' ', fg, bgc);
        put_centred(x, BTN_W, y, lbl[i], fg, bgc, hot ? TATTR_BOLD : 0);
    }
    (void)cols; (void)rows;
}

static void draw_pbutton(const PItem *b, bool foc)
{
    if (foc && b->enabled) {
        term_fill(b->x, b->y, b->w, 1, ' ', TC_BG, TC_GOLD);
        put_centred(b->x, b->w, b->y, b->label, TC_BG, TC_GOLD, TATTR_BOLD);
        term_put(b->x - 2, b->y, 0x25B6u, TC_GOLD, TC_PANEL, 0);
        term_put(b->x + b->w + 1, b->y, 0x25C0u, TC_GOLD, TC_PANEL, 0);
        return;
    }
    TColor fg = b->enabled ? TC_TEXT : TC_BORDER;
    term_fill(b->x, b->y, b->w, 1, ' ', fg, TC_PANEL);
    put_centred(b->x, b->w, b->y, b->label, fg, TC_PANEL, 0);
}

static void draw_popup(void)
{
    term_box(pbx, pby, pbw, pbh, 3, TC_GOLD, TC_PANEL);
    put_centred(pbx, pbw, pby + 1, ptitle, TC_GOLD, TC_PANEL, TATTR_BOLD);
    if (pline1[0]) put_centred(pbx, pbw, pby + 3, pline1, popup == POP_OVER ? TC_GOLD : TC_TEXT, TC_PANEL, 0);
    if (pline2[0]) put_centred(pbx, pbw, pby + 4, pline2, TC_DIM, TC_PANEL, 0);
    if (popup == POP_AFK) {
        /* drain bar on the gap row above the Yes button (AFK always has two text lines) */
        Game *g = &g_tui.game;
        int barw = pbw - 10;
        double frac = game_afk_grace_s > 0 ? g->afk_left / game_afk_grace_s : 0;
        if (frac < 0) frac = 0;
        if (frac > 1) frac = 1;
        int filled = (int)(frac * barw + 0.5);
        int bx = pbx + 5, by = pby + 5;
        for (int i = 0; i < barw; i++) {
            if (i < filled) term_put(bx + i, by, 0x2588u, TC_DANGER, TC_PANEL, 0);
            else term_put(bx + i, by, 0x2591u, TC_BORDER, TC_PANEL, 0);
        }
    }
    for (int i = 0; i < npit; i++) draw_pbutton(&pit[i], i == pfocus);
}

void tui_game_draw(void)
{
    if (!g_tui.has_game) return;
    int cols, rows;
    term_size(&cols, &rows);
    Game *g = &g_tui.game;
    draw_backdrop(cols, rows, g->board.n);
    draw_header(cols);
    draw_player_card(1, 2, 0);
    draw_player_card(cols - 1 - HUD_CARD_W, 2, 1);
    draw_board();
    draw_side_info(cols, rows);
    draw_bottom_bar(cols, rows);
    put_centred(0, cols, rows - 2,
                tr(is_pve() ? S_HINT_GAME_PVE : S_HINT_GAME_PVP), TC_DIM, g_bg, 0);
    if (popup != POP_NONE) {
        popup_build(cols, rows);
        draw_popup();
    }
    tui_fx_frame(1000 + popup, cols, rows);
}
