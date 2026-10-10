/* scr_game.c - the in-match screen (SCENE_GAME): board, HUD cards, clocks, top bar, actions.
 *
 * Input model: board keys/mouse are handled in update(); HUD buttons are immediate-mode widgets
 * handled in draw(). ACT_CONFIRM pressed on this screen is swallowed with input_consume() so a
 * focused HUD button never fires on the same Space/Enter/A press that places a stone. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "screens.h"
#include "scene.h"
#include "ui.h"
#include "theme.h"
#include "art.h"
#include "fx.h"
#include "input.h"
#include "audio.h"
#include "app.h"
#include "ai_worker.h"
#include "../core/i18n.h"

#define BOARD_X 340.0f
#define BOARD_Y 80.0f
#define BOARD_S 600.0f
#define APPEAR_T 0.25f     /* piece pop-in duration */
#define FLASH_T 0.4f       /* red flash on an occupied cell */
#define SETTLED (-1000.0f) /* placed-at time for pieces that should not animate */

/* Per-match visual state. Reset whenever a different match is shown (see same_match). */
static int64_t s_key_start = -1;
static GameMode s_key_mode = MODE_PVE;
static int s_key_n = -1;
static int s_seen;                              /* moves already processed by sync() */
static bool s_over_seen;                        /* game-over sound/effects already played */
static float s_placed[CARO_MAX_N][CARO_MAX_N];  /* g_app.time when each cell was placed */
static Pos s_cur[2];                            /* keyboard/gamepad cursor per player */
static float s_ai_delay = -1.0f;                /* AI thinking countdown, < 0 = not armed */
static int64_t s_ai_key;                        /* position the running AI search belongs to */
static float s_flash_t;                         /* remaining red flash time */
static Pos s_flash;                             /* cell being flashed */
static int s_tick_key = -1;                     /* last clock tick (turn * 100 + second) */

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* 0..1 triangle wave with a 1 s period (t >= 0). */
static float tri(float t) { float f = t - (float)(int)t; return f < 0.5f ? 2.0f * f : 2.0f * (1.0f - f); }

static unsigned rng_next(void)
{
    unsigned x = g_app.rng ? g_app.rng : 1u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g_app.rng = x;
    return x;
}

static bool same_match(const Game *g)
{
    return g->start_time == s_key_start && g->mode == s_key_mode && g->board.n == s_key_n;
}

static void reset_match(const Game *g)
{
    s_key_start = g->start_time;
    s_key_mode = g->mode;
    s_key_n = g->board.n;
    s_seen = g->move_count;
    s_over_seen = g->status == GS_OVER;
    for (int r = 0; r < CARO_MAX_N; r++)
        for (int c = 0; c < CARO_MAX_N; c++) s_placed[r][c] = SETTLED;
    s_cur[0] = s_cur[1] = (Pos){ g->board.n / 2, g->board.n / 2 };
    s_ai_delay = -1.0f;
    s_flash_t = 0.0f;
    s_tick_key = -1;
}

static Rectangle cell_rect(int n, int r, int c)
{
    float cs = BOARD_S / (float)n;
    return (Rectangle){ BOARD_X + c * cs, BOARD_Y + r * cs, cs, cs };
}

static Vector2 cell_center(int n, Pos p)
{
    Rectangle r = cell_rect(n, p.r, p.c);
    return (Vector2){ r.x + r.width * 0.5f, r.y + r.height * 0.5f };
}

/* Maps a virtual-space point to a board cell; false when outside the board rect. */
static bool board_cell_at(int n, Vector2 m, int *r, int *c)
{
    Rectangle br = { BOARD_X, BOARD_Y, BOARD_S, BOARD_S };
    if (!CheckCollisionPointRec(m, br)) return false;
    float cs = BOARD_S / (float)n;
    *c = clampi((int)((m.x - BOARD_X) / cs), 0, n - 1);
    *r = clampi((int)((m.y - BOARD_Y) / cs), 0, n - 1);
    return true;
}

static void fmt_mmss(double secs, bool round_up, char *buf, size_t n)
{
    int s = secs <= 0.0 ? 0 : (int)secs;
    if (round_up && secs > (double)s) s++;
    snprintf(buf, n, "%02d:%02d", s / 60, s % 60);
}

/* Plays the placement sound/effects for every move that is new since the last call, and the
 * game-over sound once. Idempotent: called after every state change. Undo simply lowers s_seen. */
static void sync(Game *g)
{
    float now = (float)g_app.time;
    if (g->move_count < s_seen) s_seen = g->move_count;
    if (g->status == GS_PLAYING) s_over_seen = false;
    while (s_seen < g->move_count) {
        Pos p = g->moves[s_seen++];
        Cell who = board_get(&g->board, p.r, p.c);
        s_placed[p.r][p.c] = now;
        fx_spawn_place(who, cell_rect(g->board.n, p.r, p.c));
        audio_play(who == CELL_X ? SFX_PLACE_FIRE : SFX_PLACE_WATER);
    }
    if (g->status == GS_OVER && !s_over_seen) {
        s_over_seen = true;
        if (g->winner < 0 || g->end_reason == END_DRAW) {
            audio_play(SFX_POPUP);
        } else {
            fx_spawn_celebration(game_cell_of(g->winner));
            bool lost = g->mode == MODE_PVE && g->winner == g->ai_player;
            audio_play(lost ? SFX_LOSE : SFX_WIN);
        }
    }
}

/* SFX_TICK once per second for the active player when under 10 s. */
static void tick_sound(const Game *g)
{
    if (g->status != GS_PLAYING) return;
    double c = g->clock[g->turn];
    if (c <= 0.0 || c >= 10.0) return;
    int sec = (int)c;
    if (c > (double)sec) sec++;
    int key = g->turn * 100 + sec;
    if (key != s_tick_key) {
        s_tick_key = key;
        audio_play(SFX_TICK);
    }
}

/* Attempts a placement by the current player. Only the player to move can place; the AI turn
 * and finished games are silently ignored. */
static bool try_place(Game *g, Pos at)
{
    if (g->status != GS_PLAYING || game_is_ai_turn(g)) return false;
    if (board_get(&g->board, at.r, at.c) != CELL_EMPTY) {
        audio_play(SFX_ERROR);
        s_flash = at;
        s_flash_t = FLASH_T;
        return false;
    }
    if (!game_play(g, at.r, at.c)) {
        audio_play(SFX_ERROR);
        return false;
    }
    return true;
}

static void do_undo(Game *g)
{
    if (g->mode == MODE_PVE) {
        if (game_can_request_undo(g, 0) && game_request_undo(g, 0)) {
            app_toast(tr(S_UNDO_DONE));
            audio_play(SFX_UNDO);
        } else {
            app_toast(tr(S_UNDO_NOT_ALLOWED));
            audio_play(SFX_ERROR);
        }
        return;
    }
    /* PvP: the player who made the last move asks; the opponent answers in the overlay. */
    int requester = 1 - g->turn;
    if (g->move_count > 0 && game_can_request_undo(g, requester) && game_request_undo(g, requester)) {
        return; /* OVL_UNDO_REQUEST opens from overlay_update() */
    }
    app_toast(tr(S_UNDO_NOT_ALLOWED));
    audio_play(SFX_ERROR);
}

static void update(float dt)
{
    Game *g = &g_app.game;
    if (!g_app.has_game) return;
    if (!same_match(g)) reset_match(g);
    if (s_flash_t > 0.0f) s_flash_t -= dt;

    if (overlay_update(dt)) {
        /* Clocks paused; AFK still tracked. Interaction with the popup counts as activity. */
        game_tick(g, dt, false);
        if (input_any_activity()) game_activity(g);
        sync(g);
        return;
    }

    game_tick(g, dt, true);
    if (input_any_activity()) game_activity(g);
    sync(g);
    tick_sound(g);

    if (input_pressed(ACT_HOME, -1)) { overlay_open(OVL_LEAVE_CONFIRM); return; }
    bool playing = g->status == GS_PLAYING;
    if (playing && input_pressed(ACT_BACK, -1)) { overlay_open(OVL_PAUSE); return; }

    bool conf_any = input_pressed(ACT_CONFIRM, -1);
    if (playing) {
        int n = g->board.n;
        int owners = g->mode == MODE_PVP ? 2 : 1;
        for (int p = 0; p < owners; p++) {
            int pl = g->mode == MODE_PVP ? p : -1; /* PvE: either key set drives the human */
            Pos *cp = &s_cur[p];
            if (input_pressed(ACT_UP, pl)) cp->r = clampi(cp->r - 1, 0, n - 1);
            if (input_pressed(ACT_DOWN, pl)) cp->r = clampi(cp->r + 1, 0, n - 1);
            if (input_pressed(ACT_LEFT, pl)) cp->c = clampi(cp->c - 1, 0, n - 1);
            if (input_pressed(ACT_RIGHT, pl)) cp->c = clampi(cp->c + 1, 0, n - 1);
        }
        bool conf_me = g->mode == MODE_PVP ? input_pressed(ACT_CONFIRM, g->turn) : conf_any;
        if (conf_me) try_place(g, s_cur[g->mode == MODE_PVP ? g->turn : 0]);

        if (input_mouse_pressed()) {
            int r, c;
            if (board_cell_at(n, input_mouse(), &r, &c) && !game_is_ai_turn(g)) {
                s_cur[g->turn] = (Pos){ r, c };
                try_place(g, s_cur[g->turn]);
            }
        }
    }
    /* Swallow the confirm press so a focused HUD button does not also fire. */
    if (conf_any) input_consume();

    /* AI: search on a worker thread while a 0.35-0.6 s "thinking" delay runs, then move once
     * both are done. Only reached when no overlay is open. The result is applied only if the
     * position is unchanged (same match and move count). */
    if (game_is_ai_turn(g)) {
        if (s_ai_delay < 0.0f) {
            s_ai_delay = 0.35f + 0.25f * (float)(rng_next() % 1000u) / 1000.0f;
            s_ai_key = g->start_time * 1000 + g->move_count;
            ai_worker_start(&g->board, game_cell_of(g->turn), g->diff, rng_next() | 1u);
        }
        s_ai_delay -= dt;
        Pos p;
        if (s_ai_delay <= 0.0f && ai_worker_poll(&p)) {
            s_ai_delay = -1.0f;
            if (s_ai_key == g->start_time * 1000 + g->move_count && p.r >= 0) game_play(g, p.r, p.c);
        } else if (s_ai_delay <= 0.0f) {
            s_ai_delay = 0.0f; /* keep waiting for the worker */
        }
    } else {
        if (s_ai_delay >= 0.0f) ai_worker_cancel();
        s_ai_delay = -1.0f;
    }
    sync(g);
}

/* ---- drawing ---- */

/* Gates HUD widgets: interactive only when `on` and no overlay is open. */
static void gate(bool on)
{
    ui_set_interactive(on && overlay_current() == OVL_NONE);
}

static void draw_board(const Game *g, float now)
{
    int n = g->board.n;
    Rectangle br = { BOARD_X, BOARD_Y, BOARD_S, BOARD_S };
    bool playing = g->status == GS_PLAYING;
    bool free_input = playing && overlay_current() == OVL_NONE;
    bool human_turn = free_input && !game_is_ai_turn(g);

    art_draw_board_slab(br, n, art_board_bg(n));

    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) {
            Cell w = g->board.c[r][c];
            if (w == CELL_EMPTY) continue;
            float age = clampf((now - s_placed[r][c]) / APPEAR_T, 0.0f, 1.0f);
            fx_draw_piece(w, cell_rect(n, r, c), now, ease_out_cubic(age));
        }
    }

    int hr, hc;
    if (human_turn && board_cell_at(n, input_mouse(), &hr, &hc) && g->board.c[hr][hc] == CELL_EMPTY)
        fx_draw_ghost(game_cell_of(g->turn), cell_rect(n, hr, hc), 0.55f);

    Pos lm;
    if (game_last_move(g, &lm))
        fx_draw_last_move(cell_rect(n, lm.r, lm.c), board_get(&g->board, lm.r, lm.c), now);

    if (playing) {
        int owners = g->mode == MODE_PVP ? 2 : 1;
        for (int p = 0; p < owners; p++) {
            Rectangle cr = cell_rect(n, s_cur[p].r, s_cur[p].c);
            bool own = g->mode == MODE_PVP ? (g->turn == p) : (p == 0 && !game_is_ai_turn(g));
            if (own && overlay_current() == OVL_NONE) fx_draw_cursor(cr, p, now);
            else DrawRectangleLinesEx(cr, 2, Fade(p == 0 ? COL_FIRE : COL_WATER, 0.35f));
        }
    }

    if (g->status == GS_OVER && g->end_reason == END_LINE && g->win_line.found && g->win_line.len > 0) {
        Vector2 a = cell_center(n, g->win_line.cells[0]);
        Vector2 b = cell_center(n, g->win_line.cells[g->win_line.len - 1]);
        fx_draw_win_line(a, b, g->win_line.who, now);
    }

    if (s_flash_t > 0.0f)
        DrawRectangleRec(cell_rect(n, s_flash.r, s_flash.c), Fade(COL_DANGER, 0.6f * s_flash_t / FLASH_T));

    fx_draw();
}

static void draw_top_bar(const Game *g)
{
    Rectangle r = { BOARD_X, 12, BOARD_S, 60 };
    ui_panel(r);

    char banner[96];
    Color bc = COL_GOLD;
    if (g->status == GS_PLAYING) {
        snprintf(banner, sizeof banner, tr(S_TURN_FMT), g->players[g->turn].name);
        bc = g->turn == 0 ? COL_FIRE_LIGHT : COL_WATER_LIGHT;
    } else {
        snprintf(banner, sizeof banner, "%s", tr(S_GAME_OVER));
    }
    float bsize = text_measure(banner, FS_BODY).x > BOARD_S - 40 ? FS_SMALL : FS_BODY;
    text_draw_aligned(banner, r.x + r.width / 2, r.y + 8, bsize, bc, 1);

    char hhmm[16] = "--:--";
    time_t t = (time_t)g->start_time;
    struct tm *lt = localtime(&t);
    if (lt) strftime(hhmm, sizeof hhmm, "%H:%M", lt);
    char started[48], line[96], el[16];
    snprintf(started, sizeof started, tr(S_STARTED_AT_FMT), hhmm);
    fmt_mmss(g->elapsed, false, el, sizeof el);
    snprintf(line, sizeof line, "%s     %s %s", started, tr(S_ELAPSED), el);
    text_draw_aligned(line, r.x + r.width / 2, r.y + 36, FS_SMALL, COL_TEXT_DIM, 1);
}

static void draw_hud(const Game *g, int p, float now)
{
    Rectangle card = p == 0 ? (Rectangle){ 24, 80, 288, 360 } : (Rectangle){ 968, 80, 288, 360 };
    Color elem = p == 0 ? COL_FIRE : COL_WATER;
    bool playing = g->status == GS_PLAYING;
    bool active = playing && g->turn == p;
    bool ai_thinking = active && game_is_ai_turn(g);
    float cx = card.x + card.width / 2;
    const PlayerInfo *pi = &g->players[p];

    ui_panel(card);
    if (active) {
        float pulse = tri(now * 0.5f);
        DrawRectangleLinesEx((Rectangle){ card.x - 4, card.y - 4, card.width + 8, card.height + 8 },
                             3, Fade(elem, 0.3f + 0.3f * pulse));
        DrawRectangleLinesEx(card, 4, elem);
    }

    art_draw_avatar(pi->avatar, (Rectangle){ cx - 48, card.y + 16, 96, 96 }, now);

    float nsize = text_measure(pi->name, FS_H2).x > card.width - 24 ? FS_BODY : FS_H2;
    text_draw_aligned(pi->name, cx, card.y + 120, nsize, COL_TEXT, 1);

    /* mini piece icon + side label */
    const char *side = tr(p == 0 ? S_PLAYS_FIRE : S_PLAYS_WATER);
    float gw = 40 + SP_1 + text_measure(side, FS_SMALL).x;
    float x0 = cx - gw / 2;
    fx_draw_piece(game_cell_of(p), (Rectangle){ x0, card.y + 172, 40, 40 }, now, 1.0f);
    text_draw(side, x0 + 40 + SP_1, card.y + 180, FS_SMALL, elem);

    text_draw_aligned(tr(S_TIME_LEFT), cx, card.y + 218, FS_SMALL, COL_TEXT_DIM, 1);
    char clk[16];
    fmt_mmss(g->clock[p], true, clk, sizeof clk);
    Color cc = COL_TEXT;
    if (g->clock[p] < 60.0) {
        float pulse = tri(now * 1.5f);
        cc = Fade(COL_DANGER, 0.55f + 0.45f * pulse);
    }
    text_draw_aligned(clk, cx, card.y + 240, FS_H1, cc, 1);

    if (ai_thinking) text_draw_aligned(tr(S_THINKING), cx, card.y + 316, FS_BODY, elem, 1);
    else if (active) text_draw_aligned(tr(S_YOUR_TURN), cx, card.y + 316, FS_BODY, elem, 1);

    if (!active) DrawRectangleRec(card, Fade(COL_STONE_900, 0.35f));
}

static void draw_info(const Game *g)
{
    Rectangle r = { 968, 456, 288, 200 };
    float cx = r.x + r.width / 2;
    char buf[64];
    ui_panel(r);
    snprintf(buf, sizeof buf, tr(S_MOVES_FMT), g->move_count);
    text_draw_aligned(buf, cx, r.y + 20, FS_BODY, COL_TEXT, 1);
    text_draw_aligned(tr(S_LAST_MOVE), cx, r.y + 72, FS_SMALL, COL_TEXT_DIM, 1);
    Pos lm;
    if (game_last_move(g, &lm)) {
        Cell w = board_get(&g->board, lm.r, lm.c);
        snprintf(buf, sizeof buf, "(%d, %d)", lm.r + 1, lm.c + 1);
        text_draw_aligned(buf, cx, r.y + 104, FS_H2, w == CELL_X ? COL_FIRE_LIGHT : COL_WATER_LIGHT, 1);
    } else {
        text_draw_aligned("-", cx, r.y + 104, FS_H2, COL_TEXT_DIM, 1);
    }
}

/* Undo / Surrender / Menu under the left card, Home icon at the top-left. */
static void draw_actions(Game *g)
{
    bool playing = g->status == GS_PLAYING;
    Rectangle ur = { 24, 456, 288, 56 };
    Rectangle sr = { 24, 528, 288, 56 };
    Rectangle mr = { 24, 600, 288, 56 };
    Rectangle hr = { 24, 12, 56, 56 };

    gate(playing);
    if (ui_button(ur, tr(S_UNDO), BTN_NORMAL)) do_undo(g);
    gate(playing);
    if (ui_button(sr, tr(S_SURRENDER), BTN_DANGER)) overlay_open(OVL_SURRENDER_CONFIRM);
    gate(true);
    if (ui_button(mr, tr(S_MENU), BTN_NORMAL)) overlay_open(OVL_PAUSE);
    gate(true);
    if (ui_icon_button(hr, "", tr(S_HOME))) overlay_open(OVL_LEAVE_CONFIRM);

    /* pixel house glyph on the Home icon */
    DrawRectangle((int)hr.x + 26, (int)hr.y + 16, 4, 4, COL_GOLD);
    DrawRectangle((int)hr.x + 22, (int)hr.y + 20, 12, 4, COL_GOLD);
    DrawRectangle((int)hr.x + 18, (int)hr.y + 24, 20, 4, COL_GOLD);
    DrawRectangle((int)hr.x + 16, (int)hr.y + 28, 24, 16, COL_TEXT_DIM);
    DrawRectangle((int)hr.x + 26, (int)hr.y + 34, 4, 10, COL_STONE_900);

    if (!playing) {
        DrawRectangleRec(ur, Fade(COL_STONE_900, 0.55f));
        DrawRectangleRec(sr, Fade(COL_STONE_900, 0.55f));
    }
}

static void draw(void)
{
    float now = (float)g_app.time;
    if (!g_app.has_game) {
        art_draw_background(BG_MENU, now);
        return;
    }
    Game *g = &g_app.game;
    art_draw_background(art_board_bg(g->board.n), now);
    ui_frame_begin();
    gate(true);

    draw_board(g, now);
    draw_top_bar(g);
    draw_hud(g, 0, now);
    draw_hud(g, 1, now);
    draw_actions(g);
    draw_info(g);
    ui_hint_bar(tr(g->mode == MODE_PVP ? S_HINT_GAME_PVP : S_HINT_GAME_PVE));

    ui_frame_end();
    overlay_draw();
}

static void enter(void)
{
    audio_music(BGM_GAME);
    fx_clear();
    ui_focus_reset();
    if (!g_app.has_game) return;
    if (!same_match(&g_app.game)) reset_match(&g_app.game);
    else if (s_seen > g_app.game.move_count) s_seen = g_app.game.move_count;
    s_ai_delay = -1.0f;
    ai_worker_cancel();
}

static void leave(void)
{
    ai_worker_cancel();
    s_ai_delay = -1.0f;
}

void scr_game_register(void)
{
    scene_register(SCENE_GAME, (Scene){ enter, leave, update, draw });
}
