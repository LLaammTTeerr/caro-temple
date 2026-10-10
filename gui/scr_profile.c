/* scr_profile.c - Player names and avatars before a match (SCENE_PROFILE, BG_MENU).
 * PvE: one card (Player 1, plays Fire). PvP: two cards (P1 Fire, P2 Water).
 * Layout follows docs/DESIGN.md section 5 "Profile". */
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
#include "../core/game.h"
#include "../core/i18n.h"

#define CARD_W        400
#define CARD_H        472
#define CARD_Y        112
#define CARD_GAP      48
#define INNER_X       20          /* card padding for controls */
#define INNER_W       360
#define AVATAR_PX     160
#define AVATAR_Y      96
#define AVATAR_CHOICE_Y 272
#define NAME_LABEL_Y  344
#define NAME_BOX_Y    368
#define NAME_ERR_Y    432
#define NAME_MAX_CP   16          /* visible characters */
#define BTN_Y         608
#define BTN_W         200
#define ROW_CHOICE_H  56
#define TITLE_Y       40
#define BTN_GAP       SP_3

static const char *const avatar_opts[AVATAR_COUNT] = { "1/6", "2/6", "3/6", "4/6", "5/6", "6/6" };

/* Editable copies of the two names/avatars; written to settings only on a valid Start. */
static char names[2][PLAYER_NAME_MAX];
static int avatars[2];
static bool show_err[2];   /* empty name was attempted: show the inline error until fixed */

static bool is_blank(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return *s == '\0';
}

/* Copy src without leading/trailing blanks. */
static void trimmed_copy(char *dst, size_t n, const char *src)
{
    while (*src == ' ' || *src == '\t') src++;
    size_t len = strlen(src);
    if (len >= n) len = n - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
    while (len > 0 && (dst[len - 1] == ' ' || dst[len - 1] == '\t')) dst[--len] = '\0';
}

/* Keep at most max_cp UTF-8 code points; cuts only at a code point boundary. */
static void name_clamp(char *s, int max_cp)
{
    int cp = 0;
    for (size_t i = 0; s[i] != '\0'; i++) {
        if (((unsigned char)s[i] & 0xC0) != 0x80) {
            if (cp == max_cp) { s[i] = '\0'; return; }
            cp++;
        }
    }
}

static void draw_card(int p, float x, float t)
{
    Rectangle r = { x, CARD_Y, CARD_W, CARD_H };
    Color tint = p == 0 ? COL_FIRE : COL_WATER;
    ui_panel(r);
    DrawRectangleLinesEx(r, 3, tint);

    text_draw_aligned(tr(p == 0 ? S_PLAYER_1 : S_PLAYER_2), r.x + r.width / 2, r.y + SP_2, FS_H2, COL_GOLD, 1);
    text_draw_aligned(tr(p == 0 ? S_PLAYS_FIRE : S_PLAYS_WATER), r.x + r.width / 2, r.y + SP_2 + FS_H2 + 4,
                      FS_SMALL, tint, 1);

    art_draw_avatar(avatars[p], (Rectangle){ r.x + (r.width - AVATAR_PX) / 2, r.y + AVATAR_Y, AVATAR_PX, AVATAR_PX }, t);

    ui_choice((Rectangle){ r.x + INNER_X, r.y + AVATAR_CHOICE_Y, INNER_W, ROW_CHOICE_H }, tr(S_AVATAR),
              avatar_opts, AVATAR_COUNT, &avatars[p]);

    text_draw(tr(S_NAME), r.x + INNER_X, r.y + NAME_LABEL_Y, FS_SMALL, COL_TEXT_DIM);
    ui_textbox((Rectangle){ r.x + INNER_X, r.y + NAME_BOX_Y, INNER_W, ROW_CHOICE_H }, names[p], PLAYER_NAME_MAX,
               tr(S_ENTER_NAME));
    name_clamp(names[p], NAME_MAX_CP);

    if (show_err[p] && is_blank(names[p])) {
        text_draw(tr(S_NAME_EMPTY), r.x + INNER_X, r.y + NAME_ERR_Y, FS_SMALL, COL_DANGER);
    }
}

static void enter(void)
{
    Settings *s = &g_app.settings;
    for (int p = 0; p < 2; p++) {
        snprintf(names[p], sizeof names[p], "%s", s->profile[p].name);
        avatars[p] = s->profile[p].avatar;
        if (avatars[p] < 0 || avatars[p] >= AVATAR_COUNT) avatars[p] = 0;
        show_err[p] = false;
    }
    ui_focus_scope(0);
    ui_focus_reset();
    audio_music(BGM_MENU);
}

/* Start: validate, store the names/avatars in settings, save and begin the match. */
static void try_start(bool pve)
{
    int players = pve ? 1 : 2;
    bool ok = true;
    for (int p = 0; p < players; p++) {
        if (is_blank(names[p])) {
            show_err[p] = true;
            ok = false;
        }
    }
    if (!ok) {
        audio_play(SFX_ERROR);
        return;
    }
    Settings *s = &g_app.settings;
    for (int p = 0; p < players; p++) {
        trimmed_copy(s->profile[p].name, sizeof s->profile[p].name, names[p]);
        name_clamp(s->profile[p].name, NAME_MAX_CP);
        s->profile[p].avatar = avatars[p];
    }
    app_save_settings();
    app_start_new_game();
}

static void update(float dt)
{
    (void)dt;
    /* While a name is being typed, Esc/Enter belong to the text box (it handles them in draw). */
    if (ui_textbox_editing()) return;
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
    bool pve = s->last_mode == MODE_PVE;

    art_draw_background(BG_MENU, t);
    ui_frame_begin();
    ui_focus_scope(0);

    text_draw_aligned(tr(S_PROFILE_TITLE), VIRTUAL_W / 2.0f, TITLE_Y, FS_H1, COL_GOLD, 1);

    if (pve) {
        draw_card(0, (VIRTUAL_W - CARD_W) / 2.0f, t);
    } else {
        float x1 = (VIRTUAL_W - 2 * CARD_W - CARD_GAP) / 2.0f;
        draw_card(0, x1, t);
        draw_card(1, x1 + CARD_W + CARD_GAP, t);
    }

    float btn_x0 = (VIRTUAL_W - (2 * BTN_W + BTN_GAP)) / 2.0f;
    bool back = ui_button((Rectangle){ btn_x0, BTN_Y, BTN_W, ROW_CHOICE_H }, tr(S_BACK), BTN_NORMAL);
    bool start = ui_button((Rectangle){ btn_x0 + BTN_W + BTN_GAP, BTN_Y, BTN_W, ROW_CHOICE_H }, tr(S_START), BTN_PRIMARY);
    if (back) {
        audio_play(SFX_BACK);
        scene_pop();
    } else if (start) {
        try_start(pve);
    }

    ui_hint_bar(ui_textbox_editing() ? tr(S_HINT_TEXT) : tr(S_HINT_MENU));
    ui_frame_end();
}

void scr_profile_register(void) { scene_register(SCENE_PROFILE, (Scene){ enter, 0, update, draw }); }
