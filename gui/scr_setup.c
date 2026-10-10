/* scr_setup.c - New Game setup (SCENE_SETUP, BG_MENU): mode, board size, difficulty, blocked-ends
 * rule. Layout follows docs/DESIGN.md section 5 "New Game setup". Choices live in
 * g_app.settings.last_mode / last_board / last_diff / block_rule. */
#include <stdbool.h>
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

#define PANEL_X      260
#define PANEL_Y      16
#define PANEL_W      760
#define PANEL_H      648
#define CONTENT_X    (PANEL_X + SP_3)        /* 284 */
#define CONTENT_W    (PANEL_W - 2 * SP_3)    /* 712 */
#define LABEL_H      24
#define MODE_Y       104
#define MODE_H       120
#define BOARD_LABEL_Y 240
#define BOARD_Y      264
#define BOARD_H      144
#define BOARD_W      224
#define BOARD_GAP    20                      /* 3*224 + 2*20 = 712 */
#define MODE_W       348
#define ROW_DIFF_Y   424
#define ROW_RULE_Y   488
#define ROW_H        56
#define RULE_DESC_Y  548
#define BTN_Y        592
#define BTN_W        200
#define CARD_PAD     SP_2

/* Board card swatches hint each board's background theme (art.c BG_BOARD3/5/10). */
static const Color board_swatch[3][4] = {
    { { 120, 40, 28, 255 }, { 168, 58, 34, 255 }, { 255, 90, 31, 255 }, { 255, 177, 59, 255 } },  /* Fire Shrine */
    { { 30, 60, 25, 255 },  { 58, 96, 40, 255 },  { 92, 122, 46, 255 }, { 140, 176, 72, 255 } },  /* Forest Temple */
    { { 20, 40, 70, 255 },  { 36, 70, 110, 255 }, { 47, 168, 255, 255 }, { 155, 231, 255, 255 } } /* Water Sanctum */
};

static void card_selected_check(Rectangle r, bool selected)
{
    if (!selected) return;
    DrawRectangleLinesEx(r, 3, COL_GOLD);
    /* check badge: gold tile with a dark tick, sitting on the card's top-right corner */
    float x = r.x + r.width - 20, y = r.y - 12;
    DrawRectangle((int)x, (int)y, 24, 24, COL_GOLD);
    DrawLineEx((Vector2){ x + 5, y + 12 }, (Vector2){ x + 10, y + 18 }, 3, COL_STONE_900);
    DrawLineEx((Vector2){ x + 10, y + 18 }, (Vector2){ x + 19, y + 6 }, 3, COL_STONE_900);
}

/* Dim a row that is shown but not available (e.g. difficulty in 2-player mode). */
static void dim_rect(Rectangle r) { DrawRectangleRec(r, Fade(COL_STONE_900, 0.55f)); }

/* Card content is drawn after the card's ui_button so it sits above the widget's fill. */
static void draw_mode_card(Rectangle r, GameMode mode, bool selected, float t, bool animate)
{
    if (mode == MODE_PVE) {
        art_draw_mascot(MASCOT_ROBOT, (Rectangle){ r.x + CARD_PAD + 16, r.y + (r.height - 96) / 2, 96, 96 }, t, animate);
    } else {
        art_draw_mascot(MASCOT_FIRE, (Rectangle){ r.x + CARD_PAD, r.y + (r.height - 64) / 2, 64, 64 }, t, animate);
        art_draw_mascot(MASCOT_WATER, (Rectangle){ r.x + CARD_PAD + 64, r.y + (r.height - 64) / 2, 64, 64 }, t, animate);
    }
    float tx = r.x + CARD_PAD + 128 + SP_2;
    float tw = r.x + r.width - CARD_PAD - tx;
    text_draw(tr(mode == MODE_PVE ? S_MODE_PVE : S_MODE_PVP), tx, r.y + CARD_PAD, FS_BODY,
              selected ? COL_GOLD : COL_TEXT);
    text_draw_wrapped(tr(mode == MODE_PVE ? S_MODE_PVE_DESC : S_MODE_PVP_DESC),
                      (Rectangle){ tx, r.y + CARD_PAD + FS_BODY + SP_1, tw, r.height - 2 * CARD_PAD - FS_BODY - SP_1 },
                      FS_SMALL, COL_TEXT_DIM, 0);
    card_selected_check(r, selected);
}

/* Board card, three lines: big "3 x 3", theme name, then the win rule. The description string is
 * "Theme - rule" in every language, so it is split at the first " - ". */
static void draw_board_card(Rectangle r, int idx, bool selected)
{
    float inner_w = r.width - 2 * CARD_PAD;
    const float swatch_h = 16;
    for (int i = 0; i < 4; i++) {
        DrawRectangleRec((Rectangle){ r.x + CARD_PAD + i * inner_w / 4, r.y + CARD_PAD, inner_w / 4, swatch_h },
                         board_swatch[idx][i]);
    }
    DrawRectangleLinesEx((Rectangle){ r.x + CARD_PAD, r.y + CARD_PAD, inner_w, swatch_h }, 1, COL_STONE_900);
    StrId title = idx == 0 ? S_BOARD_3 : idx == 1 ? S_BOARD_5 : S_BOARD_10;
    StrId desc = idx == 0 ? S_BOARD_3_DESC : idx == 1 ? S_BOARD_5_DESC : S_BOARD_10_DESC;

    char theme[64], rule[64];
    const char *d = tr(desc);
    const char *sep = strstr(d, " - ");
    if (sep) {
        size_t n = (size_t)(sep - d);
        if (n >= sizeof theme) n = sizeof theme - 1;
        memcpy(theme, d, n);
        theme[n] = '\0';
        snprintf(rule, sizeof rule, "%s", sep + 3);
    } else {
        snprintf(theme, sizeof theme, "%s", d);
        rule[0] = '\0';
    }

    float ty = r.y + CARD_PAD + swatch_h + SP_1;
    text_draw(tr(title), r.x + CARD_PAD, ty, FS_BODY, selected ? COL_GOLD : COL_TEXT);
    text_draw(theme, r.x + CARD_PAD, ty + FS_BODY + 4, FS_SMALL, COL_TEXT);
    text_draw(rule, r.x + CARD_PAD, ty + FS_BODY + 4 + FS_SMALL + 2, FS_SMALL, COL_TEXT_DIM);
    card_selected_check(r, selected);
}

static void enter(void)
{
    ui_focus_scope(0);
    ui_focus_reset();
    audio_music(BGM_MENU);
}

static void update(float dt)
{
    (void)dt;
    /* While typing in a text box Esc/Home belong to the box; setup has none, so only navigate. */
    if (input_pressed(ACT_BACK, -1)) {
        audio_play(SFX_BACK);
        scene_pop();
        return;
    }
    if (input_pressed(ACT_HOME, -1)) scene_goto(SCENE_HOME);
}

static void draw(void)
{
    Settings *s = &g_app.settings;
    float t = g_app.time;
    bool animate = !s->reduced_motion;
    bool pve = s->last_mode == MODE_PVE;
    bool board3 = s->last_board == 3;

    art_draw_background(BG_MENU, t);
    ui_frame_begin();
    ui_focus_scope(0);
    ui_panel_titled((Rectangle){ PANEL_X, PANEL_Y, PANEL_W, PANEL_H }, tr(S_NEW_GAME));

    /* Mode */
    text_draw(tr(S_MODE), CONTENT_X, 80, FS_SMALL, COL_GOLD);
    Rectangle mode_r[2] = {
        { CONTENT_X, MODE_Y, MODE_W, MODE_H },
        { CONTENT_X + MODE_W + SP_2, MODE_Y, MODE_W, MODE_H },
    };
    for (int i = 0; i < 2; i++) {
        GameMode m = i == 0 ? MODE_PVE : MODE_PVP;
        bool sel = s->last_mode == m;
        ui_panel(mode_r[i]);
        bool clicked = ui_button(mode_r[i], "", BTN_GHOST);
        draw_mode_card(mode_r[i], m, sel, t, animate);
        if (clicked) s->last_mode = m;
    }

    /* Board */
    text_draw(tr(S_BOARD_SIZE), CONTENT_X, BOARD_LABEL_Y, FS_SMALL, COL_GOLD);
    const int boards[3] = { 3, 5, 10 };
    for (int i = 0; i < 3; i++) {
        Rectangle r = { CONTENT_X + i * (BOARD_W + BOARD_GAP), BOARD_Y, BOARD_W, BOARD_H };
        bool sel = s->last_board == boards[i];
        ui_panel(r);
        bool clicked = ui_button(r, "", BTN_GHOST);
        draw_board_card(r, i, sel);
        if (clicked) s->last_board = boards[i];
    }

    /* Difficulty: only meaningful against the computer; shown dimmed otherwise. */
    {
        const char *opts[3] = { tr(S_DIFF_EASY), tr(S_DIFF_MEDIUM), tr(S_DIFF_HARD) };
        int sel = (int)s->last_diff;
        Rectangle r = { CONTENT_X, ROW_DIFF_Y, CONTENT_W, ROW_H };
        ui_set_interactive(pve);
        if (ui_choice(r, tr(S_DIFFICULTY), opts, 3, &sel)) s->last_diff = (Difficulty)sel;
        ui_set_interactive(true);
        if (!pve) dim_rect(r);
    }

    /* Blocked-ends rule: only for 5x5 and 10x10. */
    {
        Rectangle r = { CONTENT_X, ROW_RULE_Y, CONTENT_W, ROW_H };
        ui_set_interactive(!board3);
        ui_toggle(r, tr(S_RULE_BLOCKED), &s->block_rule);
        ui_set_interactive(true);
        if (board3) dim_rect(r);
        text_draw_wrapped(tr(S_RULE_BLOCKED_DESC), (Rectangle){ CONTENT_X, RULE_DESC_Y, CONTENT_W, LABEL_H },
                          FS_SMALL, board3 ? Fade(COL_TEXT_DIM, 0.5f) : COL_TEXT_DIM, 0);
    }

    /* Navigation */
    bool back = ui_button((Rectangle){ CONTENT_X, BTN_Y, BTN_W, ROW_H }, tr(S_BACK), BTN_NORMAL);
    bool next = ui_button((Rectangle){ CONTENT_X + CONTENT_W - BTN_W, BTN_Y, BTN_W, ROW_H }, tr(S_NEXT), BTN_PRIMARY);
    if (back) {
        audio_play(SFX_BACK);
        scene_pop();
    } else if (next) {
        app_save_settings();
        scene_push(SCENE_PROFILE);
    }

    ui_hint_bar(tr(S_HINT_MENU));
    ui_frame_end();
}

void scr_setup_register(void) { scene_register(SCENE_SETUP, (Scene){ enter, 0, update, draw }); }
