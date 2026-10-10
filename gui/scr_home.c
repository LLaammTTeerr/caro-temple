/* scr_home.c - Home screen (SCENE_HOME, BG_HOME): title, main menu, quit confirmation.
 * Layout and behaviour follow docs/DESIGN.md section 5 "Home". */
#include <stdbool.h>
#include "screens.h"
#include "scene.h"
#include "ui.h"
#include "theme.h"
#include "art.h"
#include "audio.h"
#include "input.h"
#include "app.h"
#include "../core/storage.h"
#include "../core/i18n.h"

#define MENU_W        360
#define MENU_H        60
#define MENU_GAP      SP_2
#define MENU_Y        260
#define MENU_ITEMS    5
#define MASCOT_PX     176
#define MASCOT_Y      480
#define STAGGER_S     0.04f   /* delay between consecutive menu buttons */
#define ENTER_ANIM_S  0.25f   /* slide/fade-in length per button */
#define SLIDE_PX      48.0f
#define POPUP_ANIM_S  0.18f
#define POPUP_W       520
#define POPUP_H       240
#define POPUP_BTN_W   200
#define POPUP_BTN_GAP SP_3

/* Menu indices (focus order). Continue is not focusable while disabled. */
enum { MI_PLAY = 0, MI_CONTINUE, MI_LOAD, MI_SETTINGS, MI_QUIT };

/* Navigation requested from draw(); performed on the next update() so scene changes never
 * happen in the middle of drawing. */
typedef enum { HA_NONE = 0, HA_PLAY, HA_CONTINUE, HA_LOAD, HA_SETTINGS } HomeAction;

static float enter_t;        /* seconds since enter(), drives the staggered entrance */
static bool can_continue;    /* a match is in memory or a save slot exists (cached on enter) */
static bool quit_open;       /* quit confirmation popup visible */
static bool popup_fresh;     /* first frame of the popup: focus "No" */
static float popup_t;        /* popup appear progress 0..1 */
static HomeAction pending;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static Rectangle menu_rect(int i)
{
    return (Rectangle){ (VIRTUAL_W - MENU_W) / 2.0f, MENU_Y + i * (MENU_H + MENU_GAP), MENU_W, MENU_H };
}

/* 0..1 eased entrance progress for menu item i. */
static float entrance(int i)
{
    if (g_app.settings.reduced_motion) return 1.0f;
    return ease_out_cubic(clampf((enter_t - i * STAGGER_S) / ENTER_ANIM_S, 0.0f, 1.0f));
}

/* Button slides in from the right while fading up out of the background colour. */
static bool menu_item(int i, const char *label, ButtonStyle style)
{
    float e = entrance(i);
    Rectangle r = menu_rect(i);
    r.x += (1.0f - e) * SLIDE_PX;
    bool activated = ui_button(r, label, style);
    if (e < 1.0f) DrawRectangleRec(r, Fade(COL_STONE_900, 1.0f - e));
    return activated && e >= 1.0f;
}

static void open_quit(void)
{
    quit_open = true;
    popup_fresh = true;
    popup_t = g_app.settings.reduced_motion ? 1.0f : 0.0f;
    audio_play(SFX_POPUP);
}

static void close_quit(void)
{
    quit_open = false;
    audio_play(SFX_BACK);
}

static void enter(void)
{
    enter_t = 0.0f;
    quit_open = false;
    popup_fresh = false;
    popup_t = 0.0f;
    pending = HA_NONE;
    can_continue = g_app.has_game || storage_latest_slot() >= 0;
    ui_focus_scope(0);
    ui_focus_reset();
    audio_music(BGM_MENU);
}

static void update(float dt)
{
    enter_t += dt;
    if (quit_open) {
        if (!g_app.settings.reduced_motion) popup_t = clampf(popup_t + dt / POPUP_ANIM_S, 0.0f, 1.0f);
        else popup_t = 1.0f;
        if (input_pressed(ACT_BACK, -1)) close_quit();
        return;
    }
    switch (pending) {
    case HA_PLAY: scene_push(SCENE_SETUP); break;
    case HA_CONTINUE: {
        if (g_app.has_game) scene_goto(SCENE_GAME);
        else {
            int slot = storage_latest_slot();
            if (slot >= 0) app_load_slot(slot);
        }
        break;
    }
    case HA_LOAD: g_app.saves_mode = SAVES_LOAD; scene_push(SCENE_SAVES); break;
    case HA_SETTINGS: scene_push(SCENE_SETTINGS); break;
    default: break;
    }
    pending = HA_NONE;
    if (scene_current() != SCENE_HOME) return;
    if (input_pressed(ACT_BACK, -1)) open_quit();
}

static void draw_quit_popup(void)
{
    float ap = popup_t;
    ui_dim_screen(0.6f * ap);
    ui_focus_scope(1);
    ui_set_interactive(true);
    Rectangle c = ui_popup(POPUP_W, POPUP_H, tr(S_QUIT), ap);
    if (popup_fresh) {
        ui_focus_set(1);   /* default to "No" */
        popup_fresh = false;
    }
    text_draw_aligned(tr(S_QUIT_Q), c.x + c.width / 2, c.y + SP_1, FS_BODY, COL_TEXT, 1);
    float btn_y = c.y + c.height - 56;
    float x0 = c.x + (c.width - (2 * POPUP_BTN_W + POPUP_BTN_GAP)) / 2;
    if (ui_button((Rectangle){x0, btn_y, POPUP_BTN_W, 56}, tr(S_YES), BTN_DANGER)) {
        g_app.quit = true;
    }
    if (ui_button((Rectangle){x0 + POPUP_BTN_W + POPUP_BTN_GAP, btn_y, POPUP_BTN_W, 56}, tr(S_NO), BTN_NORMAL)) {
        close_quit();
    }
    ui_focus_scope(0);
}

static void draw(void)
{
    float t = g_app.time;
    bool popup = quit_open;
    bool animate = !g_app.settings.reduced_motion;

    art_draw_background(BG_HOME, t);
    ui_frame_begin();
    ui_focus_scope(0);

    art_draw_title(VIRTUAL_W / 2.0f, 64, 1, t);
    text_draw_shadow(tr(S_GAME_SUBTITLE), VIRTUAL_W / 2.0f - text_measure(tr(S_GAME_SUBTITLE), FS_BODY).x / 2,
                     176, FS_BODY, COL_GOLD_DARK);
    art_draw_mascot(MASCOT_FIRE, (Rectangle){ 120, MASCOT_Y, MASCOT_PX, MASCOT_PX }, t, animate);
    art_draw_mascot(MASCOT_WATER, (Rectangle){ VIRTUAL_W - 120 - MASCOT_PX, MASCOT_Y, MASCOT_PX, MASCOT_PX }, t, animate);

    /* With the popup open the menu behind it is visible but not interactive. */
    ui_set_interactive(!popup);
    if (menu_item(MI_PLAY, tr(S_PLAY), BTN_PRIMARY)) pending = HA_PLAY;

    if (can_continue) {
        if (menu_item(MI_CONTINUE, tr(S_CONTINUE), BTN_NORMAL)) pending = HA_CONTINUE;
    } else {
        Rectangle r = menu_rect(MI_CONTINUE);
        r.x += (1.0f - entrance(MI_CONTINUE)) * SLIDE_PX;
        DrawRectangleRec(r, Fade(COL_STONE_900, 0.7f));
        DrawRectangleLinesEx(r, 2, Fade(COL_STONE_700, 0.7f));
        text_draw_aligned(tr(S_CONTINUE), r.x + r.width / 2, r.y + (r.height - FS_BODY) / 2,
                          FS_BODY, Fade(COL_TEXT_DIM, 0.45f), 1);
    }

    if (menu_item(MI_LOAD, tr(S_LOAD_GAME), BTN_NORMAL)) pending = HA_LOAD;
    if (menu_item(MI_SETTINGS, tr(S_SETTINGS), BTN_NORMAL)) pending = HA_SETTINGS;
    if (menu_item(MI_QUIT, tr(S_QUIT), BTN_DANGER)) open_quit();
    ui_set_interactive(true);

    ui_hint_bar(tr(S_HINT_MENU));
    if (popup) draw_quit_popup();
    ui_frame_end();
}

void scr_home_register(void) { scene_register(SCENE_HOME, (Scene){ enter, 0, update, draw }); }
