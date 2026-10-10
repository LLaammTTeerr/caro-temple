/* theme.h - design tokens (see docs/DESIGN.md) and text helpers (gui/theme.c). */
#ifndef CARO_THEME_H
#define CARO_THEME_H

#include "raylib.h"

/* Palette */
#define COL_STONE_900  (Color){ 24,  27,  17, 255}   /* deepest background */
#define COL_STONE_800  (Color){ 43,  47,  30, 255}   /* #2B2F1E panel fill */
#define COL_STONE_700  (Color){ 61,  67,  41, 255}   /* #3D4329 raised fill */
#define COL_STONE_600  (Color){ 86,  94,  58, 255}   /* #565E3A borders / bricks */
#define COL_STONE_400  (Color){130, 138,  96, 255}   /* light brick edge */
#define COL_MOSS       (Color){ 92, 122,  46, 255}   /* #5C7A2E vines */
#define COL_MOSS_LIGHT (Color){140, 176,  72, 255}
#define COL_GOLD       (Color){232, 194,  90, 255}   /* #E8C25A titles, primary CTA, focus */
#define COL_GOLD_DARK  (Color){150, 112,  40, 255}
#define COL_FIRE       (Color){255,  90,  31, 255}   /* #FF5A1F */
#define COL_FIRE_LIGHT (Color){255, 177,  59, 255}   /* #FFB13B */
#define COL_WATER      (Color){ 47, 168, 255, 255}   /* #2FA8FF */
#define COL_WATER_LIGHT (Color){155, 231, 255, 255}  /* #9BE7FF */
#define COL_TEXT       (Color){244, 236, 208, 255}   /* #F4ECD0 */
#define COL_TEXT_DIM   (Color){178, 170, 140, 255}
#define COL_DANGER     (Color){217,  69,  43, 255}   /* #D9452B */
#define COL_SHADOW     (Color){  0,   0,   0, 140}
#define COL_FOCUS      (Color){255, 255, 255, 255}   /* white outline on hover / focus */

/* Type scale (px in virtual 1280x720) */
#define FS_HUGE   96   /* title */
#define FS_H1     56
#define FS_H2     40
#define FS_BODY   32
#define FS_SMALL  24

/* Spacing (8 px grid) */
#define SP_1 8
#define SP_2 16
#define SP_3 24
#define SP_4 32
#define SP_6 48

extern Font g_font;            /* VT323, includes Vietnamese glyphs */

void theme_load(void);         /* load font from assets (searches next to exe, ./assets, ../assets); falls back to raylib default */
void theme_unload(void);

Vector2 text_measure(const char *s, float size);
void text_draw(const char *s, float x, float y, float size, Color c);
void text_draw_shadow(const char *s, float x, float y, float size, Color c);    /* 3 px hard drop shadow */
void text_draw_bold(const char *s, float x, float y, float size, Color c);      /* faux-bold: draw at +1px x */
void text_draw_outline(const char *s, float x, float y, float size, Color c, Color outline, int px);
/* Centered at (cx, y-top). align: 0 left, 1 center, 2 right relative to x */
void text_draw_aligned(const char *s, float x, float y, float size, Color c, int align);
/* Word-wrap into width; returns height used. */
float text_draw_wrapped(const char *s, Rectangle box, float size, Color c, int align);

Color color_lerp(Color a, Color b, float t);
float ease_out_cubic(float t);
float ease_in_out(float t);

#endif
