/* scr_saves.c - Load / Save Game screen (SCENE_SAVES, pushed over Home or Game).
 * 10 slot cards in a 2 x 5 grid. Each card: an interactive ghost button over the card body and
 * a small Delete button on the right of used cards. Overwrite / delete confirm in a popup. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "screens.h"
#include "scene.h"
#include "ui.h"
#include "theme.h"
#include "art.h"
#include "input.h"
#include "audio.h"
#include "app.h"
#include "../core/i18n.h"

#define CARD_W     560
#define CARD_H     96
#define CARD_GAP   16
#define GRID_X     72     /* (1280 - (2 * 560 + 16)) / 2 */
#define GRID_Y     104
#define DEL_W      112
#define DEL_H      40
#define DEL_RIGHT  16     /* gap between Delete button and card edge */
#define POP_W      600
#define POP_H      260
#define POP_FADE   0.18f

typedef enum { POP_NONE = 0, POP_OVERWRITE, POP_DELETE } PopupKind;

static SlotInfo slots[SAVE_SLOTS];
static PopupKind popup_kind;
static int popup_slot;
static float popup_t0;

static void refresh(void) { storage_list_slots(slots); }

static void go_back(void)
{
    audio_play(SFX_BACK);
    if (scene_previous() == SCENE_COUNT) scene_goto(SCENE_HOME);
    else scene_pop();
}

static void open_popup(PopupKind kind, int slot)
{
    popup_kind = kind;
    popup_slot = slot;
    popup_t0 = g_app.time;
    ui_focus_scope(1);
    ui_focus_reset();      /* default to "No" (the first popup button) */
    ui_focus_scope(0);
    audio_play(SFX_POPUP);
}

static void close_popup(void) { popup_kind = POP_NONE; }

static void draw_backdrop(void)
{
    if (scene_previous() == SCENE_GAME && g_app.has_game) {
        art_draw_background(art_board_bg(g_app.game.board.n), g_app.time);
        ui_dim_screen(0.6f);
    } else {
        art_draw_background(BG_MENU, g_app.time);
    }
}

/* Truncates src with "..." so it fits max_w at the small size; UTF-8 safe. */
static void fit_text(char *buf, size_t cap, const char *src, float max_w)
{
    char tmp[128];
    snprintf(buf, cap, "%s", src);
    if (text_measure(buf, FS_SMALL).x <= max_w) return;
    size_t n = strlen(buf);
    while (n > 0) {
        n--;
        while (n > 0 && ((unsigned char)buf[n] & 0xC0) == 0x80) n--;   /* step to a char start */
        buf[n] = '\0';
        snprintf(tmp, sizeof tmp, "%s...", buf);
        if (text_measure(tmp, FS_SMALL).x <= max_w) {
            snprintf(buf, cap, "%s", tmp);
            return;
        }
    }
    buf[0] = '\0';
}

static const char *board_label(int n)
{
    return n == 3 ? tr(S_BOARD_3) : n == 5 ? tr(S_BOARD_5) : tr(S_BOARD_10);
}

static void format_date(const SlotInfo *s, char *out, size_t cap)
{
    time_t t = (time_t)s->saved_at;
    struct tm *tm = localtime(&t);
    if (!tm || strftime(out, cap, "%Y-%m-%d %H:%M", tm) == 0) snprintf(out, cap, "-");
}

static void draw_card_used(const SlotInfo *s, Rectangle c, int slot)
{
    char line[160], date[32], names[128], mode[96];
    static const StrId diff_ids[DIFF_COUNT] = { S_DIFF_EASY, S_DIFF_MEDIUM, S_DIFF_HARD };

    snprintf(line, sizeof line, tr(S_SLOT_FMT), slot + 1);
    text_draw(line, c.x + SP_3, c.y + SP_1, FS_BODY, COL_GOLD);

    format_date(s, date, sizeof date);
    text_draw_aligned(date, c.x + c.width - SP_3, c.y + SP_2 - 4, FS_SMALL, COL_TEXT_DIM, 2);

    snprintf(names, sizeof names, "%s vs %s", s->p1, s->p2);
    fit_text(line, sizeof line, names, c.width - SP_3 - DEL_RIGHT - DEL_W - SP_2);
    text_draw(line, c.x + SP_3, c.y + 44, FS_SMALL, COL_TEXT);

    if (s->mode == MODE_PVE) {
        if ((int)s->diff >= 0 && (int)s->diff < DIFF_COUNT)
            snprintf(mode, sizeof mode, "%s - %s - %s", tr(S_MODE_PVE), board_label(s->board_n), tr(diff_ids[s->diff]));
        else
            snprintf(mode, sizeof mode, "%s - %s", tr(S_MODE_PVE), board_label(s->board_n));
    } else {
        snprintf(mode, sizeof mode, "%s - %s", tr(S_MODE_PVP), board_label(s->board_n));
    }
    fit_text(line, sizeof line, mode, 300);
    text_draw(line, c.x + SP_3, c.y + 68, FS_SMALL, COL_TEXT_DIM);

    snprintf(line, sizeof line, tr(S_MOVES_FMT), s->move_count);
    text_draw_aligned(line, c.x + c.width - DEL_RIGHT - DEL_W - SP_2, c.y + 68, FS_SMALL, COL_TEXT_DIM, 2);
}

/* Dashed rectangle outline, used for empty slots. */
static void draw_dashed_rect(Rectangle r, float thick, Color col)
{
    const float dash = 12, gap = 8;
    for (float x = r.x; x < r.x + r.width; x += dash + gap) {
        float w = x + dash > r.x + r.width ? r.x + r.width - x : dash;
        DrawRectangleRec((Rectangle){ x, r.y, w, thick }, col);
        DrawRectangleRec((Rectangle){ x, r.y + r.height - thick, w, thick }, col);
    }
    for (float y = r.y; y < r.y + r.height; y += dash + gap) {
        float h = y + dash > r.y + r.height ? r.y + r.height - y : dash;
        DrawRectangleRec((Rectangle){ r.x, y, thick, h }, col);
        DrawRectangleRec((Rectangle){ r.x + r.width - thick, y, thick, h }, col);
    }
}

/* Draws one card. The card body is the full card rect, so the focus / hover ring (drawn by the
 * ghost button) surrounds the whole card. The Delete button is inside the card; a click on it
 * is not counted as a click on the body. Interactive parts are called in order: body, then Delete. */
static void draw_card(int i, Rectangle c)
{
    const SlotInfo *s = &slots[i];
    bool load = g_app.saves_mode == SAVES_LOAD;
    Rectangle del = { c.x + c.width - DEL_W - DEL_RIGHT, c.y + c.height - DEL_H - SP_1, DEL_W, DEL_H };

    ui_panel(c);
    if (!s->used) {
        DrawRectangleRec(c, Fade(COL_STONE_900, 0.4f));
        draw_dashed_rect((Rectangle){ c.x + 4, c.y + 4, c.width - 8, c.height - 8 }, 2, COL_STONE_900);
    }

    bool body_click = ui_button(c, "", BTN_GHOST);
    bool on_delete = s->used && CheckCollisionPointRec(input_mouse(), del);
    if (body_click && !on_delete) {
        if (!s->used) {
            if (load) {
                audio_play(SFX_ERROR);
                app_toast(tr(S_EMPTY_SLOT));
            } else if (app_save_slot(i)) {
                refresh();
                scene_pop();
            }
        } else if (load) {
            app_load_slot(i);     /* goes to SCENE_GAME on success */
        } else {
            open_popup(POP_OVERWRITE, i);
        }
    }

    if (s->used) {
        draw_card_used(s, c, i);
        if (ui_button(del, tr(S_DELETE), BTN_DANGER)) open_popup(POP_DELETE, i);
    } else {
        char line[48];
        snprintf(line, sizeof line, tr(S_SLOT_FMT), i + 1);
        text_draw(line, c.x + SP_3, c.y + SP_1, FS_BODY, Fade(COL_TEXT, 0.7f));
        text_draw(tr(S_EMPTY_SLOT), c.x + SP_3, c.y + 44, FS_SMALL, Fade(COL_TEXT, 0.7f));
    }
}

static void confirm_popup(void)
{
    int slot = popup_slot;
    PopupKind kind = popup_kind;
    popup_kind = POP_NONE;
    if (kind == POP_OVERWRITE) {
        if (app_save_slot(slot)) {
            refresh();
            scene_pop();   /* back to the match */
            return;
        }
    } else if (kind == POP_DELETE) {
        storage_delete_slot(slot);
    }
    refresh();
}

static void draw_popup(void)
{
    bool overwrite = popup_kind == POP_OVERWRITE;
    float t = (g_app.time - popup_t0) / POP_FADE;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    Rectangle c = ui_popup(POP_W, POP_H, tr(overwrite ? S_OVERWRITE_Q : S_DELETE_Q), ease_out_cubic(t));

    ui_focus_scope(1);
    char line[48];
    snprintf(line, sizeof line, tr(S_SLOT_FMT), popup_slot + 1);
    text_draw_aligned(line, c.x + c.width / 2, c.y + SP_1, FS_BODY, COL_GOLD, 1);

    float by = c.y + c.height - 56, cx = c.x + c.width / 2;
    if (ui_button((Rectangle){ cx - 216, by, 200, 56 }, tr(S_NO), BTN_NORMAL)) {
        close_popup();
    } else if (ui_button((Rectangle){ cx + 16, by, 200, 56 }, tr(S_YES), overwrite ? BTN_PRIMARY : BTN_DANGER)) {
        confirm_popup();
    }
}

static int first_focus_slot(void)
{
    if (g_app.saves_mode == SAVES_LOAD)
        for (int i = 0; i < SAVE_SLOTS; i++) if (slots[i].used) return i;
    return 0;
}

static void saves_enter(void)
{
    popup_kind = POP_NONE;
    refresh();
    ui_focus_scope(0);
    ui_focus_reset();
    /* Widget order: Back (0), then per slot the card and, if used, its Delete button. */
    int target = first_focus_slot(), idx = 1;
    for (int i = 0; i < target; i++) idx += slots[i].used ? 2 : 1;
    ui_focus_set(idx);
}

static void saves_update(float dt)
{
    (void)dt;
    if (input_pressed(ACT_BACK, -1)) {
        if (popup_kind != POP_NONE) { audio_play(SFX_BACK); close_popup(); }
        else go_back();
        return;
    }
    if (popup_kind != POP_NONE) return;
    if (input_pressed(ACT_HOME, -1)) scene_goto(SCENE_HOME);
}

static void saves_draw(void)
{
    bool load = g_app.saves_mode == SAVES_LOAD;
    bool any_used = false;

    draw_backdrop();
    ui_frame_begin();
    ui_focus_scope(0);

    text_draw_aligned(tr(load ? S_LOAD_TITLE : S_SAVE_GAME), VIRTUAL_W / 2, SP_2, FS_H1, COL_GOLD, 1);

    ui_set_interactive(popup_kind == POP_NONE);
    if (ui_button((Rectangle){ GRID_X, SP_2, 176, 56 }, tr(S_BACK), BTN_NORMAL)) go_back();

    for (int i = 0; i < SAVE_SLOTS; i++) if (slots[i].used) any_used = true;
    if (load && !any_used) text_draw_aligned(tr(S_NO_SAVES), VIRTUAL_W / 2, 76, FS_SMALL, COL_TEXT_DIM, 1);

    for (int i = 0; i < SAVE_SLOTS; i++) {
        Rectangle c = { GRID_X + (i % 2) * (CARD_W + CARD_GAP), GRID_Y + (i / 2) * (CARD_H + CARD_GAP),
                        CARD_W, CARD_H };
        draw_card(i, c);
    }

    if (popup_kind != POP_NONE) {
        ui_dim_screen(0.55f);
        ui_set_interactive(true);
        draw_popup();
    }
    ui_hint_bar(tr(S_HINT_MENU));
    ui_frame_end();
}

void scr_saves_register(void)
{
    scene_register(SCENE_SAVES, (Scene){ saves_enter, NULL, saves_update, saves_draw });
}
