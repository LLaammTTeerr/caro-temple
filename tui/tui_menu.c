/* tui_menu.c - terminal menu screens: HOME, SETUP, PROFILE, SETTINGS, SAVES.
 * The item list is rebuilt from g_tui on every event and frame, so hit-testing and drawing
 * always agree on geometry. Modal confirm boxes (quit / delete / overwrite) are not in the list. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tui.h"
#include "../core/i18n.h"

#define MAX_ITEMS 16
#define MT_NONE 0xFFFFFFFFu     /* transparent mascot pixel */
#define SL_BARW 20              /* slider bar width in cells */
#define FIELD_W 20              /* name field inner width */
#define CARD_W 34               /* profile card */
#define CARD_H 17

enum { MI_BUTTON, MI_CHOICE, MI_TOGGLE, MI_SLIDER, MI_TEXT };
enum { MODAL_NONE, MODAL_QUIT, MODAL_DELETE, MODAL_OVERWRITE };
enum { HM_PLAY, HM_CONTINUE, HM_LOAD, HM_SETTINGS, HM_QUIT };
enum { SU_MODE, SU_BOARD, SU_DIFF, SU_RULE, SU_BACK, SU_NEXT };
enum { PF_AV0, PF_NAME0, PF_AV1, PF_NAME1, PF_BACK, PF_START };
enum { ST_LANG, ST_MUSIC, ST_MUTE_MUSIC, ST_SFX, ST_MUTE_SFX, ST_MOTION, ST_BACK };
#define SV_BACK 10              /* save slots use ids 0..SAVE_SLOTS-1 */

typedef struct {
    int id;
    int kind;
    const char *label;      /* may be NULL (box only) */
    char vbuf[48];          /* formatted value for choice/toggle/slider */
    const char *value;      /* points at vbuf */
    char *buf;              /* text field storage (MI_TEXT) */
    bool enabled;
    bool centre;            /* choice drawn centred without label (avatar) */
    int percent;            /* slider 0..100 */
    int x, y, w, h;
} MItem;

static MItem items[MAX_ITEMS];
static int nitems;
static int focus = -1;      /* index into items[], -1 none */
static int modal = MODAL_NONE;
static int modal_slot;
static int mfocus;          /* 0 = yes/first button, 1 = no/second */
static bool editing;        /* typing into a text field */
static bool home_has_save;  /* cached at HOME enter */
static SlotInfo slots[SAVE_SLOTS];

/* ---------------------------------------------------------------- mascots */

typedef struct { TColor outline, body, shade, hi, eye, accent, light; } MascotPal;

static const MascotPal MASCOT_PAL[6] = {
    { 0x4A1408u, 0xE8562Au, 0xB8361Au, 0xFFB13Bu, 0x2A0E06u, 0xFF7A1Fu, 0xFFE066u }, /* fire flame */
    { 0x0B2747u, 0x2FA8FFu, 0x1A72C4u, 0xC8F2FFu, 0x0B1F33u, 0x2FA8FFu, 0x9BE7FFu }, /* water drop */
    { 0x1E2A0Eu, 0x7FA34Au, 0x5C7A2Eu, 0xC6E08Au, 0x16200Au, 0x4FBF3Au, 0xB8F07Au }, /* forest sprout */
    { 0x5A2A18u, 0xD97757u, 0xB85C3Du, 0xF4B89Cu, 0x1F1E1Du, 0xF2C14Eu, 0xFFE9A8u }, /* temple king */
    { 0x1A1C12u, 0x8A8F70u, 0x656A50u, 0xC8CCAEu, 0x1A1C12u, 0xB9B79Cu, 0xE8E6CFu }, /* guardian */
    { 0x121110u, 0x3D3A36u, 0x2A2825u, 0x6B6660u, 0xF0A27Eu, 0xC9A227u, 0xE8D27Au }, /* automaton */
};

/* Headgear rows 0-5 per avatar (A/F accent, L/Y accent-light, H highlight). */
static const char *const MASCOT_HEAD[6][6] = {
    { ".......Y........", "......YF...Y....", ".....YFF..YF....",
      "....YFFLFYFF....", "....FFLLLLFF....", "...FFLLLLLLFF..." },
    { "..........L.....", ".........LL.....", "........LAL.....",
      ".......LAAL.....", "....LLLAAL......", "...LAAAAALL....." },
    { "................", ".........AA.....", "....AA..AAA.....",
      "....AAA.A.......", "......AAA.......", ".......A........" },
    { "................", "....A..A..A.....", "....AA.A.AA.....",
      "....AAAAAAA.....", "....AHAHAHA.....", "....AAAAAAA....." },
    { "................", ".......A........", ".....AAAAA......",
      "....AAHAAAA.....", "...AAAAAAAAA....", "...A.A.A.A.A...." },
    { ".......L........", "......LLL.......", ".......A........",
      ".......A........", ".....AAAAA......", "................" },
};

/* Body rows 6-15, shared by all avatars (B body, S shade, H highlight, E eye). */
static const char *const MASCOT_BODY[10] = {
    "...HBBBBBBBBB...", "...BBBBBBBBB....", "..BBBEBBBEBBB...", "...BBEBBBEBB....",
    "...SSSSSSSSS....", "...S.S...S.S....", "...S.S...S.S....", "...S.S...S.S....",
    "................", "................",
};

static TColor mpx(const TColor g[16][16], int r, int c)
{
    if (r < 0 || r >= 16 || c < 0 || c >= 16) return MT_NONE;
    return g[r][c];
}

void tui_draw_mascot(int x, int y, int avatar, TColor bg, bool bob, bool flicker);

/* Draw a 16x16 mascot as 16x8 terminal cells using the half-block glyph.
 * bob shifts the art by one half cell (idle bob); flicker swaps the fire flame tips. */
void tui_draw_mascot(int x, int y, int avatar, TColor bg, bool bob, bool flicker)
{
    int a = ((avatar % AVATAR_COUNT) + AVATAR_COUNT) % AVATAR_COUNT;
    const MascotPal *p = &MASCOT_PAL[a];
    TColor acc = p->accent, lit = p->light, px[16][16], full[16][16];
    if (flicker && a == 0) { acc = p->light; lit = p->accent; }
    for (int r = 0; r < 16; r++) {
        for (int c = 0; c < 16; c++) {
            char ch = r < 6 ? MASCOT_HEAD[a][r][c] : MASCOT_BODY[r - 6][c];
            TColor t = MT_NONE;
            switch (ch) {
            case 'B': t = p->body; break;
            case 'S': t = p->shade; break;
            case 'H': t = p->hi; break;
            case 'E': t = p->eye; break;
            case 'A': case 'F': t = acc; break;
            case 'L': case 'Y': t = lit; break;
            default: break;
            }
            px[r][c] = t;
        }
    }
    for (int r = 0; r < 16; r++) {
        for (int c = 0; c < 16; c++) {
            if (px[r][c] != MT_NONE) { full[r][c] = px[r][c]; continue; }
            bool edge = mpx(px, r - 1, c) != MT_NONE || mpx(px, r + 1, c) != MT_NONE ||
                        mpx(px, r, c - 1) != MT_NONE || mpx(px, r, c + 1) != MT_NONE;
            full[r][c] = edge ? p->outline : MT_NONE;
        }
    }
    int sh = bob ? 1 : 0;
    for (int ty = 0; ty < 8; ty++) {
        for (int tx = 0; tx < 16; tx++) {
            TColor t = mpx(full, 2 * ty - sh, tx), b = mpx(full, 2 * ty + 1 - sh, tx);
            if (t == MT_NONE && b == MT_NONE) continue;
            if (t == MT_NONE) term_put(x + tx, y + ty, 0x2584u, b, bg, 0);      /* lower half block */
            else if (b == MT_NONE) term_put(x + tx, y + ty, 0x2580u, t, bg, 0); /* upper half block */
            else term_put(x + tx, y + ty, 0x2580u, t, b, 0);
        }
    }
}

/* ---------------------------------------------------------------- text helpers */

static void put_centred(int x, int w, int y, const char *s, TColor fg, TColor bg, int attr)
{
    int n = utf8_cell_width(s);
    int sx = x + (w - n) / 2;
    if (sx < x) sx = x;
    term_print(sx, y, s, fg, bg, attr);
}

static void put_right(int xr, int y, const char *s, TColor fg, TColor bg, int attr)
{
    term_print(xr - utf8_cell_width(s), y, s, fg, bg, attr);
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

/* Suffix of s that fits in maxc cells (shows the end of a long name). */
static const char *tail_cells(const char *s, int maxc)
{
    int n = utf8_cell_width(s);
    while (n > maxc && *s) {
        uint32_t cp;
        int k = utf8_decode(s, &cp);
        if (k <= 0) break;
        s += k;
        n--;
    }
    return s;
}

static int utf8_encode(uint32_t cp, char *o)
{
    if (cp < 0x80u) { o[0] = (char)cp; return 1; }
    if (cp < 0x800u) { o[0] = (char)(0xC0u | (cp >> 6)); o[1] = (char)(0x80u | (cp & 0x3Fu)); return 2; }
    if (cp < 0x10000u) {
        o[0] = (char)(0xE0u | (cp >> 12)); o[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        o[2] = (char)(0x80u | (cp & 0x3Fu));
        return 3;
    }
    o[0] = (char)(0xF0u | (cp >> 18)); o[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
    o[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu)); o[3] = (char)(0x80u | (cp & 0x3Fu));
    return 4;
}

/* Append one codepoint if the UTF-8 result still fits PLAYER_NAME_MAX - 1 bytes. */
static void name_append(char *buf, uint32_t cp)
{
    char enc[4];
    if (cp < 0x20u || cp == 0x7Fu) return;
    int n = utf8_encode(cp, enc);
    size_t len = strlen(buf);
    if (len + (size_t)n > PLAYER_NAME_MAX - 1) return;
    memcpy(buf + len, enc, (size_t)n);
    buf[len + (size_t)n] = '\0';
}

/* Remove the last whole UTF-8 character. */
static void name_backspace(char *buf)
{
    size_t len = strlen(buf);
    if (len == 0) return;
    size_t i = len - 1;
    while (i > 0 && (((unsigned char)buf[i]) & 0xC0u) == 0x80u) i--;
    buf[i] = '\0';
}

static bool name_blank(const char *s)
{
    for (; *s; s++) if (*s != ' ') return false;
    return true;
}

static bool is_nav_char(uint32_t ch)
{
    switch (ch) {
    case 'w': case 'a': case 's': case 'd':
    case 'W': case 'A': case 'S': case 'D': case ' ':
        return true;
    default:
        return false;
    }
}

/* ---------------------------------------------------------------- item list */

static MItem *add_item(int id, int kind, const char *label, int x, int y, int w, int h, bool enabled)
{
    if (nitems >= MAX_ITEMS) return &items[MAX_ITEMS - 1];
    MItem *m = &items[nitems++];
    memset(m, 0, sizeof *m);
    m->id = id; m->kind = kind; m->label = label;
    m->x = x; m->y = y; m->w = w; m->h = h; m->enabled = enabled;
    m->value = m->vbuf;
    return m;
}

static void fix_focus(void)
{
    if (focus >= 0 && focus < nitems && items[focus].enabled) return;
    focus = -1;
    for (int i = 0; i < nitems; i++) {
        if (items[i].enabled) { focus = i; return; }
    }
}

static int slider_barx(const MItem *m) { return m->x + m->w - 6 - SL_BARW; }

static int board_index(void)
{
    int b = g_tui.settings.last_board;
    return b == 3 ? 0 : (b == 5 ? 1 : 2);
}

static void fmt_avatar(char *buf, int avatar)
{
    snprintf(buf, 48, "< %s %d/%d >", tr(S_AVATAR), avatar + 1, AVATAR_COUNT);
}

static void fmt_pct(char *buf, int pct)
{
    snprintf(buf, 48, "%3d%%", pct);
}

static int pct_of(float v)
{
    int p = (int)(v * 100.0f + 0.5f);
    return ((p + 2) / 5) * 5;
}

static int clamp_pct(int p)
{
    if (p < 0) return 0;
    if (p > 100) return 100;
    return p;
}

/* ---------------------------------------------------------------- saves cache */

static void slots_refresh(void) { storage_list_slots(slots); }

/* ---------------------------------------------------------------- builders */

static void build_home(int cols, int rows)
{
    int oy = rows > 30 ? (rows - 30) / 2 : 0;
    int bx = (cols - 30) / 2;
    add_item(HM_PLAY, MI_BUTTON, tr(S_PLAY), bx, 9 + oy, 30, 3, true);
    add_item(HM_CONTINUE, MI_BUTTON, tr(S_CONTINUE), bx, 12 + oy, 30, 3,
             g_tui.has_game || home_has_save);
    add_item(HM_LOAD, MI_BUTTON, tr(S_LOAD_GAME), bx, 15 + oy, 30, 3, true);
    add_item(HM_SETTINGS, MI_BUTTON, tr(S_SETTINGS), bx, 18 + oy, 30, 3, true);
    add_item(HM_QUIT, MI_BUTTON, tr(S_QUIT), bx, 21 + oy, 30, 3, true);
}

static void build_setup(int cols, int rows)
{
    (void)rows;
    Settings *s = &g_tui.settings;
    bool pve = s->last_mode == MODE_PVE;
    int px = (cols - 62) / 2, py = 3, iw = 56;
    MItem *m;
    m = add_item(SU_MODE, MI_CHOICE, tr(S_MODE), px + 3, py + 3, iw, 1, true);
    snprintf(m->vbuf, 48, "< %s >", tr(pve ? S_MODE_PVE : S_MODE_PVP));
    m = add_item(SU_BOARD, MI_CHOICE, tr(S_BOARD_SIZE), px + 3, py + 6, iw, 1, true);
    snprintf(m->vbuf, 48, "< %s >", tr(board_index() == 0 ? S_BOARD_3 : (board_index() == 1 ? S_BOARD_5 : S_BOARD_10)));
    m = add_item(SU_DIFF, MI_CHOICE, tr(S_DIFFICULTY), px + 3, py + 9, iw, 1, pve);
    snprintf(m->vbuf, 48, "< %s >", tr(s->last_diff == DIFF_EASY ? S_DIFF_EASY :
                                         (s->last_diff == DIFF_HARD ? S_DIFF_HARD : S_DIFF_MEDIUM)));
    m = add_item(SU_RULE, MI_TOGGLE, tr(S_RULE_BLOCKED), px + 3, py + 12, iw, 1, s->last_board >= 5);
    snprintf(m->vbuf, 48, "< %s >", tr(s->block_rule ? S_ON : S_OFF));
    add_item(SU_BACK, MI_BUTTON, tr(S_BACK), px + 6, py + 16, 18, 3, true);
    add_item(SU_NEXT, MI_BUTTON, tr(S_NEXT), px + 62 - 24, py + 16, 18, 3, true);
}

static void build_profile(int cols, int rows)
{
    (void)rows;
    Settings *s = &g_tui.settings;
    bool pve = s->last_mode == MODE_PVE;
    int x0 = (cols - (CARD_W * 2 + 4)) / 2;
    if (x0 < 1) x0 = 1;
    int x1 = x0 + CARD_W + 4;
    MItem *m;
    m = add_item(PF_AV0, MI_CHOICE, NULL, x0 + 3, 4 + 11, CARD_W - 6, 1, true);
    m->centre = true; fmt_avatar(m->vbuf, s->profile[0].avatar);
    m = add_item(PF_NAME0, MI_TEXT, tr(S_NAME), x0 + 3, 4 + 13, CARD_W - 6, 1, true);
    m->buf = s->profile[0].name;
    if (!pve) {
        m = add_item(PF_AV1, MI_CHOICE, NULL, x1 + 3, 4 + 11, CARD_W - 6, 1, true);
        m->centre = true; fmt_avatar(m->vbuf, s->profile[1].avatar);
        m = add_item(PF_NAME1, MI_TEXT, tr(S_NAME), x1 + 3, 4 + 13, CARD_W - 6, 1, true);
        m->buf = s->profile[1].name;
    }
    add_item(PF_BACK, MI_BUTTON, tr(S_BACK), cols / 2 - 20, 22, 18, 3, true);
    add_item(PF_START, MI_BUTTON, tr(S_START), cols / 2 + 2, 22, 18, 3, true);
}

static void build_settings(int cols, int rows)
{
    (void)rows;
    Settings *s = &g_tui.settings;
    int px = (cols - 60) / 2, py = 3, iw = 54;
    MItem *m;
    m = add_item(ST_LANG, MI_CHOICE, tr(S_LANGUAGE), px + 3, py + 3, iw, 1, true);
    snprintf(m->vbuf, 48, "< %s >", tr(S_LANG_NAME));
    m = add_item(ST_MUSIC, MI_SLIDER, tr(S_MUSIC), px + 3, py + 4, iw, 1, true);
    m->percent = clamp_pct(pct_of(s->bgm_volume)); fmt_pct(m->vbuf, m->percent);
    m = add_item(ST_MUTE_MUSIC, MI_TOGGLE, tr(S_MUTE), px + 3, py + 5, iw, 1, true);
    snprintf(m->vbuf, 48, "< %s >", tr(s->bgm_muted ? S_ON : S_OFF));
    m = add_item(ST_SFX, MI_SLIDER, tr(S_SOUND_FX), px + 3, py + 6, iw, 1, true);
    m->percent = clamp_pct(pct_of(s->sfx_volume)); fmt_pct(m->vbuf, m->percent);
    m = add_item(ST_MUTE_SFX, MI_TOGGLE, tr(S_MUTE), px + 3, py + 7, iw, 1, true);
    snprintf(m->vbuf, 48, "< %s >", tr(s->sfx_muted ? S_ON : S_OFF));
    m = add_item(ST_MOTION, MI_TOGGLE, tr(S_REDUCED_MOTION), px + 3, py + 8, iw, 1, true);
    snprintf(m->vbuf, 48, "< %s >", tr(s->reduced_motion ? S_ON : S_OFF));
    add_item(ST_BACK, MI_BUTTON, tr(S_BACK), px + 20, py + 10, 20, 3, true);
}

static void build_saves(int cols, int rows)
{
    (void)rows;
    int cx0 = (cols - 78) / 2;
    if (cx0 < 1) cx0 = 1;
    bool load = g_tui.saves_mode == TSAVES_LOAD;
    for (int i = 0; i < SAVE_SLOTS; i++) {
        int col = i / 5, row = i % 5;
        add_item(i, MI_BUTTON, NULL, cx0 + col * 40, 2 + row * 5, 38, 5,
                 load ? slots[i].used : true);
    }
    add_item(SV_BACK, MI_BUTTON, tr(S_BACK), cols / 2 - 9, 27, 18, 1, true);
}

static void build(void)
{
    int cols, rows;
    term_size(&cols, &rows);
    nitems = 0;
    switch (g_tui.screen) {
    case TS_HOME: build_home(cols, rows); break;
    case TS_SETUP: build_setup(cols, rows); break;
    case TS_PROFILE: build_profile(cols, rows); break;
    case TS_SETTINGS: build_settings(cols, rows); break;
    case TS_SAVES: build_saves(cols, rows); break;
    default: break;
    }
    fix_focus();
}

/* Modal confirm box geometry (yes/first button index 0, no/second index 1). */
static void modal_box(int cols, int rows, int *x, int *y, int *w, int *h)
{
    const char *msg = modal == MODAL_QUIT ? tr(S_QUIT_Q) :
                      (modal == MODAL_DELETE ? tr(S_DELETE_Q) : tr(S_OVERWRITE_Q));
    *w = utf8_cell_width(msg) + 6;
    if (*w < 40) *w = 40;
    if (*w > cols - 2) *w = cols - 2;
    *h = 7;
    *x = (cols - *w) / 2;
    *y = (rows - *h) / 2;
}

static void modal_btn(int cols, int rows, int i, int *x, int *y, int *w)
{
    int bx, by, bw, bh;
    modal_box(cols, rows, &bx, &by, &bw, &bh);
    *w = 16;
    *y = by + 4;
    *x = i == 0 ? bx + bw / 2 - 17 : bx + bw / 2 + 1;
}

static const char *modal_label(int i)
{
    if (modal == MODAL_QUIT) return tr(i == 0 ? S_YES : S_NO);
    if (modal == MODAL_DELETE) return tr(i == 0 ? S_DELETE : S_CANCEL);
    return tr(i == 0 ? S_YES : S_NO);
}

/* ---------------------------------------------------------------- navigation */

/* Geometric focus move: nearest enabled item strictly in the requested direction. */
static void nav(int dx, int dy)
{
    if (focus < 0) return;
    MItem *f = &items[focus];
    int cx = f->x + f->w / 2, cy = f->y + f->h / 2;
    int best = -1, best_score = 1 << 30;
    for (int i = 0; i < nitems; i++) {
        if (i == focus || !items[i].enabled) continue;
        int ix = items[i].x + items[i].w / 2, iy = items[i].y + items[i].h / 2;
        int ddx = ix - cx, ddy = iy - cy;
        if (dy < 0 && ddy >= 0) continue;
        if (dy > 0 && ddy <= 0) continue;
        if (dx < 0 && ddx >= 0) continue;
        if (dx > 0 && ddx <= 0) continue;
        /* the primary axis of the move dominates, so Up/Down stay in their column and
         * Left/Right stay in their row whenever possible */
        int score = dy != 0 ? abs(ddy) + abs(ddx) * 8 : abs(ddx) + abs(ddy) * 8;
        if (score < best_score) { best_score = score; best = i; }
    }
    if (best >= 0) focus = best;
}

/* ---------------------------------------------------------------- actions */

static void go_back(void)
{
    switch (g_tui.screen) {
    case TS_HOME:
        modal = MODAL_QUIT; mfocus = 1;
        break;
    case TS_SETUP: tui_goto(TS_HOME); break;
    case TS_PROFILE: tui_goto(TS_SETUP); break;
    case TS_SETTINGS:
    case TS_SAVES:
        if (g_tui.prev_screen == TS_GAME && !g_tui.has_game) tui_goto(TS_HOME);
        else tui_goto(g_tui.prev_screen);
        break;
    default: break;
    }
}

static bool load_slot_into_game(int slot)
{
    Game tmp;
    if (!storage_load_game(slot, &tmp)) return false;
    g_tui.game = tmp;
    g_tui.has_game = true;
    return true;
}

static void save_slot(int slot)
{
    if (storage_save_game(slot, &g_tui.game)) {
        char b[96];
        snprintf(b, sizeof b, tr(S_SAVED_FMT), slot + 1);
        tui_toast(b);
        slots_refresh();
        tui_goto(TS_GAME);
    } else {
        tui_toast(tr(S_LOAD_FAILED));
    }
}

static void continue_game(void)
{
    if (g_tui.has_game) { tui_goto(TS_GAME); return; }
    int latest = storage_latest_slot();
    if (latest >= 0 && load_slot_into_game(latest)) { tui_goto(TS_GAME); return; }
    tui_toast(tr(S_LOAD_FAILED));
}

static void slot_action(int slot)
{
    bool used = slots[slot].used;
    if (g_tui.saves_mode == TSAVES_SAVE) {
        if (used) { modal = MODAL_OVERWRITE; modal_slot = slot; mfocus = 1; }
        else save_slot(slot);
        return;
    }
    if (!used) return;
    if (load_slot_into_game(slot)) tui_goto(TS_GAME);
    else tui_toast(tr(S_LOAD_FAILED));
}

static void start_match(void)
{
    Settings *s = &g_tui.settings;
    int n = s->last_mode == MODE_PVE ? 1 : 2;
    for (int i = 0; i < n; i++) {
        if (name_blank(s->profile[i].name)) {
            for (int k = 0; k < nitems; k++) {
                if (items[k].kind == MI_TEXT && items[k].buf == s->profile[i].name) focus = k;
            }
            tui_toast(tr(S_NAME_EMPTY));
            return;
        }
    }
    tui_save_settings();
    tui_start_new_game();
}

static void setup_adjust(int id, int dir)
{
    Settings *s = &g_tui.settings;
    switch (id) {
    case SU_MODE: s->last_mode = s->last_mode == MODE_PVE ? MODE_PVP : MODE_PVE; break;
    case SU_BOARD: {
        int idx = (board_index() + dir + 3) % 3;
        s->last_board = idx == 0 ? 3 : (idx == 1 ? 5 : 10);
        break;
    }
    case SU_DIFF: {
        int d = (int)s->last_diff;
        if (d > DIFF_HARD) d = DIFF_MEDIUM;
        s->last_diff = (Difficulty)((d + dir + DIFF_COUNT) % DIFF_COUNT);
        break;
    }
    case SU_RULE: s->block_rule = !s->block_rule; break;
    default: break;
    }
}

static float step_volume(float v, int dir)
{
    int p = pct_of(v) + 5 * dir;
    p = clamp_pct(p);
    return (float)p / 100.0f;
}

static void settings_set_slider(int id, int pct)
{
    Settings *s = &g_tui.settings;
    float v = (float)clamp_pct(pct) / 100.0f;
    if (id == ST_MUSIC) s->bgm_volume = v;
    else if (id == ST_SFX) s->sfx_volume = v;
    tui_save_settings();
}

static void settings_adjust(int id, int dir)
{
    Settings *s = &g_tui.settings;
    switch (id) {
    case ST_LANG: s->lang = (Lang)(((int)s->lang + dir + LANG_COUNT) % LANG_COUNT); break;
    case ST_MUSIC: s->bgm_volume = step_volume(s->bgm_volume, dir); break;
    case ST_MUTE_MUSIC: s->bgm_muted = !s->bgm_muted; break;
    case ST_SFX: s->sfx_volume = step_volume(s->sfx_volume, dir); break;
    case ST_MUTE_SFX: s->sfx_muted = !s->sfx_muted; break;
    case ST_MOTION: s->reduced_motion = !s->reduced_motion; break;
    default: return;
    }
    tui_save_settings();
}

/* Value change for CHOICE / TOGGLE / SLIDER items (dir +1 or -1). */
static void adjust(int id, int dir)
{
    switch (g_tui.screen) {
    case TS_SETUP: setup_adjust(id, dir); break;
    case TS_PROFILE:
        if (id == PF_AV0 || id == PF_AV1) {
            int i = id == PF_AV0 ? 0 : 1;
            int av = g_tui.settings.profile[i].avatar;
            g_tui.settings.profile[i].avatar = ((av + dir) % AVATAR_COUNT + AVATAR_COUNT) % AVATAR_COUNT;
        }
        break;
    case TS_SETTINGS: settings_adjust(id, dir); break;
    default: break;
    }
}

static void button_action(int id)
{
    switch (g_tui.screen) {
    case TS_HOME:
        switch (id) {
        case HM_PLAY: tui_goto(TS_SETUP); break;
        case HM_CONTINUE: continue_game(); break;
        case HM_LOAD: g_tui.saves_mode = TSAVES_LOAD; tui_goto(TS_SAVES); break;
        case HM_SETTINGS: tui_goto(TS_SETTINGS); break;
        case HM_QUIT: modal = MODAL_QUIT; mfocus = 1; break;
        default: break;
        }
        break;
    case TS_SETUP:
        if (id == SU_BACK) tui_goto(TS_HOME);
        else if (id == SU_NEXT) tui_goto(TS_PROFILE);
        break;
    case TS_PROFILE:
        if (id == PF_BACK) tui_goto(TS_SETUP);
        else if (id == PF_START) start_match();
        break;
    case TS_SETTINGS:
        if (id == ST_BACK) go_back();
        break;
    case TS_SAVES:
        if (id == SV_BACK) go_back();
        else if (id >= 0 && id < SAVE_SLOTS) slot_action(id);
        break;
    default: break;
    }
}

/* Activate the item at index i (click, Enter, Space). */
static void activate_index(int i)
{
    MItem *m = &items[i];
    if (!m->enabled) return;
    focus = i;
    if (m->kind == MI_TEXT) { editing = true; return; }
    if (m->kind == MI_BUTTON) { button_action(m->id); return; }
    adjust(m->id, 1);
}

static void delete_focus(void)
{
    if (focus < 0) return;
    int slot = items[focus].id;
    if (g_tui.screen != TS_SAVES || slot < 0 || slot >= SAVE_SLOTS || !slots[slot].used) return;
    modal = MODAL_DELETE; modal_slot = slot; mfocus = 1;
}

static void modal_yes(void)
{
    int m = modal, slot = modal_slot;
    modal = MODAL_NONE;
    switch (m) {
    case MODAL_QUIT: g_tui.quit = true; break;
    case MODAL_DELETE: storage_delete_slot(slot); slots_refresh(); break;
    case MODAL_OVERWRITE: save_slot(slot); break;   /* may switch to the game screen */
    default: break;
    }
}

static void modal_choose(int i)
{
    if (i == 0) modal_yes();
    else modal = MODAL_NONE;
}

/* ---------------------------------------------------------------- text editing */

static bool edit_key(const TermEvent *ev)
{
    MItem *m = focus >= 0 ? &items[focus] : NULL;
    if (!m || m->kind != MI_TEXT || !m->buf) { editing = false; return false; }
    switch (ev->key) {
    case TK_CHAR: name_append(m->buf, ev->ch); return true;
    case TK_BACKSPACE: name_backspace(m->buf); return true;
    case TK_ENTER: case TK_ESC: editing = false; return true;
    case TK_UP: case TK_DOWN: editing = false; return false;  /* finish, then navigate */
    default: return false;
    }
}

/* ---------------------------------------------------------------- input */

static void menu_mouse(const TermEvent *ev)
{
    int cols, rows;
    term_size(&cols, &rows);
    if (modal != MODAL_NONE) {
        if (ev->mkind != TM_PRESS || ev->mbutton != 0) return;
        for (int i = 0; i < 2; i++) {
            int x, y, w;
            modal_btn(cols, rows, i, &x, &y, &w);
            if (ev->mx >= x && ev->mx < x + w && ev->my == y) { mfocus = i; modal_choose(i); return; }
        }
        return;
    }
    if (ev->mkind != TM_PRESS || ev->mbutton != 0) return;
    editing = false;
    for (int i = 0; i < nitems; i++) {
        MItem *m = &items[i];
        if (!m->enabled) continue;
        if (ev->mx < m->x || ev->mx >= m->x + m->w || ev->my < m->y || ev->my >= m->y + m->h) continue;
        if (m->kind == MI_SLIDER) {
            focus = i;
            int bx = slider_barx(m);
            if (ev->mx >= bx && ev->mx < bx + SL_BARW) {
                int pct = (ev->mx - bx) * 100 / (SL_BARW - 1);
                settings_set_slider(m->id, ((pct + 2) / 5) * 5);
            }
            return;
        }
        activate_index(i);
        return;
    }
}

static void modal_key(const TermEvent *ev)
{
    int pl = -1;
    ActionType a = tui_key_action(ev, &pl);
    switch (a) {
    case ACT_LEFT: case ACT_RIGHT: case ACT_UP: case ACT_DOWN: mfocus = 1 - mfocus; break;
    case ACT_CONFIRM: modal_choose(mfocus); break;
    case ACT_BACK: case ACT_HOME: modal = MODAL_NONE; break;
    default: break;
    }
}

/* ---------------------------------------------------------------- public API */

void tui_menu_enter(TuiScreen s)
{
    (void)s;
    editing = false;
    modal = MODAL_NONE;
    focus = -1;
    if (g_tui.screen == TS_HOME) home_has_save = storage_latest_slot() >= 0;
    if (g_tui.screen == TS_SAVES) slots_refresh();
    build();
}

void tui_menu_event(const TermEvent *ev)
{
    build();
    if (ev->type == TEV_MOUSE) { menu_mouse(ev); return; }
    if (ev->type != TEV_KEY) return;
    if (modal != MODAL_NONE) { modal_key(ev); return; }
    if (editing && edit_key(ev)) return;

    /* typing a non-navigation character starts editing a focused text field */
    if (focus >= 0 && items[focus].kind == MI_TEXT && ev->key == TK_CHAR && !is_nav_char(ev->ch) &&
        items[focus].enabled) {
        editing = true;
        name_append(items[focus].buf, ev->ch);
        return;
    }
    if (g_tui.screen == TS_SAVES &&
        (ev->key == TK_DELETE || (ev->key == TK_CHAR && (ev->ch == 'x' || ev->ch == 'X')))) {
        delete_focus();
        return;
    }

    int pl = -1;
    ActionType a = tui_key_action(ev, &pl);
    bool value_item = focus >= 0 && (items[focus].kind == MI_CHOICE || items[focus].kind == MI_TOGGLE ||
                                     items[focus].kind == MI_SLIDER);
    switch (a) {
    case ACT_UP: nav(0, -1); break;
    case ACT_DOWN: nav(0, 1); break;
    case ACT_LEFT:
        if (value_item) adjust(items[focus].id, -1);
        else nav(-1, 0);
        break;
    case ACT_RIGHT:
        if (value_item) adjust(items[focus].id, 1);
        else nav(1, 0);
        break;
    case ACT_CONFIRM:
        if (focus >= 0) activate_index(focus);
        break;
    case ACT_BACK:
        go_back();
        break;
    case ACT_HOME:
        if (g_tui.screen != TS_HOME) tui_goto(TS_HOME);
        break;
    default:
        break;
    }
}

void tui_menu_update(double dt)
{
    (void)dt;
}

/* ---------------------------------------------------------------- drawing */

static void draw_item(const MItem *m, bool focused)
{
    if (m->kind == MI_TEXT) {
        TColor lab = focused ? TC_GOLD : TC_TEXT;
        term_print(m->x + 1, m->y, m->label, lab, TC_BG, focused ? TATTR_BOLD : TATTR_NONE);
        int fx = m->x + m->w - (FIELD_W + 2);
        TColor fc = focused ? TC_GOLD : TC_BORDER;
        term_put(fx, m->y, '[', fc, TC_BG, 0);
        term_put(fx + FIELD_W + 1, m->y, ']', fc, TC_BG, 0);
        int used = 0;
        if (m->buf[0] == '\0' && !(editing && focused)) {
            term_print(fx + 1, m->y, tr(S_ENTER_NAME), TC_BORDER, TC_BG, 0);
        } else {
            const char *t = tail_cells(m->buf, FIELD_W - (editing && focused ? 1 : 0));
            used = term_print(fx + 1, m->y, t, TC_TEXT, TC_BG, 0);
        }
        if (editing && focused) {
            int blink = ((int)(term_now() * 2.0)) & 1;
            term_put(fx + 1 + used, m->y, blink ? '_' : ' ', TC_GOLD, TC_BG, TATTR_BOLD);
            used++;
        }
        if (used < FIELD_W) term_fill(fx + 1 + used, m->y, FIELD_W - used, 1, ' ', TC_TEXT, TC_BG);
        return;
    }
    if (m->h >= 3) {
        TColor edge = !m->enabled ? TC_BORDER : (focused ? TC_GOLD : TC_BORDER);
        TColor fill = (focused && m->enabled) ? 0x3D4329u : TC_PANEL;
        term_box(m->x, m->y, m->w, m->h, focused ? 1 : 0, edge, fill);
        if (!m->label) return;
        int ty = m->y + m->h / 2;
        TColor fg = !m->enabled ? TC_BORDER : (focused ? TC_GOLD : TC_TEXT);
        int tw = utf8_cell_width(m->label);
        int tx = m->x + (m->w - tw) / 2;
        term_print(tx, ty, m->label, fg, fill, focused ? TATTR_BOLD : TATTR_NONE);
        if (focused && m->enabled) {
            term_put(tx - 2, ty, 0x25B6u, TC_GOLD, fill, 0);      /* gold marker */
            term_put(tx + tw + 1, ty, 0x25C0u, TC_GOLD, fill, 0);
        }
        return;
    }
    /* single-row styles */
    bool bar = focused && m->enabled;
    TColor fg = !m->enabled ? TC_BORDER : TC_TEXT;
    TColor bgc = bar ? TC_GOLD : TC_BG;
    int attr = bar ? TATTR_BOLD : TATTR_NONE;
    if (bar) {
        fg = TC_BG;
        term_fill(m->x, m->y, m->w, 1, ' ', TC_BG, TC_GOLD);
        term_put(m->x - 2, m->y, 0x25B6u, TC_GOLD, TC_BG, 0);
        term_put(m->x + m->w + 1, m->y, 0x25C0u, TC_GOLD, TC_BG, 0);
    }
    switch (m->kind) {
    case MI_BUTTON:
        put_centred(m->x, m->w, m->y, m->label, fg, bgc, attr);
        break;
    case MI_CHOICE:
    case MI_TOGGLE:
        if (m->centre) {
            put_centred(m->x, m->w, m->y, m->value, fg, bgc, attr);
        } else {
            term_print(m->x + 1, m->y, m->label, fg, bgc, attr);
            put_right(m->x + m->w - 1, m->y, m->value, fg, bgc, attr);
        }
        break;
    case MI_SLIDER: {
        term_print(m->x + 1, m->y, m->label, fg, bgc, attr);
        int bx = slider_barx(m);
        int filled = m->percent * SL_BARW / 100;
        for (int k = 0; k < SL_BARW; k++) {
            if (k < filled) term_put(bx + k, m->y, 0x2588u, bar ? TC_BG : TC_GOLD, bgc, 0);
            else term_put(bx + k, m->y, 0x2591u, bar ? TC_BG : TC_BORDER, bgc, 0);
        }
        put_right(m->x + m->w - 1, m->y, m->value, fg, bgc, attr);
        break;
    }
    default:
        break;
    }
}

static void draw_items(void)
{
    for (int i = 0; i < nitems; i++) draw_item(&items[i], i == focus);
}

static void draw_hint(int cols, int rows, const char *hint)
{
    put_centred(0, cols, rows - 2, hint, TC_DIM, TC_BG, 0);
}

/* Big block-letter title using 5-row glyphs. */
static const char *glyph_row(char ch, int r)
{
    static const char *const C[5] = {".####", "#....", "#....", "#....", ".####"};
    static const char *const A[5] = {".###.", "#...#", "#####", "#...#", "#...#"};
    static const char *const R[5] = {"####.", "#...#", "####.", "#..#.", "#...#"};
    static const char *const O[5] = {".###.", "#...#", "#...#", "#...#", ".###."};
    static const char *const T[5] = {"#####", "..#..", "..#..", "..#..", "..#.."};
    static const char *const E[5] = {"#####", "#....", "####.", "#....", "#####"};
    static const char *const M[5] = {"#...#", "##.##", "#.#.#", "#...#", "#...#"};
    static const char *const P[5] = {"####.", "#...#", "####.", "#....", "#...."};
    static const char *const L[5] = {"#....", "#....", "#....", "#....", "#####"};
    const char *const *g = NULL;
    switch (ch) {
    case 'C': g = C; break; case 'A': g = A; break; case 'R': g = R; break;
    case 'O': g = O; break; case 'T': g = T; break; case 'E': g = E; break;
    case 'M': g = M; break; case 'P': g = P; break; case 'L': g = L; break;
    default: return "     ";
    }
    return g[r];
}

static void draw_title(int cols, int y)
{
    const char *text = "CARO TEMPLE";
    int w = 0;
    for (const char *s = text; *s; s++) w += (*s == ' ') ? 3 : 6;
    w -= 1;
    int x0 = (cols - w) / 2;
    for (int r = 0; r < 5; r++) {
        int x = x0;
        for (const char *s = text; *s; s++) {
            if (*s == ' ') { x += 3; continue; }
            const char *row = glyph_row(*s, r);
            for (int c = 0; row[c]; c++) {
                if (row[c] == '#') term_put(x + c, y + r, 0x2588u, TC_GOLD, TC_BG, 0);
            }
            x += 6;
        }
    }
}

static void draw_frame(int x, int y, int w, int h, TColor edge, const char *title)
{
    term_box(x, y, w, h, 3, edge, TC_PANEL);
    if (title) put_centred(x, w, y, title, TC_GOLD, TC_PANEL, TATTR_BOLD);
}

static void draw_home(int cols, int rows)
{
    int oy = rows > 30 ? (rows - 30) / 2 : 0;
    draw_title(cols, 1 + oy);
    put_centred(0, cols, 7 + oy, tr(S_GAME_SUBTITLE), TC_DIM, TC_BG, 0);
    bool bob = !g_tui.settings.reduced_motion && (((int)(term_now() * 2.0)) & 1);
    tui_draw_mascot(2, rows - 10, 0, TC_BG, bob, true);
    tui_draw_mascot(cols - 18, rows - 10, 1, TC_BG, bob, false);
    draw_items();
    draw_hint(cols, rows, tr(S_HINT_MENU));
}

static void draw_setup(int cols, int rows)
{
    int px = (cols - 62) / 2, py = 3;
    draw_frame(px, py, 62, 21, TC_BORDER, NULL);
    put_centred(px, 62, py + 1, tr(S_NEW_GAME), TC_GOLD, TC_PANEL, TATTR_BOLD);
    bool pve = g_tui.settings.last_mode == MODE_PVE;
    put_centred(px, 62, py + 4, tr(pve ? S_MODE_PVE_DESC : S_MODE_PVP_DESC), TC_DIM, TC_PANEL, 0);
    int bi = board_index();
    put_centred(px, 62, py + 7, tr(bi == 0 ? S_BOARD_3_DESC : (bi == 1 ? S_BOARD_5_DESC : S_BOARD_10_DESC)),
                TC_DIM, TC_PANEL, 0);
    put_centred(px, 62, py + 13, tr(S_RULE_BLOCKED_DESC), TC_DIM, TC_PANEL, 0);
    draw_items();
    draw_hint(cols, rows, tr(S_HINT_MENU));
}

static void draw_card(int x, int y, TColor edge, const char *title, const char *plays, int avatar,
                      const char *name, bool show_err)
{
    draw_frame(x, y, CARD_W, CARD_H, edge, NULL);
    put_centred(x, CARD_W, y + 1, title, edge, TC_PANEL, TATTR_BOLD);
    put_centred(x, CARD_W, y + 2, plays, TC_DIM, TC_PANEL, 0);
    tui_draw_mascot(x + (CARD_W - 16) / 2, y + 3, avatar, TC_PANEL, false, true);
    if (show_err) put_centred(x, CARD_W, y + 14, tr(S_NAME_EMPTY), TC_DANGER, TC_PANEL, 0);
    (void)name;
}

static void draw_profile(int cols, int rows)
{
    Settings *s = &g_tui.settings;
    bool pve = s->last_mode == MODE_PVE;
    int x0 = (cols - (CARD_W * 2 + 4)) / 2;
    if (x0 < 1) x0 = 1;
    int x1 = x0 + CARD_W + 4;
    put_centred(0, cols, 1, tr(S_PROFILE_TITLE), TC_GOLD, TC_BG, TATTR_BOLD);
    draw_card(x0, 4, TC_FIRE, tr(S_PLAYER_1), tr(S_PLAYS_FIRE), s->profile[0].avatar, s->profile[0].name,
              name_blank(s->profile[0].name));
    if (pve) {
        draw_card(x1, 4, TC_WATER, tr(S_COMPUTER), tr(S_PLAYS_WATER), 5, tr(S_COMPUTER), false);
        put_centred(x1, CARD_W, 17, tr(S_COMPUTER), TC_TEXT, TC_PANEL, 0);
    } else {
        draw_card(x1, 4, TC_WATER, tr(S_PLAYER_2), tr(S_PLAYS_WATER), s->profile[1].avatar, s->profile[1].name,
                  name_blank(s->profile[1].name));
    }
    draw_items();
    draw_hint(cols, rows, editing ? tr(S_HINT_TEXT) : tr(S_HINT_MENU));
    (void)rows;
}

static void draw_settings(int cols, int rows)
{
    int px = (cols - 60) / 2, py = 3;
    draw_frame(px, py, 60, 14, TC_BORDER, NULL);
    put_centred(px, 60, py + 1, tr(S_SETTINGS), TC_GOLD, TC_PANEL, TATTR_BOLD);
    draw_frame(px, py + 15, 60, 7, TC_BORDER, NULL);
    put_centred(px, 60, py + 16, tr(S_CONTROLS), TC_GOLD, TC_PANEL, TATTR_BOLD);
    term_print(px + 3, py + 17, tr(S_CTRL_MOUSE), TC_TEXT, TC_PANEL, 0);
    term_print(px + 3, py + 18, tr(S_CTRL_P1), TC_TEXT, TC_PANEL, 0);
    term_print(px + 3, py + 19, tr(S_CTRL_P2), TC_TEXT, TC_PANEL, 0);
    term_print(px + 3, py + 20, tr(S_CTRL_MENU), TC_TEXT, TC_PANEL, 0);
    draw_items();
    draw_hint(cols, rows, tr(S_HINT_MENU));
}

static void draw_saves(int cols, int rows)
{
    bool load = g_tui.saves_mode == TSAVES_LOAD;
    put_centred(0, cols, 0, tr(load ? S_LOAD_TITLE : S_SAVE_GAME), TC_GOLD, TC_BG, TATTR_BOLD);
    int cx0 = (cols - 78) / 2;
    if (cx0 < 1) cx0 = 1;
    bool any = false;
    for (int i = 0; i < nitems && i < SAVE_SLOTS; i++) {
        const MItem *m = &items[i];
        bool foc = i == focus;
        draw_item(m, foc);
        const SlotInfo *si = &slots[i];
        int x = m->x + 2, w = m->w - 4;
        TColor fg = m->enabled ? TC_TEXT : TC_BORDER;
        char line[96];
        if (!si->used) {
            snprintf(line, sizeof line, tr(S_SLOT_FMT), i + 1);
            term_print(x, m->y + 1, line, TC_BORDER, TC_PANEL, 0);
            put_right(x + w, m->y + 1, tr(S_EMPTY_SLOT), TC_BORDER, TC_PANEL, 0);
            continue;
        }
        any = true;
        snprintf(line, sizeof line, tr(S_SLOT_FMT), i + 1);
        term_print(x, m->y + 1, line, foc ? TC_GOLD : fg, TC_PANEL, TATTR_BOLD);
        char when[32] = "";
        time_t t = (time_t)si->saved_at;
        struct tm *tmv = localtime(&t);
        if (tmv) strftime(when, sizeof when, "%Y-%m-%d %H:%M", tmv);
        put_right(x + w, m->y + 1, when, TC_DIM, TC_PANEL, 0);
        char mode[64];
        if (si->mode == MODE_PVE) {
            const char *d = si->diff == DIFF_EASY ? tr(S_DIFF_EASY) :
                            (si->diff == DIFF_HARD ? tr(S_DIFF_HARD) : tr(S_DIFF_MEDIUM));
            snprintf(mode, sizeof mode, "%s \xC2\xB7 %s \xC2\xB7 %s", tr(S_MODE_PVE),
                     tr(si->board_n == 3 ? S_BOARD_3 : (si->board_n == 5 ? S_BOARD_5 : S_BOARD_10)), d);
        } else {
            snprintf(mode, sizeof mode, "%s \xC2\xB7 %s", tr(S_MODE_PVP),
                     tr(si->board_n == 3 ? S_BOARD_3 : (si->board_n == 5 ? S_BOARD_5 : S_BOARD_10)));
        }
        term_print(x, m->y + 2, mode, TC_TEXT, TC_PANEL, 0);
        char n1[40], n2[40], names[96];
        clip_cells(n1, sizeof n1, si->p1, 15);
        clip_cells(n2, sizeof n2, si->p2, 15);
        snprintf(names, sizeof names, "%s / %s", n1, n2);
        term_print(x, m->y + 3, names, TC_DIM, TC_PANEL, 0);
        char moves[32];
        snprintf(moves, sizeof moves, tr(S_MOVES_FMT), si->move_count);
        put_right(x + w, m->y + 3, moves, TC_DIM, TC_PANEL, 0);
    }
    if (load && !any) put_centred(0, cols, 1, tr(S_NO_SAVES), TC_DIM, TC_BG, 0);
    /* back button is the last item */
    for (int i = SAVE_SLOTS; i < nitems; i++) draw_item(&items[i], i == focus);
    draw_hint(cols, rows, tr(S_HINT_MENU));
}

static void draw_modal(int cols, int rows)
{
    int bx, by, bw, bh;
    modal_box(cols, rows, &bx, &by, &bw, &bh);
    const char *msg = modal == MODAL_QUIT ? tr(S_QUIT_Q) :
                      (modal == MODAL_DELETE ? tr(S_DELETE_Q) : tr(S_OVERWRITE_Q));
    term_box(bx, by, bw, bh, 3, TC_GOLD, TC_PANEL);
    put_centred(bx, bw, by + 1, tr(S_CONFIRM), TC_GOLD, TC_PANEL, TATTR_BOLD);
    put_centred(bx, bw, by + 2, msg, TC_TEXT, TC_PANEL, 0);
    for (int i = 0; i < 2; i++) {
        int x, y, w;
        modal_btn(cols, rows, i, &x, &y, &w);
        bool foc = i == mfocus;
        MItem tmp;
        memset(&tmp, 0, sizeof tmp);
        tmp.kind = MI_BUTTON; tmp.label = modal_label(i); tmp.enabled = true;
        tmp.x = x; tmp.y = y; tmp.w = w; tmp.h = 1;
        draw_item(&tmp, foc);
    }
}

void tui_menu_draw(void)
{
    int cols, rows;
    term_size(&cols, &rows);
    build();
    switch (g_tui.screen) {
    case TS_HOME: draw_home(cols, rows); break;
    case TS_SETUP: draw_setup(cols, rows); break;
    case TS_PROFILE: draw_profile(cols, rows); break;
    case TS_SETTINGS: draw_settings(cols, rows); break;
    case TS_SAVES: draw_saves(cols, rows); break;
    default: break;
    }
    if (modal != MODAL_NONE) draw_modal(cols, rows);
}
