/* theme.c - VT323 font loading and text helpers (declared in theme.h). */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "theme.h"

#define FONT_REL "assets/fonts/VT323-Regular.ttf"
#define FONT_BASE 64
#define FONT_FILTER TEXTURE_FILTER_BILINEAR
#define SPAN_CAP 2048   /* max bytes measured/drawn as one span (wrapping) */

Font g_font;
static bool font_is_custom;

/* Explicit glyph set: ASCII, Latin-1, Latin Extended-A, Vietnamese, and a few symbols. */
static int build_codepoints(int *cp, int cap)
{
    static const int ranges[][2] = {
        {32, 126}, {160, 255}, {256, 383}, {0x1A0, 0x1A1}, {0x1AF, 0x1B0},
        {0x1EA0, 0x1EF9}, {0x2022, 0x2022}, {0x2026, 0x2026}, {0x2190, 0x2193},
        {0x25B6, 0x25B6}, {0x25C0, 0x25C0}
    };
    int n = 0;
    for (size_t i = 0; i < sizeof ranges / sizeof ranges[0]; i++)
        for (int c = ranges[i][0]; c <= ranges[i][1] && n < cap; c++) cp[n++] = c;
    return n;
}

void theme_load(void)
{
    static int cps[512];
    char path[600];
    int n = build_codepoints(cps, 512);
    font_is_custom = false;
    g_font = GetFontDefault();
    snprintf(path, sizeof path, "%s%s", GetApplicationDirectory(), FONT_REL);
    for (int i = 0; i < 3; i++) {
        const char *p = i == 0 ? path : (i == 1 ? FONT_REL : "../" FONT_REL);
        if (!FileExists(p)) continue;
        Font f = LoadFontEx(p, FONT_BASE, cps, n);
        if (f.texture.id != 0) {
            g_font = f;
            font_is_custom = true;
            SetTextureFilter(g_font.texture, FONT_FILTER);
            break;
        }
    }
}

void theme_unload(void)
{
    if (font_is_custom) UnloadFont(g_font);
    font_is_custom = false;
    g_font = GetFontDefault();
}

/* Raw draw at whole-pixel coordinates (keeps the pixel font crisp). */
static void draw_at(const char *s, float x, float y, float size, Color c)
{
    DrawTextEx(g_font, s, (Vector2){ floorf(x + 0.5f), floorf(y + 0.5f) }, size, 0, c);
}

/* Width of bytes [a, b) (UTF-8 safe; over-long spans are cut at a codepoint boundary). */
static float span_width(const char *a, const char *b, float size)
{
    char buf[SPAN_CAP];
    size_t len = (size_t)(b - a), n = len;
    if (n >= SPAN_CAP) {
        n = SPAN_CAP - 1;
        while (n > 0 && ((unsigned char)a[n] & 0xC0) == 0x80) n--;
    }
    memcpy(buf, a, n);
    buf[n] = '\0';
    return text_measure(buf, size).x;
}

static void draw_span(const char *a, const char *b, float x, float y, float size, Color c)
{
    char buf[SPAN_CAP];
    size_t len = (size_t)(b - a), n = len;
    if (n >= SPAN_CAP) {
        n = SPAN_CAP - 1;
        while (n > 0 && ((unsigned char)a[n] & 0xC0) == 0x80) n--;
    }
    memcpy(buf, a, n);
    buf[n] = '\0';
    draw_at(buf, x, y, size, c);
}

Vector2 text_measure(const char *s, float size)
{
    if (!s) return (Vector2){ 0, 0 };
    return MeasureTextEx(g_font, s, size, 0);
}

void text_draw(const char *s, float x, float y, float size, Color c)
{
    if (s) draw_at(s, x, y, size, c);
}

void text_draw_shadow(const char *s, float x, float y, float size, Color c)
{
    if (!s) return;
    draw_at(s, x + 3, y + 3, size, COL_SHADOW);
    draw_at(s, x, y, size, c);
}

void text_draw_bold(const char *s, float x, float y, float size, Color c)
{
    if (!s) return;
    draw_at(s, x, y, size, c);
    draw_at(s, x + 1, y, size, c);
    if (size >= 48) {
        draw_at(s, x, y + 1, size, c);
        draw_at(s, x + 1, y + 1, size, c);
    }
}

void text_draw_outline(const char *s, float x, float y, float size, Color c, Color outline, int px)
{
    if (!s) return;
    for (int dx = -px; dx <= px; dx++)
        for (int dy = -px; dy <= px; dy++)
            if (dx || dy) draw_at(s, x + dx, y + dy, size, outline);
    draw_at(s, x, y, size, c);
}

void text_draw_aligned(const char *s, float x, float y, float size, Color c, int align)
{
    if (!s) return;
    float w = text_measure(s, size).x;
    float dx = align == 1 ? x - w / 2 : (align == 2 ? x - w : x);
    draw_at(s, dx, y, size, c);
}

/* Largest codepoint boundary p in (a, b] with width(a..p) <= w; at least one codepoint. */
static const char *fit_prefix(const char *a, const char *b, float w, float size)
{
    const char *best = NULL, *p = a;
    while (p < b) {
        const char *q = p + 1;
        while (q < b && ((unsigned char)*q & 0xC0) == 0x80) q++;
        if (span_width(a, q, size) > w) break;
        best = q;
        p = q;
    }
    if (!best) {
        best = a + 1;
        while (best < b && ((unsigned char)*best & 0xC0) == 0x80) best++;
    }
    return best;
}

static void emit_line(const char *a, const char *b, Rectangle box, float y, float size, Color c, int align)
{
    float x = box.x;
    if (align == 1 || align == 2) {
        float w = span_width(a, b, size);
        x = align == 1 ? box.x + (box.width - w) / 2 : box.x + box.width - w;
    }
    draw_span(a, b, x, y, size, c);
}

float text_draw_wrapped(const char *s, Rectangle box, float size, Color c, int align)
{
    if (!s || box.width <= 0) return 0;
    float lh = text_measure("Mg", size).y;
    if (lh <= 0) lh = size;
    float y = box.y;
    const char *line = NULL, *lend = NULL, *p = s;
    for (;;) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n') {
            if (line) {
                emit_line(line, lend, box, y, size, c, align);
                y += lh;
                line = NULL;
            } else if (*p == '\n') {
                y += lh; /* blank line */
            }
            if (*p == '\0') break;
            p++;
            continue;
        }
        const char *ws = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
        const char *we = p;
        if (line) {
            if (span_width(line, we, size) <= box.width) {
                lend = we;
                continue;
            }
            emit_line(line, lend, box, y, size, c, align);
            y += lh;
            line = NULL;
            p = ws; /* re-read this word on the next line */
            continue;
        }
        line = ws;
        lend = we;
        /* hard-break a word that is wider than the box, on codepoint boundaries */
        while (span_width(line, lend, size) > box.width) {
            const char *cut = fit_prefix(line, lend, box.width, size);
            if (cut >= lend) break;
            emit_line(line, cut, box, y, size, c, align);
            y += lh;
            line = cut;
        }
    }
    return y - box.y;
}

Color color_lerp(Color a, Color b, float t)
{
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return (Color){ (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                    (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t) };
}

float ease_out_cubic(float t)
{
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    t = 1 - t;
    return 1 - t * t * t;
}

float ease_in_out(float t)
{
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return t < 0.5f ? 2 * t * t : 1 - powf(-2 * t + 2, 2) / 2;
}
