/* scr_settings.c - Settings screen (SCENE_SETTINGS, pushed over Home or Game).
 * Left panel: language, music / sound FX volumes and mutes, reduced motion, fullscreen.
 * Right panel: controls reference drawn with pixel key caps. */
#include <stddef.h>
#include "screens.h"
#include "scene.h"
#include "ui.h"
#include "theme.h"
#include "art.h"
#include "input.h"
#include "audio.h"
#include "app.h"
#include "../core/i18n.h"

#define PANEL_Y      96
#define PANEL_H      456
#define PANEL_W      552
#define LEFT_X       64
#define RIGHT_X      664
#define ROW_H        48    /* settings row height */
#define ROW_STEP     56    /* settings row pitch (height + 8 gap) */
#define KEY_H        36    /* key cap height */
#define CTRL_STEP    72    /* controls row pitch */
#define PREVIEW_STEP 0.125f /* SFX preview throttle: 8 per second */
#define BACK_Y       576   /* Back button: below both panels, above the hint bar (y 672) */

static const char *const LANG_OPTIONS[LANG_COUNT] = { "English", "Ti\xe1\xbb\x87ng Vi\xe1\xbb\x87t" };

static float preview_next; /* earliest g_app.time for the next SFX preview */

static void settings_enter(void)
{
    ui_focus_scope(0);
    ui_focus_reset();
    preview_next = 0.0f;
}

static void go_back(void)
{
    audio_play(SFX_BACK);
    if (scene_previous() == SCENE_COUNT) scene_goto(SCENE_HOME);
    else scene_pop();
}

static void go_home(void)
{
    /* PRD: Home always returns to the main menu. A match in progress stays in memory, so the
     * Home screen's Continue resumes it. */
    scene_goto(SCENE_HOME);
}

static void draw_backdrop(void)
{
    if (scene_previous() == SCENE_GAME && g_app.has_game) {
        art_draw_background(art_board_bg(g_app.game.board.n), g_app.time);
        ui_dim_screen(0.6f);
    } else {
        art_draw_background(BG_MENU, g_app.time);
    }
}

/* ---- pixel key caps ---- */

static float key_w(const char *label)
{
    float w = text_measure(label, FS_SMALL).x + SP_2 + 8;
    return w < 48 ? 48 : w;
}

static void key_face(float x, float y, float w)
{
    DrawRectangle((int)x, (int)y + 3, (int)w, KEY_H, COL_STONE_900);  /* lower edge = key depth */
    DrawRectangle((int)x, (int)y, (int)w, KEY_H, COL_STONE_700);
    DrawRectangleLinesEx((Rectangle){x, y, w, KEY_H}, 2, COL_STONE_400);
}

/* Draws a labelled key cap; returns the x advance including the gap. */
static float key_draw(float x, float y, const char *label)
{
    float w = key_w(label);
    key_face(x, y, w);
    text_draw_aligned(label, x + w / 2, y + (KEY_H - FS_SMALL) / 2, FS_SMALL, COL_TEXT, 1);
    return w + SP_1;
}

static void tri(Vector2 a, Vector2 b, Vector2 c, Color col)
{
    float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross > 0) { Vector2 t = b; b = c; c = t; }   /* raylib wants counter-clockwise */
    DrawTriangle(a, b, c, col);
}

/* Arrow key cap (48 px wide). dir: 0 up, 1 down, 2 left, 3 right. */
static float key_arrow(float x, float y, int dir)
{
    const float w = 48;
    key_face(x, y, w);
    float cx = x + w / 2, cy = y + KEY_H / 2;
    switch (dir) {
    case 0: tri((Vector2){cx, cy - 8}, (Vector2){cx - 8, cy + 5}, (Vector2){cx + 8, cy + 5}, COL_TEXT); break;
    case 1: tri((Vector2){cx, cy + 8}, (Vector2){cx + 8, cy - 5}, (Vector2){cx - 8, cy - 5}, COL_TEXT); break;
    case 2: tri((Vector2){cx - 8, cy}, (Vector2){cx + 5, cy - 8}, (Vector2){cx + 5, cy + 8}, COL_TEXT); break;
    default: tri((Vector2){cx + 8, cy}, (Vector2){cx - 5, cy + 8}, (Vector2){cx - 5, cy - 8}, COL_TEXT); break;
    }
    return w + SP_1;
}

/* ---- panels ---- */

static void draw_settings_panel(Rectangle p)
{
    Settings *s = &g_app.settings;
    bool changed = false, preview = false;
    ui_panel_titled(p, tr(S_SETTINGS));

    float x = p.x + SP_4, w = p.width - 2 * SP_4;
    float y = p.y + SP_6 + SP_2 - SP_1; /* first row below the panel title */

    int lang = (int)s->lang;
    if (ui_choice((Rectangle){x, y, w, ROW_H}, tr(S_LANGUAGE), LANG_OPTIONS, LANG_COUNT, &lang)) {
        s->lang = (Lang)lang;
        changed = true;
    }
    y += ROW_STEP;

    if (ui_slider((Rectangle){x, y, w, ROW_H}, tr(S_MUSIC), &s->bgm_volume)) changed = true;
    y += ROW_STEP;
    if (ui_toggle((Rectangle){x + SP_4, y, w - SP_4, ROW_H}, tr(S_MUTE), &s->bgm_muted)) changed = true;
    y += ROW_STEP;

    if (ui_slider((Rectangle){x, y, w, ROW_H}, tr(S_SOUND_FX), &s->sfx_volume)) {
        changed = true;
        preview = true;
    }
    y += ROW_STEP;
    if (ui_toggle((Rectangle){x + SP_4, y, w - SP_4, ROW_H}, tr(S_MUTE), &s->sfx_muted)) changed = true;
    y += ROW_STEP;

    if (ui_toggle((Rectangle){x, y, w, ROW_H}, tr(S_REDUCED_MOTION), &s->reduced_motion)) changed = true;
    y += ROW_STEP;
    if (ui_toggle((Rectangle){x, y, w, ROW_H}, tr(S_FULLSCREEN), &s->fullscreen)) {
        ToggleBorderlessWindowed();   /* s->fullscreen already holds the new value */
        changed = true;
    }

    /* Apply first so the preview plays at the new volume. */
    if (changed) app_save_settings();
    if (preview && g_app.time >= preview_next) {
        audio_play(SFX_CLICK);
        preview_next = g_app.time + PREVIEW_STEP;
    }
}

static void draw_controls_panel(Rectangle p)
{
    ui_panel_titled(p, tr(S_CONTROLS));
    float x = p.x + SP_4;
    float y = p.y + SP_6 + SP_2 - SP_1;
    float kx;

    /* Mouse */
    key_draw(x, y, "LMB");
    text_draw(tr(S_CTRL_MOUSE), x, y + KEY_H + SP_1, FS_SMALL, COL_TEXT);
    y += CTRL_STEP;

    /* Player 1 */
    kx = x;
    kx += key_draw(kx, y, "W");
    kx += key_draw(kx, y, "A");
    kx += key_draw(kx, y, "S");
    kx += key_draw(kx, y, "D");
    key_draw(kx + SP_1, y, "SPACE");
    text_draw(tr(S_CTRL_P1), x, y + KEY_H + SP_1, FS_SMALL, COL_TEXT);
    y += CTRL_STEP;

    /* Player 2 */
    kx = x;
    kx += key_arrow(kx, y, 0);
    kx += key_arrow(kx, y, 1);
    kx += key_arrow(kx, y, 2);
    kx += key_arrow(kx, y, 3);
    key_draw(kx + SP_1, y, "ENTER");
    text_draw(tr(S_CTRL_P2), x, y + KEY_H + SP_1, FS_SMALL, COL_TEXT);
    y += CTRL_STEP;

    /* Menus */
    kx = x;
    kx += key_draw(kx, y, "ESC");
    key_draw(kx + SP_1, y, "HOME");
    text_draw(tr(S_CTRL_MENU), x, y + KEY_H + SP_1, FS_SMALL, COL_TEXT);
    y += CTRL_STEP;

    /* Gamepad: two description lines */
    kx = x;
    kx += key_draw(kx, y, "D-PAD");
    kx += key_draw(kx, y, "A");
    kx += key_draw(kx, y, "B");
    key_draw(kx, y, "START");
    text_draw(tr(S_CTRL_PAD_GAME), x, y + KEY_H + SP_1, FS_SMALL, COL_TEXT);
    text_draw(tr(S_CTRL_PAD_MENU), x, y + KEY_H + SP_1 + 24, FS_SMALL, COL_TEXT);
}

static void settings_update(float dt)
{
    (void)dt;
    if (input_pressed(ACT_HOME, -1)) { go_home(); return; }
    if (input_pressed(ACT_BACK, -1)) go_back();
}

static void settings_draw(void)
{
    draw_backdrop();
    ui_frame_begin();
    ui_focus_scope(0);

    draw_settings_panel((Rectangle){LEFT_X, PANEL_Y, PANEL_W, PANEL_H});
    draw_controls_panel((Rectangle){RIGHT_X, PANEL_Y, PANEL_W, PANEL_H});

    if (ui_button((Rectangle){LEFT_X, BACK_Y, 200, 56}, tr(S_BACK), BTN_NORMAL)) go_back();
    if (ui_home_button()) go_home();
    ui_hint_bar(tr(S_HINT_MENU));
    ui_frame_end();
}

void scr_settings_register(void)
{
    scene_register(SCENE_SETTINGS, (Scene){ settings_enter, NULL, settings_update, settings_draw });
}
