/* ui.c - immediate-mode pixel widgets with focus navigation (contract in ui.h).
 * Navigation is applied in ui_frame_end() (after this frame's widgets have had the chance to
 * consume LEFT/RIGHT or typed keys), so focus changes show on the next frame. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "raylib.h"
#include "ui.h"
#include "theme.h"
#include "input.h"
#include "audio.h"
#include "app.h"
#include "scene.h"
#include "../core/i18n.h"

#define SCOPES 4
#define MAX_W 64          /* per-scope animation slots; extra widgets share the last one */
#define ANIM_TIME 0.15f   /* seconds for focus/hover easing */
#define SLIDE_TIME 0.12f  /* toggle knob travel */
#define PANEL_HEAD 64     /* height of a titled panel's header area */
#define EDIT_MAX 256

typedef struct { float t; float aux; } Anim;

/* Per-widget frame state returned by wbegin(). */
typedef struct { bool active, hover, focus; int scope, idx; float t; } W;

static bool interactive = true;
static bool kbnav = true;         /* keyboard/gamepad focus navigation enabled (per frame) */
static bool blocked;              /* scene transition running: all widgets inert */
static int cur_scope = 0;
static int focus[SCOPES];
static int cnt[SCOPES];           /* interactive widgets drawn this frame, per scope */
static int focus_snap[SCOPES];    /* focus at frame begin (for the hover sound) */
static int last_scope = -1;       /* scope of the last interactive widget drawn this frame */
static Anim anim[SCOPES][MAX_W];
static float g_dt = 0.016f;
static float g_fade = 1.0f;       /* alpha multiplier set by ui_popup while it appears */
static bool lr_used;              /* focused widget consumed LEFT/RIGHT this frame */
static bool edit_active;          /* a textbox is in editing mode */
static int edit_scope, edit_idx;
static bool edit_seen;            /* the editing textbox was drawn this frame */
static bool drag_on;              /* slider drag in progress */
static int drag_scope, drag_idx;

/* ---- small colour / drawing helpers ---- */

static Color fc(Color c)
{
    c.a = (unsigned char)(c.a * g_fade + 0.5f);
    return c;
}

static Color with_a(Color c, unsigned char a)
{
    c.a = a;
    return c;
}

static Color lighten(Color c, float k) { return color_lerp(c, (Color){255, 255, 255, 255}, k); }
static Color darken(Color c, float k) { return color_lerp(c, (Color){0, 0, 0, 255}, k); }

static void fillr(float x, float y, float w, float h, Color c)
{
    DrawRectangleRec((Rectangle){ x, y, w, h }, fc(c));
}

static Rectangle inflate(Rectangle r, float d)
{
    return (Rectangle){ r.x - d, r.y - d, r.width + 2 * d, r.height + 2 * d };
}

static bool in_rect(Vector2 p, Rectangle r) { return CheckCollisionPointRec(p, r); }

/* Pixel arrow head (3 cells wide, 5 tall). left = pointing left. */
static void draw_marker(float x, float y, float cell, Color c, bool left)
{
    static const char pat[5][4] = { "X..", "XX.", "XXX", "XX.", "X.." };
    for (int r = 0; r < 5; r++)
        for (int k = 0; k < 3; k++) {
            if (pat[r][k] != 'X') continue;
            int col = left ? 2 - k : k;
            fillr(x + col * cell, y + r * cell, cell, cell, c);
        }
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

/* Snap to the 0.05 keyboard step. */
static float snap5(float v) { return roundf(clampf(v, 0, 1) * 20.0f) / 20.0f; }

/* Moves *t toward 1 (on) or 0 (off) in ANIM_TIME; returns the eased value. */
static float anim_step(float *t, bool on)
{
    float target = on ? 1.0f : 0.0f;
    float step = g_dt / ANIM_TIME;
    if (*t < target) { *t += step; if (*t > target) *t = target; }
    else if (*t > target) { *t -= step; if (*t < target) *t = target; }
    return ease_out_cubic(*t);
}

static float linear_step(float *t, float target, float time)
{
    float step = g_dt / time;
    if (*t < target) { *t += step; if (*t > target) *t = target; }
    else if (*t > target) { *t -= step; if (*t < target) *t = target; }
    return *t;
}

/* ---- focus plumbing ---- */

/* Common per-widget prologue: assigns an index, handles mouse focus and animation. */
static W wbegin(Rectangle r)
{
    W w;
    memset(&w, 0, sizeof w);
    if (!interactive || blocked) return w;
    w.active = true;
    w.scope = cur_scope;
    w.idx = cnt[cur_scope]++;
    w.hover = in_rect(input_mouse(), r);
    if (w.hover && (input_mouse_moved() || input_mouse_pressed())) focus[cur_scope] = w.idx;
    w.focus = kbnav ? focus[cur_scope] == w.idx : w.hover;
    last_scope = cur_scope;
    int ai = w.idx < MAX_W ? w.idx : MAX_W - 1;
    w.t = anim_step(&anim[cur_scope][ai].t, w.focus);
    return w;
}

static Anim *anim_of(const W *w)
{
    int ai = w->idx < MAX_W ? w->idx : MAX_W - 1;
    return &anim[w->scope][ai];
}

/* True when the widget was activated this frame (confirm key on focus, or click on it). */
static bool activate(const W *w)
{
    if (!w->active) return false;
    bool a = (kbnav && w->focus && input_pressed(ACT_CONFIRM, -1)) || (w->hover && input_mouse_pressed());
    if (a) audio_play(SFX_CLICK);
    return a;
}

void ui_frame_begin(void)
{
    interactive = true;
    kbnav = true;
    blocked = scene_transitioning();
    cur_scope = 0;
    g_fade = 1.0f;
    lr_used = false;
    edit_seen = false;
    last_scope = -1;
    for (int s = 0; s < SCOPES; s++) {
        cnt[s] = 0;
        focus_snap[s] = focus[s];
    }
    g_dt = GetFrameTime();
    if (g_dt < 0) g_dt = 0;
    if (g_dt > 0.1f) g_dt = 0.1f;
}

void ui_frame_end(void)
{
    if (edit_active && !edit_seen) edit_active = false; /* its textbox vanished */

    int ns = last_scope < 0 ? cur_scope : last_scope;
    int n = cnt[ns];
    if (n <= 0) {
        focus[ns] = 0;
    } else {
        if (focus[ns] < 0) focus[ns] = 0;
        if (focus[ns] >= n) focus[ns] = n - 1;
        if (!edit_active && kbnav) {
            if (input_pressed(ACT_UP, -1)) focus[ns] = (focus[ns] + n - 1) % n;
            if (input_pressed(ACT_DOWN, -1)) focus[ns] = (focus[ns] + 1) % n;
            if (!lr_used && input_pressed(ACT_LEFT, -1)) focus[ns] = (focus[ns] + n - 1) % n;
            if (!lr_used && input_pressed(ACT_RIGHT, -1)) focus[ns] = (focus[ns] + 1) % n;
        }
    }

    for (int s = 0; s < SCOPES; s++)
        if (focus[s] != focus_snap[s]) { audio_play(SFX_HOVER); break; }
}

void ui_focus_scope(int scope_id)
{
    cur_scope = scope_id < 0 ? 0 : (scope_id >= SCOPES ? SCOPES - 1 : scope_id);
}

void ui_focus_reset(void) { focus[cur_scope] = 0; }
void ui_focus_set(int index) { focus[cur_scope] = index < 0 ? 0 : index; }
int ui_focus_get(void) { return focus[cur_scope]; }
void ui_set_interactive(bool on) { interactive = on; }
void ui_set_keyboard_nav(bool on) { kbnav = on; }

/* ---- buttons ---- */

static void draw_tooltip(Rectangle r, const char *tip)
{
    Vector2 m = text_measure(tip, FS_SMALL);
    float tw = m.x + 24, th = m.y + 12;
    float x = clampf(r.x + r.width / 2 - tw / 2, 8, VIRTUAL_W - tw - 8);
    float y = r.y - th - 10;
    fillr(x, y, tw, th, with_a(COL_STONE_900, 235));
    DrawRectangleLinesEx((Rectangle){ x, y, tw, th }, 2, fc(COL_STONE_600));
    text_draw(tip, x + 12, y + 6, FS_SMALL, fc(COL_TEXT));
}

static bool button_core(Rectangle r, const char *label, ButtonStyle style, bool marker, W *out)
{
    W w = wbegin(r);
    bool act = activate(&w);
    bool held = w.active && ((kbnav && w.focus && input_down(ACT_CONFIRM, -1)) || (w.hover && input_mouse_down()));
    float t = w.t;
    bool ghost = style == BTN_GHOST;
    Color fill = COL_STONE_700, txt = COL_TEXT, mark = COL_GOLD;

    switch (style) {
    case BTN_PRIMARY:
        fill = color_lerp(COL_GOLD, (Color){255, 255, 255, 255}, 0.20f * t);
        txt = COL_STONE_900;
        mark = COL_STONE_900;
        break;
    case BTN_DANGER:
        fill = color_lerp(COL_DANGER, COL_FIRE, 0.25f * t);
        break;
    case BTN_GHOST:
        break;
    default:
        fill = color_lerp(COL_STONE_700, COL_STONE_600, 0.55f * t);
        break;
    }

    /* Lift 2 px on focus (hard shadow shows underneath); sink 2 px while pressed. */
    Rectangle b = r;
    b.y = held ? r.y + 2 : r.y - roundf(2.0f * t);
    if (!ghost) {
        fillr(r.x, held ? b.y : r.y + 4, r.width, r.height, with_a(COL_STONE_900, 220));
        fillr(b.x, b.y, b.width, b.height, fill);
        fillr(b.x + 3, b.y + 3, b.width - 6, 2, lighten(fill, 0.35f));
        fillr(b.x + 3, b.y + b.height - 5, b.width - 6, 2, darken(fill, 0.3f));
        DrawRectangleLinesEx(b, 3, fc(COL_STONE_900));
    }
    if (t > 0.01f)
        DrawRectangleLinesEx(inflate(b, 6), 2, fc(with_a(COL_FOCUS, (unsigned char)(255 * t))));

    Vector2 m = text_measure(label, FS_BODY);
    float tx = b.x + (b.width - m.x) / 2, ty = b.y + (b.height - m.y) / 2;
    if (t > 0.5f) text_draw_bold(label, tx, ty, FS_BODY, fc(txt));
    else text_draw(label, tx, ty, FS_BODY, fc(txt));
    if (marker && !ghost && w.focus && label[0] && tx - (b.x + 24) > 8)
        draw_marker(b.x + 12, b.y + (b.height - 20) / 2, 4, fc(mark), false);

    if (out) *out = w;
    return act;
}

bool ui_button(Rectangle r, const char *label, ButtonStyle style)
{
    return button_core(r, label, style, true, NULL);
}

bool ui_icon_button(Rectangle r, const char *glyph, const char *tooltip)
{
    W w;
    bool act = button_core(r, glyph, BTN_NORMAL, false, &w);
    if (w.focus && tooltip && *tooltip) draw_tooltip(r, tooltip);
    return act;
}

/* ---- rows: slider / toggle / choice share a label on the left ---- */

static void row_label(const W *w, Rectangle r, const char *label)
{
    Vector2 m = text_measure(label, FS_BODY);
    float ty = r.y + (r.height - m.y) / 2;
    if (w->focus) draw_marker(r.x + 4, r.y + (r.height - 20) / 2, 4, fc(COL_GOLD), false);
    if (w->t > 0.5f) text_draw_bold(label, r.x + 32, ty, FS_BODY, fc(COL_TEXT));
    else text_draw(label, r.x + 32, ty, FS_BODY, fc(COL_TEXT));
}

static void row_focus_ring(const W *w, Rectangle r)
{
    if (w->t > 0.01f)
        DrawRectangleLinesEx(inflate(r, 6), 2, fc(with_a(COL_FOCUS, (unsigned char)(255 * w->t))));
}

bool ui_slider(Rectangle r, const char *label, float *value)
{
    W w = wbegin(r);
    float v = clampf(*value, 0, 1);
    float old = v;
    float tx = r.x + r.width * 0.46f;
    float tw = (r.x + r.width - 120) - tx;
    float cy = r.y + r.height / 2;
    Rectangle track = { tx, cy - 10, tw, 20 };
    Rectangle hit = { tx - 12, cy - 24, tw + 24, 48 };

    if (w.active) {
        if (input_mouse_pressed() && in_rect(input_mouse(), hit)) {
            drag_on = true;
            drag_scope = w.scope;
            drag_idx = w.idx;
        }
        if (drag_on && drag_scope == w.scope && drag_idx == w.idx) {
            if (input_mouse_down()) v = clampf((input_mouse().x - tx) / tw, 0, 1);
            else drag_on = false;
        }
        if (w.focus) {
            if (input_pressed(ACT_LEFT, -1)) { v = snap5(v - 0.05f); lr_used = true; }
            if (input_pressed(ACT_RIGHT, -1)) { v = snap5(v + 0.05f); lr_used = true; }
        }
    }

    row_label(&w, r, label);
    row_focus_ring(&w, r);

    /* track: sunken stone with a gold fill */
    fillr(track.x, track.y, track.width, track.height, COL_STONE_900);
    fillr(track.x + 2, track.y + 2, (track.width - 4) * v, track.height - 4, COL_GOLD);
    fillr(track.x + 2, track.y + 2, (track.width - 4) * v, 2, lighten(COL_GOLD, 0.4f));
    DrawRectangleLinesEx(track, 2, fc(COL_STONE_600));

    /* square knob */
    float kx = track.x + track.width * v;
    Rectangle knob = { kx - 12, cy - 14, 24, 28 };
    fillr(knob.x, knob.y, knob.width, knob.height, w.focus ? COL_GOLD : COL_TEXT);
    DrawRectangleLinesEx(knob, 3, fc(COL_STONE_900));

    char pct[8];
    snprintf(pct, sizeof pct, "%d%%", (int)roundf(v * 100));
    text_draw_aligned(pct, r.x + r.width, r.y + (r.height - FS_BODY) / 2, FS_BODY, fc(COL_TEXT), 2);

    *value = v;
    return v != old;
}

bool ui_toggle(Rectangle r, const char *label, bool *value)
{
    W w = wbegin(r);
    bool changed = false;
    bool act = activate(&w);
    bool lr = w.active && w.focus && (input_pressed(ACT_LEFT, -1) || input_pressed(ACT_RIGHT, -1));
    if (lr) lr_used = true;
    if (act || lr) {
        *value = !*value;
        changed = true;
        if (lr) audio_play(SFX_CLICK);
    }

    row_label(&w, r, label);
    row_focus_ring(&w, r);

    /* pixel switch: sunken track with a square knob that slides */
    float sw_w = 104, sw_h = 40;
    Rectangle sw = { r.x + r.width - sw_w, r.y + (r.height - sw_h) / 2, sw_w, sw_h };
    float aux = *value ? 1.0f : 0.0f;
    if (w.active) aux = linear_step(&anim_of(&w)->aux, aux, SLIDE_TIME);
    fillr(sw.x, sw.y, sw.width, sw.height, color_lerp(COL_STONE_900, COL_MOSS, aux));
    DrawRectangleLinesEx(sw, 3, fc(COL_STONE_900));
    float kx = sw.x + 6 + (sw.width - 12 - 28) * aux;
    fillr(kx, sw.y + 6, 28, 28, color_lerp(COL_STONE_400, COL_GOLD, aux));
    DrawRectangleLinesEx((Rectangle){ kx, sw.y + 6, 28, 28 }, 2, fc(COL_STONE_900));

    const char *vt = *value ? tr(S_ON) : tr(S_OFF);
    Vector2 m = text_measure(vt, FS_BODY);
    text_draw_aligned(vt, sw.x - 20, r.y + (r.height - m.y) / 2, FS_BODY,
                      fc(*value ? COL_GOLD : COL_TEXT_DIM), 2);
    return changed;
}

bool ui_choice(Rectangle r, const char *label, const char *const *options, int count, int *selected)
{
    W w = wbegin(r);
    bool changed = false;
    /* value box: up to 360 px but never over the label (label starts at r.x + 32) */
    float lw = text_measure(label, FS_BODY).x + 32 + SP_3;
    float bw = r.width - lw;
    if (bw > 360) bw = 360;
    if (bw < 160) bw = 160;
    Rectangle box = { r.x + r.width - bw, r.y, bw, r.height };
    Rectangle la = { box.x, box.y, 48, box.height };
    Rectangle ra = { box.x + box.width - 48, box.y, 48, box.height };

    int delta = 0;
    if (w.active && count > 0) {
        if (w.focus && input_pressed(ACT_LEFT, -1)) { delta = -1; lr_used = true; }
        if (w.focus && input_pressed(ACT_RIGHT, -1)) { delta = 1; lr_used = true; }
        if (w.hover && input_mouse_pressed() && in_rect(input_mouse(), box))
            delta = in_rect(input_mouse(), la) ? -1 : 1;
    }
    if (delta && count > 0) {
        int cur = *selected;
        if (cur < 0 || cur >= count) cur = 0;
        int nv = ((cur + delta) % count + count) % count;
        if (nv != *selected) {
            *selected = nv;
            changed = true;
        }
        audio_play(SFX_CLICK);
    }

    row_label(&w, r, label);
    row_focus_ring(&w, r);

    /* sunken value box with clickable pixel arrows */
    fillr(box.x, box.y, box.width, box.height, COL_STONE_900);
    DrawRectangleLinesEx(box, 2, fc(w.focus ? COL_GOLD : COL_STONE_600));
    Vector2 mp = input_mouse();
    bool hl = w.hover && in_rect(mp, la);
    bool hr = w.hover && in_rect(mp, ra);
    draw_marker(la.x + 18, la.y + (la.height - 20) / 2, 4, fc(hl ? COL_FOCUS : COL_GOLD), true);
    draw_marker(ra.x + 18, ra.y + (ra.height - 20) / 2, 4, fc(hr ? COL_FOCUS : COL_GOLD), false);

    int cur = (count > 0 && *selected >= 0 && *selected < count) ? *selected : 0;
    if (count > 0) {
        Vector2 m = text_measure(options[cur], FS_BODY);
        text_draw_aligned(options[cur], box.x + box.width / 2, box.y + (box.height - m.y) / 2,
                          FS_BODY, fc(COL_TEXT), 1);
    }
    return changed;
}

/* ---- text box ---- */

static int utf8_encode(int cp, char *o)
{
    if (cp < 0x80) { o[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        o[0] = (char)(0xC0 | (cp >> 6));
        o[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        o[0] = (char)(0xE0 | (cp >> 12));
        o[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        o[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    o[0] = (char)(0xF0 | (cp >> 18));
    o[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    o[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    o[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* Removes the last whole UTF-8 character. */
static void utf8_pop(char *buf)
{
    size_t len = strlen(buf);
    if (len == 0) return;
    size_t i = len - 1;
    while (i > 0 && ((unsigned char)buf[i] & 0xC0) == 0x80) i--;
    buf[i] = '\0';
}

bool ui_textbox(Rectangle r, char *buf, int max_bytes, const char *placeholder)
{
    W w = wbegin(r);
    bool editing = false;
    if (max_bytes < 1) max_bytes = 1;
    buf[max_bytes - 1] = '\0'; /* the caller's buffer holds max_bytes incl. NUL */

    if (w.active) {
        editing = edit_active && edit_scope == w.scope && edit_idx == w.idx;
        if (editing) {
            edit_seen = true;
            int guard = 0, cp;
            while (guard++ < 32 && (cp = input_char()) != 0) {
                if (cp < 32 || (cp >= 127 && cp < 160)) continue;
                char enc[4];
                int n = utf8_encode(cp, enc);
                int len = (int)strlen(buf);
                if (len + n < max_bytes) {
                    memcpy(buf + len, enc, (size_t)n);
                    buf[len + n] = '\0';
                }
            }
            if (input_backspace()) utf8_pop(buf);
            /* Enter (player 2 confirm) or Esc leaves; Space (player 1) is typed text. */
            if (input_pressed(ACT_CONFIRM, 1) || input_pressed(ACT_BACK, -1)) {
                edit_active = false;
                editing = false;
            } else if (input_mouse_pressed() && !w.hover) {
                edit_active = false;
                editing = false;
            }
        } else if ((w.focus && input_pressed(ACT_CONFIRM, -1)) || (w.hover && input_mouse_pressed())) {
            edit_active = true;
            edit_scope = w.scope;
            edit_idx = w.idx;
            edit_seen = true;
            editing = true;
            audio_play(SFX_CLICK);
        }
    }

    bool hi = w.focus || editing;
    fillr(r.x, r.y, r.width, r.height, COL_STONE_900);
    DrawRectangleLinesEx(r, 3, fc(hi ? COL_GOLD : COL_STONE_600));

    float avail = r.width - 32 - 8;
    float ty = r.y + (r.height - FS_BODY) / 2;
    int len = (int)strlen(buf);
    if (len == 0) {
        if (placeholder && *placeholder)
            text_draw(placeholder, r.x + 16, ty, FS_BODY, fc(with_a(COL_TEXT_DIM, 170)));
    }
    /* long text shows its tail: skip leading codepoints until the rest fits */
    int off = 0;
    while (off < len && text_measure(buf + off, FS_BODY).x > avail) {
        off++;
        while (off < len && ((unsigned char)buf[off] & 0xC0) == 0x80) off++;
    }
    if (len) text_draw(buf + off, r.x + 16, ty, FS_BODY, fc(COL_TEXT));
    if (editing && fmodf((float)GetTime(), 1.0f) < 0.5f) {
        float cx = r.x + 16 + (len ? text_measure(buf + off, FS_BODY).x : 0);
        fillr(cx + 2, ty + 2, 4, FS_BODY - 6, COL_GOLD);
    }
    return editing;
}

bool ui_textbox_editing(void) { return edit_active; }

/* ---- decorative ---- */

static void draw_panel_at(Rectangle r, const char *title)
{
    fillr(r.x, r.y, r.width, r.height, COL_STONE_800);

    /* faint brick seams in the top band */
    Color seam = with_a(COL_STONE_900, 90);
    for (int row = 0; row < 3; row++) {
        float y = r.y + 8 + row * 24;
        if (y > r.y + 80) break;
        fillr(r.x + 8, y, r.width - 16, 1, seam);
        float off = (row & 1) ? 0 : 40;
        for (float x = r.x + 8 + off; x < r.x + r.width - 8; x += 80)
            fillr(x, y, 1, 24, seam);
    }

    DrawRectangleLinesEx(r, 4, fc(COL_STONE_900));
    DrawRectangleLinesEx((Rectangle){ r.x + 4, r.y + 4, r.width - 8, r.height - 8 }, 2, fc(COL_STONE_600));
    fillr(r.x + 6, r.y + 6, r.width - 12, 2, with_a(COL_STONE_400, 70));

    if (title && *title) {
        fillr(r.x + 4, r.y + 4, r.width - 8, 48, with_a(COL_STONE_900, 150));
        fillr(r.x + 4, r.y + 52, r.width - 8, 2, COL_GOLD_DARK);
        Vector2 m = text_measure(title, FS_H2);
        text_draw_outline(title, r.x + (r.width - m.x) / 2, r.y + 4 + (48 - m.y) / 2, FS_H2,
                          fc(COL_GOLD), fc(COL_STONE_900), 2);
    }

    /* gold corner rivets */
    float rx[2] = { r.x + 12, r.x + r.width - 20 };
    float ry[2] = { r.y + 12, r.y + r.height - 20 };
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) {
            fillr(rx[i], ry[j], 8, 8, COL_GOLD);
            fillr(rx[i] + 6, ry[j], 2, 8, COL_GOLD_DARK);
            fillr(rx[i], ry[j] + 6, 8, 2, COL_GOLD_DARK);
        }
}

void ui_panel(Rectangle r) { draw_panel_at(r, NULL); }
void ui_panel_titled(Rectangle r, const char *title) { draw_panel_at(r, title); }

void ui_dim_screen(float alpha01)
{
    float a = clampf(alpha01, 0, 1);
    DrawRectangleRec((Rectangle){ 0, 0, VIRTUAL_W, VIRTUAL_H }, (Color){ 0, 0, 0, (unsigned char)(255 * a) });
}

Rectangle ui_popup(float w, float h, const char *title, float appear01)
{
    float a = clampf(appear01, 0, 1);
    float sc = 0.92f + 0.08f * ease_out_cubic(a);
    g_fade = a;
    float pw = w * sc, ph = h * sc;
    Rectangle pr = { (VIRTUAL_W - pw) / 2, (VIRTUAL_H - ph) / 2, pw, ph };
    draw_panel_at(pr, title);
    float top = (title && *title) ? PANEL_HEAD : 24;
    return (Rectangle){ (VIRTUAL_W - w) / 2 + 24, (VIRTUAL_H - h) / 2 + top, w - 48, h - top - 24 };
}

void ui_hint_bar(const char *text)
{
    fillr(0, VIRTUAL_H - 48, VIRTUAL_W, 48, with_a(COL_STONE_900, 235));
    fillr(0, VIRTUAL_H - 48, VIRTUAL_W, 2, COL_STONE_600);
    if (!text) return;
    Vector2 m = text_measure(text, FS_SMALL);
    text_draw(text, (VIRTUAL_W - m.x) / 2, VIRTUAL_H - 48 + (48 - m.y) / 2, FS_SMALL, fc(COL_TEXT_DIM));
}
