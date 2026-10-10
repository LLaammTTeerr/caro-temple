/* scr_overlays.c - in-game popups: pause, undo request, surrender / leave confirms, AFK
 * prompt and game over. Driven by scr_game.c: overlay_update(dt) runs before its own input
 * and overlay_draw() runs last. Widgets are immediate-mode, so every button result is
 * collected during draw and applied once, after all widgets for the frame were drawn. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "screens.h"
#include "scene.h"
#include "ui.h"
#include "theme.h"
#include "art.h"
#include "audio.h"
#include "input.h"
#include "app.h"
#include "../core/i18n.h"

#define OVL_APPEAR_S          0.18f
#define OVL_APPEAR_REDUCED_S  0.06f
#define OVL_OVER_DELAY_S      0.8f
#define OVL_TICK_SECONDS      10

/* Button results. Applied once per frame. */
typedef enum {
    OA_NONE = 0,
    OA_RESUME,
    OA_SAVE,
    OA_UNDO,
    OA_SETTINGS,
    OA_ASK_SURRENDER,
    OA_ASK_HOME,
    OA_UNDO_ACCEPT,
    OA_UNDO_DECLINE,
    OA_SURRENDER_YES,
    OA_SURRENDER_NO,
    OA_LEAVE_YES,
    OA_LEAVE_NO,
    OA_AFK_YES,
    OA_REMATCH,
    OA_GAME_HOME
} OvlAction;

static OverlayId cur = OVL_NONE;
static float appear = 0.0f;       /* 0..1 popup appear progress */
static float over_wait = -1.0f;   /* >= 0 while the game-over delay runs */
static int tick_last = -1;        /* last whole AFK second that played SFX_TICK */

/* Switch popup without sound (used for back navigation). */
static void set_overlay(OverlayId id)
{
    cur = id;
    appear = 0.0f;
    tick_last = -1;
    ui_focus_scope(1);
    ui_focus_reset();
    if (id == OVL_SURRENDER_CONFIRM) ui_focus_set(1); /* default to "No" */
    ui_focus_scope(0);
}

void overlay_open(OverlayId id)
{
    set_overlay(id);
    audio_play(SFX_POPUP);
}

void overlay_close(void)
{
    cur = OVL_NONE;
}

OverlayId overlay_current(void)
{
    return cur;
}

static Color side_color(int p)
{
    return p == 0 ? COL_FIRE : COL_WATER;
}

static void fmt_clock(char *buf, size_t n, double secs)
{
    int s = secs < 0.0 ? 0 : (int)secs;
    snprintf(buf, n, "%02d:%02d", s / 60, s % 60);
}

static void draw_center_shadow(const char *s, float cx, float y, float size, Color c)
{
    Vector2 m = text_measure(s, size);
    text_draw_shadow(s, cx - m.x / 2, y, size, c);
}

/* Button that stays visible but is dimmed and not focusable when disabled. */
static bool menu_button(Rectangle r, const char *label, ButtonStyle st, bool enabled)
{
    if (!enabled) {
        DrawRectangleRec(r, Fade(COL_STONE_700, 0.4f));
        DrawRectangleLinesEx(r, 2, Fade(COL_STONE_600, 0.4f));
        text_draw_aligned(label, r.x + r.width / 2, r.y + (r.height - FS_BODY) / 2,
                          FS_BODY, Fade(COL_TEXT, 0.4f), 1);
        return false;
    }
    return ui_button(r, label, st);
}

static bool reduced(void)
{
    return g_app.settings.reduced_motion;
}

/* ---------- popup bodies: each returns the button result for this frame ---------- */

static OvlAction draw_pause(Rectangle c, const Game *g)
{
    OvlAction a = OA_NONE;
    bool playing = g->status == GS_PLAYING;
    float bw = 400, bh = 56, step = 56 + SP_2;
    Rectangle r = { c.x + (c.width - bw) / 2, c.y + SP_1, bw, bh };

    if (ui_button(r, tr(S_RESUME), BTN_PRIMARY)) a = OA_RESUME;
    r.y += step;
    if (ui_button(r, tr(S_SAVE_GAME), BTN_NORMAL)) a = OA_SAVE;
    r.y += step;
    if (menu_button(r, tr(S_UNDO), BTN_NORMAL, playing)) a = OA_UNDO;
    r.y += step;
    if (ui_button(r, tr(S_SETTINGS), BTN_NORMAL)) a = OA_SETTINGS;
    r.y += step;
    if (menu_button(r, tr(S_SURRENDER), BTN_DANGER, playing)) a = OA_ASK_SURRENDER;
    r.y += step;
    if (ui_button(r, tr(S_HOME), BTN_NORMAL)) a = OA_ASK_HOME;
    return a;
}

static OvlAction draw_undo_request(Rectangle c, const Game *g)
{
    OvlAction a = OA_NONE;
    int req = g->undo_requester < 0 ? 0 : g->undo_requester;
    int ans = 1 - req;   /* the opponent must answer */
    float cx = c.x + c.width / 2;
    char buf[256];

    art_draw_avatar(g->players[ans].avatar, (Rectangle){ cx - 64, c.y, 128, 128 }, g_app.time);
    text_draw_aligned(g->players[ans].name, cx, c.y + 136, FS_H2, side_color(ans), 1);
    snprintf(buf, sizeof buf, tr(S_UNDO_REQUEST_FMT), g->players[req].name);
    text_draw_wrapped(buf, (Rectangle){ c.x + SP_3, c.y + 184, c.width - 2 * SP_3, 100 },
                      FS_BODY, COL_TEXT, 1);

    float bw = 240, y = c.y + 296;
    Rectangle r1 = { cx - bw - SP_2 / 2, y, bw, 56 };
    Rectangle r2 = { cx + SP_2 / 2, y, bw, 56 };
    if (ui_button(r1, tr(S_ACCEPT), BTN_PRIMARY)) a = OA_UNDO_ACCEPT;
    if (ui_button(r2, tr(S_DECLINE), BTN_NORMAL)) a = OA_UNDO_DECLINE;
    return a;
}

static OvlAction draw_surrender(Rectangle c, const Game *g)
{
    OvlAction a = OA_NONE;
    int who = g->mode == MODE_PVP ? g->turn : 0;
    float cx = c.x + c.width / 2;
    char buf[160];

    art_draw_avatar(g->players[who].avatar, (Rectangle){ cx - 48, c.y, 96, 96 }, g_app.time);
    snprintf(buf, sizeof buf, tr(S_SURRENDER_Q_FMT), g->players[who].name);
    text_draw_wrapped(buf, (Rectangle){ c.x + SP_3, c.y + 112, c.width - 2 * SP_3, 96 },
                      FS_BODY, side_color(who), 1);

    float bw = 240, y = c.y + 224;
    Rectangle r1 = { cx - bw - SP_2 / 2, y, bw, 56 };
    Rectangle r2 = { cx + SP_2 / 2, y, bw, 56 };
    if (ui_button(r1, tr(S_YES), BTN_DANGER)) a = OA_SURRENDER_YES;
    if (ui_button(r2, tr(S_NO), BTN_NORMAL)) a = OA_SURRENDER_NO;
    return a;
}

static OvlAction draw_leave(Rectangle c)
{
    OvlAction a = OA_NONE;
    float cx = c.x + c.width / 2;
    text_draw_wrapped(tr(S_LEAVE_GAME_Q), (Rectangle){ c.x + SP_3, c.y + SP_1, c.width - 2 * SP_3, 110 },
                      FS_BODY, COL_TEXT, 1);

    float bw = 240, y = c.y + c.height - 56 - SP_1;
    Rectangle r1 = { cx - bw - SP_2 / 2, y, bw, 56 };
    Rectangle r2 = { cx + SP_2 / 2, y, bw, 56 };
    if (ui_button(r1, tr(S_YES), BTN_PRIMARY)) a = OA_LEAVE_YES;
    if (ui_button(r2, tr(S_NO), BTN_NORMAL)) a = OA_LEAVE_NO;
    return a;
}

static OvlAction draw_afk(Rectangle c, const Game *g)
{
    OvlAction a = OA_NONE;
    float cx = c.x + c.width / 2;
    float grace = game_afk_grace_s > 0.0 ? (float)game_afk_grace_s : 1.0f;
    float left = g->afk_left > 0.0 ? (float)g->afk_left : 0.0f;
    int secs = (int)ceilf(left);
    char num[16], line[96];

    text_draw_aligned(tr(S_AFK_Q), cx, c.y, FS_BODY, COL_TEXT, 1);

    snprintf(num, sizeof num, "%d", secs);
    draw_center_shadow(num, cx, c.y + 40, FS_HUGE, secs <= OVL_TICK_SECONDS ? COL_DANGER : COL_GOLD);

    /* Draining bar: gold while there is time, turning danger red as it nears 0. */
    float frac = left / grace;
    if (frac > 1.0f) frac = 1.0f;
    Rectangle track = { cx - 240, c.y + 160, 480, 24 };
    DrawRectangleRec(track, COL_STONE_900);
    Color fill = frac >= 0.5f ? COL_GOLD : color_lerp(COL_DANGER, COL_GOLD, frac * 2.0f);
    DrawRectangleRec((Rectangle){ track.x, track.y, track.width * frac, track.height }, fill);
    DrawRectangleLinesEx(track, 2, COL_STONE_600);

    snprintf(line, sizeof line, tr(S_AFK_COUNTDOWN_FMT), secs);
    text_draw_aligned(line, cx, c.y + 196, FS_SMALL, COL_TEXT_DIM, 1);

    if (ui_button((Rectangle){ cx - 180, c.y + 240, 360, 72 }, tr(S_YES), BTN_PRIMARY)) a = OA_AFK_YES;
    return a;
}

static OvlAction draw_game_over(Rectangle c, const Game *g)
{
    OvlAction a = OA_NONE;
    float cx = c.x + c.width / 2;
    float t = g_app.time;
    float bob = reduced() ? 0.0f : -fabsf(sinf(t * 4.0f)) * 12.0f;
    int w = g->winner;
    char buf[PLAYER_NAME_MAX + 64], line[96], clk[16], moves[32];

    if (w == 0 || w == 1) {
        snprintf(buf, sizeof buf, tr(S_WINS_FMT), g->players[w].name);
        draw_center_shadow(buf, cx, c.y, FS_H1, side_color(w));
        art_draw_avatar(g->players[w].avatar, (Rectangle){ cx - 64, c.y + 72 + bob, 128, 128 }, t);
    } else {
        draw_center_shadow(tr(S_DRAW), cx, c.y, FS_H1, COL_GOLD);
        art_draw_avatar(g->players[0].avatar, (Rectangle){ cx - 104, c.y + 80 + bob, 96, 96 }, t);
        art_draw_avatar(g->players[1].avatar, (Rectangle){ cx + 8, c.y + 80 + bob, 96, 96 }, t);
    }

    if (g->mode == MODE_PVE && w >= 0) {
        draw_center_shadow(tr(w == 0 ? S_YOU_WIN : S_YOU_LOSE), cx, c.y + 216, FS_H2,
                           w == 0 ? COL_GOLD : COL_DANGER);
    }

    const char *why = "";
    switch (g->end_reason) {
    case END_LINE: why = tr(S_REASON_LINE); break;
    case END_TIMEOUT: why = tr(S_REASON_TIMEOUT); break;
    case END_SURRENDER: why = tr(S_REASON_SURRENDER); break;
    case END_DRAW: why = tr(S_REASON_DRAW); break;
    default: break;
    }
    text_draw_aligned(why, cx, c.y + 270, FS_BODY, COL_TEXT_DIM, 1);

    fmt_clock(clk, sizeof clk, g->elapsed);
    snprintf(moves, sizeof moves, tr(S_MOVES_FMT), g->move_count);
    snprintf(line, sizeof line, "%s   %s %s", moves, tr(S_ELAPSED), clk);
    text_draw_aligned(line, cx, c.y + 318, FS_SMALL, COL_TEXT, 1);

    float bw = 260, y = c.y + c.height - 56 - SP_1;
    Rectangle r1 = { cx - bw - SP_2 / 2, y, bw, 56 };
    Rectangle r2 = { cx + SP_2 / 2, y, bw, 56 };
    if (ui_button(r1, tr(S_REMATCH), BTN_PRIMARY)) a = OA_REMATCH;
    if (ui_button(r2, tr(S_HOME), BTN_NORMAL)) a = OA_GAME_HOME;
    return a;
}

/* ---------- actions ---------- */

static void do_undo(Game *g)
{
    if (g->mode == MODE_PVE) {
        if (game_request_undo(g, 0)) {
            audio_play(SFX_UNDO);
            app_toast(tr(S_UNDO_DONE));
            overlay_close();
        } else {
            audio_play(SFX_ERROR);
            app_toast(tr(S_UNDO_NOT_ALLOWED));
        }
        return;
    }
    /* PvP: the player who made the last move is the one not on turn. */
    int req = 1 - g->turn;
    if (game_can_request_undo(g, req) && game_request_undo(g, req)) {
        overlay_close();   /* UNDO_REQUEST opens on the next update */
    } else {
        audio_play(SFX_ERROR);
        app_toast(tr(S_UNDO_NOT_ALLOWED));
    }
}

static void do_rematch(Game *g)
{
    Settings *s = &g_app.settings;
    s->last_mode = g->mode;
    s->last_board = g->board.n;
    s->last_diff = g->diff;
    s->block_rule = g->board.block_rule;
    s->profile[0] = g->players[0];
    if (g->mode == MODE_PVP) s->profile[1] = g->players[1];
    overlay_close();
    app_start_new_game();
}

static void apply_action(OvlAction act)
{
    Game *g = &g_app.game;
    switch (act) {
    case OA_NONE:
        break;
    case OA_RESUME:
        overlay_close();
        break;
    case OA_SAVE:
        g_app.saves_mode = SAVES_SAVE;
        overlay_close();
        scene_push(SCENE_SAVES);
        break;
    case OA_UNDO:
        do_undo(g);
        break;
    case OA_SETTINGS:
        overlay_close();
        scene_push(SCENE_SETTINGS);
        break;
    case OA_ASK_SURRENDER:
        overlay_open(OVL_SURRENDER_CONFIRM);
        break;
    case OA_ASK_HOME:
        overlay_open(OVL_LEAVE_CONFIRM);
        break;
    case OA_UNDO_ACCEPT:
        game_answer_undo(g, true);
        audio_play(SFX_UNDO);
        app_toast(tr(S_UNDO_DONE));
        overlay_close();
        break;
    case OA_UNDO_DECLINE:
        game_answer_undo(g, false);
        audio_play(SFX_CLICK);
        app_toast(tr(S_UNDO_DECLINED));
        overlay_close();
        break;
    case OA_SURRENDER_YES:
        game_surrender(g, g->mode == MODE_PVP ? g->turn : 0);
        overlay_close();
        break;
    case OA_SURRENDER_NO:
        overlay_open(OVL_PAUSE);
        break;
    case OA_LEAVE_YES:
        overlay_close();
        if (g->status == GS_OVER) g_app.has_game = false; /* a finished match is not resumable */
        scene_goto(SCENE_HOME);
        break;
    case OA_LEAVE_NO:
        overlay_open(OVL_PAUSE);
        break;
    case OA_AFK_YES:
        game_afk_continue(g);
        overlay_close();
        break;
    case OA_REMATCH:
        do_rematch(g);
        break;
    case OA_GAME_HOME:
        overlay_close();
        g_app.has_game = false;
        scene_goto(SCENE_HOME);
        break;
    }
}

static void play_result_sfx(const Game *g)
{
    if (g->winner < 0) return;
    if (g->mode == MODE_PVE) audio_play(g->winner == 0 ? SFX_WIN : SFX_LOSE);
    else audio_play(SFX_WIN);
}

/* ---------- per-frame logic ---------- */

bool overlay_update(float dt)
{
    Game *g = &g_app.game;
    float dur = reduced() ? OVL_APPEAR_REDUCED_S : OVL_APPEAR_S;
    if (appear < 1.0f) {
        appear += dt / dur;
        if (appear > 1.0f) appear = 1.0f;
    }

    /* Game over: the win line stays visible for a moment, then the popup opens. */
    if (g->status == GS_OVER) {
        if (cur == OVL_GAME_OVER) return true;
        if (over_wait < 0.0f) over_wait = OVL_OVER_DELAY_S;
        over_wait -= dt;
        if (cur != OVL_NONE) overlay_close();
        if (over_wait <= 0.0f) {
            over_wait = -1.0f;
            overlay_open(OVL_GAME_OVER);
            play_result_sfx(g);
            return true;
        }
        return false;
    }
    over_wait = -1.0f;

    /* AFK timeout: autosave to a free slot (or the oldest), then leave for Home. */
    if (g->afk == AFK_TIMEOUT) {
        int slot = storage_autosave_slot();
        bool ok = slot >= 0 && storage_save_game(slot, g);
        g->afk = AFK_NONE;
        g->idle = 0.0;
        overlay_close();
        if (ok) {
            char buf[128];
            snprintf(buf, sizeof buf, tr(S_AFK_SAVED_FMT), slot + 1);
            app_toast(buf);
            g_app.has_game = false;
        } else {
            /* Keep the match in memory so Continue can still resume it. */
            audio_play(SFX_ERROR);
            app_toast(tr(S_LOAD_FAILED));
        }
        scene_goto(SCENE_HOME);
        return true;
    }

    /* AFK prompt takes priority over every other popup. */
    if (g->afk == AFK_PROMPT) {
        if (cur != OVL_AFK) overlay_open(OVL_AFK);
        int secs = (int)ceilf(g->afk_left > 0.0 ? (float)g->afk_left : 0.0f);
        if (secs != tick_last && secs >= 1 && secs <= OVL_TICK_SECONDS) audio_play(SFX_TICK);
        tick_last = secs;
        return true;
    }
    if (cur == OVL_AFK) overlay_close();

    /* PvP undo request: opponent must answer. */
    if (g->undo_requester >= 0) {
        if (cur != OVL_UNDO_REQUEST) overlay_open(OVL_UNDO_REQUEST);
    } else if (cur == OVL_UNDO_REQUEST) {
        overlay_close();
    }

    /* Esc: back out of the popup; never dismisses AFK / game over. */
    if (cur != OVL_NONE && input_pressed(ACT_BACK, -1)) {
        switch (cur) {
        case OVL_PAUSE:
            overlay_close();
            audio_play(SFX_BACK);
            break;
        case OVL_SURRENDER_CONFIRM:
        case OVL_LEAVE_CONFIRM:
            set_overlay(OVL_PAUSE);
            audio_play(SFX_BACK);
            break;
        case OVL_UNDO_REQUEST:
            game_answer_undo(g, false);
            app_toast(tr(S_UNDO_DECLINED));
            overlay_close();
            audio_play(SFX_BACK);
            break;
        default:
            break;
        }
    }
    return cur != OVL_NONE;
}

void overlay_draw(void)
{
    if (cur == OVL_NONE) return;
    Game *g = &g_app.game;
    OvlAction act = OA_NONE;
    float a = ease_out_cubic(appear);

    ui_frame_begin();
    ui_focus_scope(1);
    ui_dim_screen(0.55f * a);
    switch (cur) {
    case OVL_PAUSE:
        act = draw_pause(ui_popup(600, 520, tr(S_PAUSED), a), g);
        break;
    case OVL_UNDO_REQUEST:
        act = draw_undo_request(ui_popup(600, 440, tr(S_UNDO), a), g);
        break;
    case OVL_SURRENDER_CONFIRM:
        act = draw_surrender(ui_popup(560, 420, tr(S_SURRENDER), a), g);
        break;
    case OVL_LEAVE_CONFIRM:
        act = draw_leave(ui_popup(560, 340, tr(S_HOME), a));
        break;
    case OVL_AFK:
        act = draw_afk(ui_popup(600, 460, tr(S_AFK_TITLE), a), g);
        break;
    case OVL_GAME_OVER:
        act = draw_game_over(ui_popup(640, 520, tr(S_GAME_OVER), a), g);
        break;
    default:
        break;
    }
    ui_frame_end();
    ui_focus_scope(0);

    apply_action(act);
}
