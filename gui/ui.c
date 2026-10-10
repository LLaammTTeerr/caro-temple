/* STUB - replaced by unit "UI widgets + theme". Minimal mouse-only widgets. */
#include <string.h>
#include "ui.h"
#include "theme.h"
#include "input.h"
#include "app.h"
static bool interactive = true;
static int focus_idx, count;
void ui_frame_begin(void) { interactive = true; count = 0; }
void ui_frame_end(void) {}
void ui_focus_scope(int s) { (void)s; }
void ui_focus_reset(void) { focus_idx = 0; }
void ui_focus_set(int i) { focus_idx = i; }
int ui_focus_get(void) { return focus_idx; }
void ui_set_interactive(bool on) { interactive = on; }
static bool hit(Rectangle r) { return interactive && CheckCollisionPointRec(input_mouse(), r); }
bool ui_button(Rectangle r, const char *label, ButtonStyle style)
{
    count++;
    bool h = hit(r);
    DrawRectangleRec(r, style == BTN_PRIMARY ? COL_GOLD_DARK : COL_STONE_700);
    if (h) DrawRectangleLinesEx(r, 2, COL_FOCUS);
    text_draw_aligned(label, r.x + r.width / 2, r.y + (r.height - FS_BODY) / 2, FS_BODY, COL_TEXT, 1);
    return h && input_mouse_pressed();
}
bool ui_icon_button(Rectangle r, const char *g, const char *t) { (void)t; return ui_button(r, g, BTN_NORMAL); }
bool ui_slider(Rectangle r, const char *label, float *v) { count++; text_draw(label, r.x, r.y, FS_BODY, COL_TEXT); DrawRectangle(r.x + r.width / 2, r.y + 10, (int)(r.width / 2 * *v), 12, COL_GOLD); return false; }
bool ui_toggle(Rectangle r, const char *label, bool *v) { count++; text_draw(label, r.x, r.y, FS_BODY, COL_TEXT); text_draw(*v ? "ON" : "OFF", r.x + r.width - 60, r.y, FS_BODY, COL_GOLD); if (hit(r) && input_mouse_pressed()) { *v = !*v; return true; } return false; }
bool ui_choice(Rectangle r, const char *label, const char *const *o, int n, int *sel) { count++; text_draw(label, r.x, r.y, FS_BODY, COL_TEXT); if (*sel >= 0 && *sel < n) text_draw(o[*sel], r.x + r.width / 2, r.y, FS_BODY, COL_GOLD); if (hit(r) && input_mouse_pressed()) { *sel = (*sel + 1) % n; return true; } return false; }
bool ui_textbox(Rectangle r, char *buf, int max, const char *ph) { (void)max; count++; DrawRectangleRec(r, COL_STONE_800); text_draw(buf[0] ? buf : ph, r.x + 8, r.y + 4, FS_BODY, COL_TEXT); return false; }
bool ui_textbox_editing(void) { return false; }
void ui_panel(Rectangle r) { DrawRectangleRec(r, COL_STONE_800); DrawRectangleLinesEx(r, 3, COL_STONE_600); }
void ui_panel_titled(Rectangle r, const char *t) { ui_panel(r); text_draw_aligned(t, r.x + r.width / 2, r.y + 8, FS_H2, COL_GOLD, 1); }
void ui_dim_screen(float a) { DrawRectangle(0, 0, VIRTUAL_W, VIRTUAL_H, Fade(BLACK, a)); }
Rectangle ui_popup(float w, float h, const char *title, float ap) { (void)ap; Rectangle r = {(VIRTUAL_W - w) / 2, (VIRTUAL_H - h) / 2, w, h}; ui_panel_titled(r, title); return (Rectangle){r.x + 24, r.y + 64, w - 48, h - 88}; }
void ui_hint_bar(const char *t) { text_draw_aligned(t, VIRTUAL_W / 2, VIRTUAL_H - 36, FS_SMALL, COL_TEXT_DIM, 1); }
