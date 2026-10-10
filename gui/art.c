/* art.c - procedurally generated pixel art (see art.h).
 * Backgrounds are baked once into 320x180 "art pixels" (1 art px = 4 virtual px) and drawn scaled x4 with
 * point filtering. Animated bits are drawn each frame as art-px rectangles snapped to the 4 px grid, or as
 * tinted overlay textures (light shaft, glows). Every random choice uses a fixed-seed PRNG, so the art is
 * identical on every run. */

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "art.h"
#include "theme.h"
#include "app.h"
#include "../core/i18n.h"

#define AW 320
#define AH 180
#define PX 4                 /* virtual px per art px */
#define TITLE_FRAMES 32      /* shine sweep frames (frame 0 = resting title) */

typedef struct {
    Color *px;
    int w, h;
} Canvas;

/* ---------- module state ---------- */
static bool s_inited;
static Color *s_buf;                          /* scratch bake buffer, AW*AH */
static Texture2D s_bg[BG_COUNT];
static Texture2D s_mascot[MASCOT_COUNT][3];   /* 0 normal, 1 blink, 2 flicker/shimmer */
static Texture2D s_glow;                      /* 32x32 soft radial glow (white, alpha) */
static Texture2D s_shaft;                     /* 320x180 diagonal light shaft (white-warm, alpha) */

static bool s_title_ok;
static Texture2D s_title;
static int s_title_size, s_title_w, s_title_h;
static const char *s_title_str;

/* ---------- small helpers ---------- */
static Color rgb(int r, int g, int b)
{
    return (Color){ (unsigned char)r, (unsigned char)g, (unsigned char)b, 255 };
}

static Color hex(unsigned v)
{
    return rgb((int)((v >> 16) & 255u), (int)((v >> 8) & 255u), (int)(v & 255u));
}

static Color ca(Color c, int a)
{
    if (a < 0) a = 0;
    if (a > 255) a = 255;
    c.a = (unsigned char)a;
    return c;
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static Color shift(Color c, int d)
{
    return rgb(clampi(c.r + d, 0, 255), clampi(c.g + d, 0, 255), clampi(c.b + d, 0, 255));
}

static Color mul(Color c, float f)
{
    return rgb((int)(c.r * f), (int)(c.g * f), (int)(c.b * f));
}

static float smoothf(float a, float b, float x)
{
    float t = (x - a) / (b - a);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

/* xorshift32 PRNG for baking (seed must be non-zero) */
static unsigned xs(unsigned *s)
{
    unsigned x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static int rnd_range(unsigned *s, int lo, int hi)
{
    return lo + (int)(xs(s) % (unsigned)(hi - lo + 1));
}

static float rnd01(unsigned *s)
{
    return (float)(xs(s) & 0xFFFFFFu) / 16777216.0f;
}

/* stateless hash for per-frame animation (deterministic per cell and frame) */
static unsigned hash2(int a, int b)
{
    unsigned h = (unsigned)a * 374761393u + (unsigned)b * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

/* art-pixel rectangle in virtual coordinates (snapped to the 4 px grid) */
static void apx(int x, int y, int w, int h, Color c)
{
    DrawRectangle(x * PX, y * PX, w * PX, h * PX, c);
}

static void cv_put(Canvas *c, int x, int y, Color col)
{
    if (x >= 0 && y >= 0 && x < c->w && y < c->h) c->px[y * c->w + x] = col;
}

static void cv_blend(Canvas *c, int x, int y, Color col, float a)
{
    if (x < 0 || y < 0 || x >= c->w || y >= c->h) return;
    Color d = c->px[y * c->w + x];
    Color o = color_lerp(d, col, a);
    o.a = 255;
    c->px[y * c->w + x] = o;
}

static void cv_rect(Canvas *c, int x, int y, int w, int h, Color col)
{
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++)
            cv_put(c, xx, yy, col);
}

static void cv_fill(Canvas *c, Color col)
{
    for (int i = 0; i < c->w * c->h; i++) c->px[i] = col;
}

/* multiply the whole canvas by f */
static void cv_scale_all(Canvas *c, float f)
{
    for (int i = 0; i < c->w * c->h; i++) c->px[i] = mul(c->px[i], f);
}

/* darken toward the edges: strength at the corners, none in the middle */
static void cv_vignette(Canvas *c, float strength)
{
    for (int y = 0; y < c->h; y++) {
        for (int x = 0; x < c->w; x++) {
            float dx = ((float)x - c->w * 0.5f) / (c->w * 0.5f);
            float dy = ((float)y - c->h * 0.5f) / (c->h * 0.5f);
            float d = sqrtf(dx * dx + dy * dy) / 1.414f;
            float f = 1.0f - strength * smoothf(0.45f, 1.0f, d);
            c->px[y * c->w + x] = mul(c->px[y * c->w + x], f);
        }
    }
}

/* darken a vertical band around cx (calm area behind menus or a board) */
static void cv_center_veil(Canvas *c, float cx, float halfw, float strength)
{
    for (int x = 0; x < c->w; x++) {
        float u = fabsf((float)x - cx) / halfw;
        float f = 1.0f - strength * (1.0f - smoothf(0.45f, 1.0f, u));
        for (int y = 0; y < c->h; y++)
            c->px[y * c->w + x] = mul(c->px[y * c->w + x], f);
    }
}

/* ---------- stone painters ---------- */
typedef struct {
    Color base, hi, lo, mortar, crack, moss, moss_hi;
    int bw, bh, jitter;
} WallPal;

static void paint_wall(Canvas *c, const WallPal *p, unsigned *seed)
{
    for (int row = 0, y0 = 0; y0 < c->h; row++, y0 += p->bh) {
        int off = (row & 1) ? p->bw / 2 : 0;
        for (int x0 = -off; x0 < c->w; x0 += p->bw) {
            Color base = shift(p->base, rnd_range(seed, -p->jitter, p->jitter));
            for (int dy = 0; dy < p->bh; dy++) {
                for (int dx = 0; dx < p->bw; dx++) {
                    Color col;
                    if (dx == p->bw - 1 || dy == p->bh - 1) {
                        col = p->mortar;
                    } else {
                        col = base;
                        if (dy == 0) col = color_lerp(col, p->hi, 0.5f);
                        else if (dy >= p->bh - 3) col = color_lerp(col, p->lo, 0.55f);
                        if (dx == 0) col = color_lerp(col, p->hi, 0.2f);
                    }
                    cv_put(c, x0 + dx, y0 + dy, col);
                }
            }
        }
    }
    /* grain */
    for (int i = 0; i < c->w * c->h; i++) {
        unsigned r = xs(seed);
        if (r % 6 == 0) c->px[i] = shift(c->px[i], ((r >> 8) & 1) ? 7 : -7);
    }
    /* cracks: short downward wandering lines */
    for (int k = 0; k < 9; k++) {
        int x = rnd_range(seed, 0, c->w - 1);
        int y = rnd_range(seed, 0, c->h - 1);
        int len = rnd_range(seed, 4, 11);
        for (int s = 0; s < len; s++) {
            cv_put(c, x, y, p->crack);
            if (rnd_range(seed, 0, 2) == 0) x += rnd_range(seed, -1, 1);
            if (rnd_range(seed, 0, 2) != 0) y += 1;
        }
    }
}

/* soft blobs of moss (or soot, or algae) with drips */
static void paint_patches(Canvas *c, unsigned *seed, int count, int rmin, int rmax,
                          Color col, Color hi, bool top_bias)
{
    for (int k = 0; k < count; k++) {
        float u = rnd01(seed);
        int cx = rnd_range(seed, 0, c->w - 1);
        int cy = top_bias ? (int)(u * u * c->h * 0.6f) : rnd_range(seed, 0, c->h - 1);
        int r = rnd_range(seed, rmin, rmax);
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                float d = sqrtf((float)(dx * dx + dy * dy)) / (float)r;
                if (d > 1.0f) continue;
                if (rnd01(seed) < 0.9f - 0.5f * d) {
                    Color cc = (dy < -r / 2 && rnd01(seed) < 0.5f) ? hi : col;
                    cv_blend(c, cx + dx, cy + dy, cc, 0.85f);
                }
            }
        }
        int drips = rnd_range(seed, 0, 2);
        for (int d = 0; d < drips; d++) {
            int x = cx + rnd_range(seed, -r, r);
            int len = rnd_range(seed, 2, 2 + r);
            for (int i = 0; i < len; i++) cv_blend(c, x, cy + r / 2 + i, col, 0.8f);
        }
    }
}

/* horizontal stone flagstones starting at y0 */
static void paint_flags(Canvas *c, int y0, unsigned *seed, Color base, Color mortar, Color hi, int jitter)
{
    for (int row = 0, y = y0; y < c->h; row++, y += 8) {
        int off = (row & 1) ? 12 : 0;
        for (int x = -off; x < c->w; x += 24) {
            Color b = shift(base, rnd_range(seed, -jitter, jitter));
            for (int dy = 0; dy < 8; dy++) {
                for (int dx = 0; dx < 24; dx++) {
                    Color col;
                    if (dx == 23 || dy == 7) col = mortar;
                    else if (dy == 0) col = color_lerp(b, hi, 0.5f);
                    else col = b;
                    cv_put(c, x + dx, y + dy, col);
                }
            }
        }
    }
}

/* a stone ledge: light top edge, body, dark underside, block joints */
static void paint_ledge(Canvas *c, int y, Color hi, Color mid, Color dk)
{
    cv_rect(c, 0, y, c->w, 1, hi);
    cv_rect(c, 0, y + 1, c->w, 2, mid);
    cv_rect(c, 0, y + 3, c->w, 1, dk);
    for (int x = 0; x < c->w; x += 40) cv_rect(c, x, y + 1, 1, 2, dk);
}

/* a small arched door (10x14) standing on a ledge at its bottom edge.
 * Fire: red door with flame glyph. Water: blue door with droplet glyph. */
static void paint_door(Canvas *c, int x, int y, bool fire)
{
    static const char *flame[6] = { "..a..", ".aba.", "aabba", "abbba", "abbba", ".aaa." };
    static const char *drop[6]  = { "..b..", "..a..", ".aba.", "abbba", "abbba", ".aaa." };
    Color stone_hi = hex(0x828A60), stone_dk = hex(0x181B11);
    Color top = fire ? hex(0x4A1209) : hex(0x0C2A4A);
    Color bot = fire ? hex(0x240806) : hex(0x061226);
    Color rim = fire ? hex(0xC8461E) : hex(0x3CA0E6);
    Color ga = fire ? COL_FIRE : COL_WATER;
    Color gb = fire ? hex(0xFFE066) : COL_WATER_LIGHT;

    for (int dy = 0; dy < 14; dy++) {
        for (int dx = 0; dx < 10; dx++) {
            Color col;
            if (dy == 0 && (dx == 0 || dx == 9)) continue;     /* rounded top corners */
            if (dx == 0 || dy == 0) col = stone_hi;
            else if (dx == 9 || dy == 13) col = stone_dk;
            else {
                float f = (float)(dy - 1) / 11.0f;
                col = color_lerp(top, bot, f);
                if (dy == 1) col = color_lerp(col, rim, 0.6f);
            }
            cv_put(c, x + dx, y + dy, col);
        }
    }
    const char **g = fire ? flame : drop;
    for (int gy = 0; gy < 6; gy++) {
        for (int gx = 0; gx < 5; gx++) {
            char ch = g[gy][gx];
            if (ch == 'a') cv_put(c, x + 2 + gx, y + 4 + gy, ga);
            else if (ch == 'b') cv_put(c, x + 2 + gx, y + 4 + gy, gb);
        }
    }
}

/* hanging vine: zig-zag stem with small leaves */
static void paint_vine(Canvas *c, unsigned *seed, int x0, int len, Color stem, Color leaf, Color leaf_hi)
{
    int x = x0;
    int dir = rnd_range(seed, 0, 1) ? 1 : -1;
    int end = len < c->h ? len : c->h;
    for (int y = 0; y < end; y++) {
        cv_put(c, x, y, stem);
        if (y > 0 && y % 4 == 0) {
            cv_put(c, x + dir, y, stem);
            cv_put(c, x + 2 * dir, y, stem);
            x += 2 * dir;
            dir = -dir;
        }
        if (y > 3 && y % 4 == 2 && rnd_range(seed, 0, 2) != 0) {
            int s = rnd_range(seed, 0, 1) ? 1 : -1;
            cv_put(c, x + s, y, leaf);
            cv_put(c, x + 2 * s, y, leaf);
            cv_put(c, x + 2 * s, y + 1, leaf);
            cv_put(c, x + s, y + 1, leaf_hi);
        }
    }
    if (end > 0) cv_put(c, x, end - 1, leaf_hi);
}

/* ---------- backgrounds ---------- */
static void bake_hall(Canvas *c, bool menu)
{
    unsigned seed = menu ? 0x2B1Au : 0x51A7u;
    WallPal p = {
        .base = hex(0x565E3A), .hi = hex(0x8C9466), .lo = hex(0x2E3320),
        .mortar = hex(0x181B11), .crack = hex(0x14170D),
        .moss = hex(0x5C7A2E), .moss_hi = hex(0x8CB048),
        .bw = 20, .bh = 9, .jitter = 9
    };
    paint_wall(c, &p, &seed);
    paint_patches(c, &seed, menu ? 5 : 11, 2, 5, p.moss, p.moss_hi, true);
    paint_flags(c, 162, &seed, hex(0x4A5034), hex(0x181B11), hex(0x6E7650), 6);

    static const int tiers_hall[3] = { 4, 62, 124 };
    static const int tiers_menu[2] = { 40, 118 };
    int ntier = menu ? 2 : 3;
    for (int i = 0; i < ntier; i++) {
        int ty = menu ? tiers_menu[i] : tiers_hall[i];
        paint_ledge(c, ty + 14, hex(0x828A60), hex(0x565E3A), hex(0x181B11));
        int step = menu ? 48 : 24;
        int x0 = (i & 1) ? (menu ? 24 : 12) : 0;
        int k = 0;
        for (int x = x0; x < c->w; x += step, k++) paint_door(c, x, ty, ((k + i) & 1) == 0);
    }

    Color moss = hex(0x5C7A2E), leaf = COL_MOSS_LIGHT, leaf_hi = hex(0xB8E07A);
    static const int vh[][2] = { {6, 70}, {16, 38}, {28, 56}, {300, 64}, {312, 44}, {292, 34}, {108, 20}, {212, 26} };
    static const int vm[][2] = { {8, 46}, {312, 58} };
    if (menu) {
        for (unsigned i = 0; i < sizeof vm / sizeof vm[0]; i++) paint_vine(c, &seed, vm[i][0], vm[i][1], moss, leaf, leaf_hi);
    } else {
        for (unsigned i = 0; i < sizeof vh / sizeof vh[0]; i++) paint_vine(c, &seed, vh[i][0], vh[i][1], moss, leaf, leaf_hi);
    }

    if (menu) {
        cv_scale_all(c, 0.8f);
        cv_center_veil(c, 160.0f, 130.0f, 0.55f);
        cv_vignette(c, 0.6f);
    } else {
        cv_center_veil(c, 160.0f, 100.0f, 0.5f);
        cv_vignette(c, 0.45f);
    }
}

static void bake_fire(Canvas *c)
{
    unsigned seed = 0xF1DEu;
    WallPal p = {
        .base = hex(0x7E3624), .hi = hex(0xA85A38), .lo = hex(0x4A1E14),
        .mortar = hex(0x25100B), .crack = hex(0x1A0A06),
        .moss = hex(0x2C140C), .moss_hi = hex(0x4A2216),
        .bw = 16, .bh = 8, .jitter = 10
    };
    paint_wall(c, &p, &seed);
    paint_patches(c, &seed, 10, 2, 5, hex(0x2C140C), hex(0x4A2216), true);

    /* warm glow rising from the lava */
    for (int y = 96; y < AH; y++) {
        for (int x = 0; x < AW; x++) {
            float f = 0.42f * smoothf(96.0f, 172.0f, (float)y) * (1.0f - 0.35f * fabsf((float)x - 160.0f) / 160.0f);
            cv_blend(c, x, y, hex(0xE0501E), f);
        }
    }

    /* torch brackets (flames are animated) */
    static const int torch_x[2] = { 12, 308 };
    for (int i = 0; i < 2; i++) {
        int tx = torch_x[i];
        cv_rect(c, tx - 1, 11, 3, 1, hex(0x2A2624));
        cv_rect(c, tx, 12, 1, 7, hex(0x6E4220));
        cv_put(c, tx - 1, 12, hex(0x2A2624));
        cv_put(c, tx - 1, 13, hex(0x2A2624));
    }

    cv_center_veil(c, 160.0f, 90.0f, 0.3f);

    /* lava band */
    cv_rect(c, 0, 161, AW, 1, hex(0x2A0E08));
    for (int y = 162; y < AH; y++) {
        for (int x = 0; x < AW; x++) {
            Color col = (y == 162) ? hex(0xFFB14A)
                                   : color_lerp(hex(0xE8561A), hex(0x7A1A0A), (float)(y - 163) / 16.0f);
            unsigned r = xs(&seed);
            if (r % 5 == 0) col = shift(col, 18);
            else if (r % 7 == 0) col = shift(col, -14);
            cv_put(c, x, y, col);
        }
    }
    for (int k = 0; k < 40; k++) {
        cv_put(c, rnd_range(&seed, 0, AW - 1), rnd_range(&seed, 163, 170), hex(0xFFD36A));
    }
    cv_vignette(c, 0.35f);
}

static void bake_forest(Canvas *c)
{
    unsigned seed = 0xF0E5u;
    WallPal p = {
        .base = hex(0x46602F), .hi = hex(0x6C8C4A), .lo = hex(0x2A3C1E),
        .mortar = hex(0x142210), .crack = hex(0x0E1A0B),
        .moss = hex(0x3C8232), .moss_hi = hex(0x78C850),
        .bw = 24, .bh = 10, .jitter = 9
    };
    paint_wall(c, &p, &seed);
    paint_patches(c, &seed, 26, 3, 7, p.moss, p.moss_hi, true);

    /* mossy floor with grass tufts */
    cv_rect(c, 0, 164, AW, AH - 164, hex(0x26401E));
    for (int x = 0; x < AW; x++) {
        int h = 2 + rnd_range(&seed, 0, 4);
        for (int k = 0; k < h; k++) cv_put(c, x, 164 + k, (k == h - 1) ? hex(0x7CC04E) : hex(0x3C6A2C));
        x += rnd_range(&seed, 0, 2);
    }

    /* many vines */
    Color stem = hex(0x3E6E2C), leaf = hex(0x60A448), leaf_hi = hex(0x9ED468);
    for (int k = 0; k < 16; k++) {
        int x = (k * 23 + 7) % AW;
        int len = 24 + rnd_range(&seed, 0, 60);
        paint_vine(c, &seed, x, len, stem, leaf, leaf_hi);
    }

    cv_center_veil(c, 160.0f, 110.0f, 0.22f);
    cv_vignette(c, 0.4f);
}

static void bake_water(Canvas *c)
{
    unsigned seed = 0x3A7E5u;
    WallPal p = {
        .base = hex(0x485C6C), .hi = hex(0x7490A0), .lo = hex(0x2A3844),
        .mortar = hex(0x141C24), .crack = hex(0x0E1218),
        .moss = hex(0x346E6A), .moss_hi = hex(0x5AA49C),
        .bw = 32, .bh = 12, .jitter = 7
    };
    paint_wall(c, &p, &seed);
    paint_patches(c, &seed, 10, 2, 5, p.moss, p.moss_hi, false);

    /* waterfall channels: recess, carved spout, falling stream (animated stripes are drawn on top) */
    static const int falls[2] = { 14, 288 };
    for (int i = 0; i < 2; i++) {
        int x0 = falls[i];
        cv_rect(c, x0 - 2, 0, 16, 152, hex(0x1E2A34));
        cv_rect(c, x0, 6, 12, 146, hex(0x2F6E9C));
        cv_rect(c, x0, 6, 1, 146, hex(0x9CDFF5));
        cv_rect(c, x0 + 11, 6, 1, 146, hex(0x1C4C72));
        cv_rect(c, x0 - 2, 0, 16, 5, hex(0x141C24));
        cv_rect(c, x0 - 2, 5, 16, 1, hex(0xA8CCD8));
    }

    cv_center_veil(c, 160.0f, 100.0f, 0.2f);

    /* pool */
    for (int y = 152; y < AH; y++) {
        float f = (float)(y - 152) / 28.0f;
        Color base = color_lerp(hex(0x2A6E9A), hex(0x123A60), f);
        for (int x = 0; x < AW; x++) {
            unsigned r = xs(&seed);
            Color col = base;
            if (r % 9 == 0) col = shift(base, 10);
            cv_put(c, x, y, col);
        }
    }
    cv_rect(c, 0, 152, AW, 1, hex(0x8FD4F4));
    cv_rect(c, 0, 153, AW, 1, hex(0x4CA6D8));
    for (int k = 0; k < 14; k++) {
        int x = rnd_range(&seed, 0, AW - 6), y = rnd_range(&seed, 158, 176);
        cv_rect(c, x, y, rnd_range(&seed, 3, 6), 1, hex(0x7CC8EC));
    }
    cv_vignette(c, 0.4f);
}

/* light shaft: a soft diagonal band from the top-left, fading downward (tinted per frame) */
static void bake_shaft(Canvas *c)
{
    for (int y = 0; y < c->h; y++) {
        float cx = 56.0f + (float)y * 0.62f;
        float hw = 9.0f + (float)y * 0.11f;
        float fade = 1.0f - (float)y / 190.0f;
        for (int x = 0; x < c->w; x++) {
            float u = fabsf((float)x - cx) / hw;
            Color col = { 0, 0, 0, 0 };
            if (u < 1.0f) {
                float a = (1.0f - u) * (1.0f - u) * fade * 74.0f;
                col = (Color){ 255, 238, 190, (unsigned char)a };
            }
            c->px[y * c->w + x] = col;
        }
    }
}

static Texture2D tex_from(Color *px, int w, int h)
{
    Image img = { .data = px, .width = w, .height = h, .mipmaps = 1,
                  .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    Texture2D t = LoadTextureFromImage(img);
    SetTextureFilter(t, TEXTURE_FILTER_POINT);
    return t;
}

/* ---------- mascots ---------- */
typedef struct {
    const char *head[6];
    unsigned pal[7];   /* outline, body, shade, highlight, eye, accent, accent-light */
} MascotDef;

static const MascotDef s_mascots[MASCOT_COUNT] = {
    { { ".......Y........", "......YF...Y....", ".....YFF..YF....", "....YFFLFYFF....", "....FFLLLLFF....", "...FFLLLLLLFF..." },
      { 0x4A1408, 0xE8562A, 0xB8361A, 0xFFB13B, 0x2A0E06, 0xFF7A1F, 0xFFE066 } },
    { { "..........L.....", ".........LL.....", "........LAL.....", ".......LAAL.....", "....LLLAAL......", "...LAAAAALL....." },
      { 0x0B2747, 0x2FA8FF, 0x1A72C4, 0xC8F2FF, 0x0B1F33, 0x2FA8FF, 0x9BE7FF } },
    { { "................", ".........AA.....", "....AA..AAA.....", "....AAA.A.......", "......AAA.......", ".......A........" },
      { 0x1E2A0E, 0x7FA34A, 0x5C7A2E, 0xC6E08A, 0x16200A, 0x4FBF3A, 0xB8F07A } },
    { { "................", "....A..A..A.....", "....AA.A.AA.....", "....AAAAAAA.....", "....AHAHAHA.....", "....AAAAAAA....." },
      { 0x5A2A18, 0xD97757, 0xB85C3D, 0xF4B89C, 0x1F1E1D, 0xF2C14E, 0xFFE9A8 } },
    { { "................", ".......A........", ".....AAAAA......", "....AAHAAAA.....", "...AAAAAAAAA....", "...A.A.A.A.A...." },
      { 0x1A1C12, 0x8A8F70, 0x656A50, 0xC8CCAE, 0x1A1C12, 0xB9B79C, 0xE8E6CF } },
    { { ".......L........", "......LLL.......", ".......A........", ".......A........", ".....AAAAA......", "................" },
      { 0x121110, 0x3D3A36, 0x2A2825, 0x6B6660, 0xF0A27E, 0xC9A227, 0xE8D27A } },
};

static const char *s_body[10] = {
    "...HBBBBBBBBB...",
    "...BBBBBBBBB....",
    "..BBBEBBBEBBB...",
    "...BBEBBBEBB....",
    "...SSSSSSSSS....",
    "...S.S...S.S....",
    "...S.S...S.S....",
    "...S.S...S.S....",
    "................",
    "................",
};

/* Compose one 16x16 mascot. variant 0 = normal, 1 = blink (eyes closed), 2 = flicker frame. */
static void compose_mascot(MascotId m, int variant, Color out[256])
{
    const MascotDef *d = &s_mascots[m];
    Color pal[7];
    for (int i = 0; i < 7; i++) pal[i] = hex(d->pal[i]);

    unsigned char filled[256];
    memset(filled, 0, sizeof filled);
    for (int i = 0; i < 256; i++) out[i] = (Color){ 0, 0, 0, 0 };

    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            char ch = '.';
            Color col = { 0, 0, 0, 0 };
            if (y < 6) {
                ch = d->head[y][x];
                if (ch == '.') continue;
                switch (ch) {
                case 'A': case 'F': col = pal[5]; break;
                case 'L': case 'Y': col = pal[6]; break;
                case 'H': col = pal[3]; break;
                default: continue;
                }
                if (variant == 2) {
                    if (m == MASCOT_FIRE && ch == 'Y') col = pal[5];
                    if (m == MASCOT_WATER && ch == 'L') col = pal[3];
                }
            } else {
                ch = s_body[y - 6][x];
                if (ch == '.') continue;
                switch (ch) {
                case 'B': col = pal[1]; break;
                case 'S': col = pal[2]; break;
                case 'H': col = pal[3]; break;
                case 'E':
                    col = pal[4];
                    if (variant == 1) col = (y == 8) ? pal[1] : pal[2];   /* top eye row -> body, bottom -> shade */
                    break;
                default: continue;
                }
            }
            filled[y * 16 + x] = 1;
            out[y * 16 + x] = col;
        }
    }

    /* 1 px outline on the 4-neighbours of every filled pixel */
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            if (filled[y * 16 + x]) continue;
            static const int dx4[4] = { 1, -1, 0, 0 }, dy4[4] = { 0, 0, 1, -1 };
            for (int k = 0; k < 4; k++) {
                int qx = x + dx4[k], qy = y + dy4[k];
                if (qx < 0 || qy < 0 || qx > 15 || qy > 15) continue;
                if (filled[qy * 16 + qx]) {
                    out[y * 16 + x] = pal[0];
                    break;
                }
            }
        }
    }
}

/* Visible-content box (art px, inclusive) of each mascot, union over all its frames. */
static int s_bx0[MASCOT_COUNT], s_by0[MASCOT_COUNT], s_bx1[MASCOT_COUNT], s_by1[MASCOT_COUNT];

static void bake_mascots(void)
{
    Color buf[256];
    for (int m = 0; m < MASCOT_COUNT; m++) {
        s_bx0[m] = 16; s_by0[m] = 16; s_bx1[m] = -1; s_by1[m] = -1;
        for (int v = 0; v < 3; v++) {
            /* only fire and water have a flicker/shimmer frame; others reuse the normal texture */
            if (v == 2 && m != MASCOT_FIRE && m != MASCOT_WATER) {
                s_mascot[m][2] = s_mascot[m][0];
                continue;
            }
            compose_mascot((MascotId)m, v, buf);
            s_mascot[m][v] = tex_from(buf, 16, 16);
            for (int y = 0; y < 16; y++)
                for (int x = 0; x < 16; x++)
                    if (buf[y * 16 + x].a > 0) {
                        if (x < s_bx0[m]) s_bx0[m] = x;
                        if (x > s_bx1[m]) s_bx1[m] = x;
                        if (y < s_by0[m]) s_by0[m] = y;
                        if (y > s_by1[m]) s_by1[m] = y;
                    }
        }
    }
}

/* ---------- title ---------- */
static void build_title(int size, const char *txt)
{
    if (s_title_ok) UnloadTexture(s_title);
    s_title_ok = false;
    s_title_str = txt;
    s_title_size = size;

    Image gi = ImageTextEx(g_font, txt, (float)size, 0, WHITE);
    if (gi.data == NULL || gi.width <= 0 || gi.height <= 0) return;
    int gw = gi.width, gh = gi.height;
    Color *g = LoadImageColors(gi);
    UnloadImage(gi);
    if (g == NULL) return;

    const int pad = 4;
    int W = gw + 2 * pad, H = gh + 2 * pad;
    unsigned char *m = calloc((size_t)W * (size_t)H, 1);
    unsigned char *ol = calloc((size_t)W * (size_t)H, 1);
    Color *out = malloc(sizeof(Color) * (size_t)W * (size_t)H * TITLE_FRAMES);
    if (!m || !ol || !out) {
        free(m); free(ol); free(out); UnloadImageColors(g);
        return;
    }
    for (int y = 0; y < gh; y++)
        for (int x = 0; x < gw; x++)
            if (g[y * gw + x].a >= 128) m[(y + pad) * W + (x + pad)] = 1;
    UnloadImageColors(g);

    /* outline: non-glyph pixels touching the glyph (8-neighbourhood) */
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (m[y * W + x]) continue;
            for (int dy = -1; dy <= 1 && !ol[y * W + x]; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int qx = x + dx, qy = y + dy;
                    if (qx < 0 || qy < 0 || qx >= W || qy >= H) continue;
                    if (m[qy * W + qx]) { ol[y * W + x] = 1; break; }
                }
        }
    }

    Color outline = hex(0x3A1C0A);
    for (int f = 0; f < TITLE_FRAMES; f++) {
        float band = -1000.0f;   /* frame 0 has no shine */
        if (f > 0) band = -0.25f * (float)W + (float)(f - 1) / (float)(TITLE_FRAMES - 2) * 1.5f * (float)W;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                int i = y * W + x;
                Color col = { 0, 0, 0, 0 };
                if (m[i]) {
                    float v = (float)(y - pad) / (float)gh;
                    if (v < 0.5f) col = color_lerp(hex(0xFFEBA6), hex(0xE8C25A), v * 2.0f);
                    else col = color_lerp(hex(0xD29E46), hex(0x8A5C22), (v - 0.5f) * 2.0f);
                    if (y > 0 && !m[i - W]) col = color_lerp(col, hex(0xFFF6D0), 0.6f);   /* bevel highlight */
                    if (y + 1 < H && !m[i + W]) col = color_lerp(col, hex(0x3A2008), 0.45f); /* bevel shade */
                    if (f > 0) {
                        float d = (float)x - band - 0.4f * (float)(y - H / 2);
                        float s = 1.0f - fabsf(d) / 6.0f;
                        if (s > 0.0f) col = color_lerp(col, hex(0xFFFBEA), s * 0.85f);
                    }
                    col.a = 255;
                } else if (ol[i]) {
                    col = ca(outline, 255);
                }
                out[((size_t)f * H + y) * W + x] = col;
            }
        }
    }
    free(m);
    free(ol);

    Image img = { .data = out, .width = W, .height = H * TITLE_FRAMES, .mipmaps = 1,
                  .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    s_title = LoadTextureFromImage(img);
    SetTextureFilter(s_title, TEXTURE_FILTER_POINT);
    s_title_w = W;
    s_title_h = H;
    s_title_ok = true;
    free(out);
}

/* ---------- public: lifecycle ---------- */
void art_init(void)
{
    if (s_inited) return;
    s_inited = true;
    s_buf = malloc(sizeof(Color) * AW * AH);
    if (!s_buf) return;
    Canvas cv = { s_buf, AW, AH };

    cv_fill(&cv, COL_STONE_800);
    bake_hall(&cv, false);
    s_bg[BG_HOME] = tex_from(s_buf, AW, AH);

    cv_fill(&cv, COL_STONE_800);
    bake_hall(&cv, true);
    s_bg[BG_MENU] = tex_from(s_buf, AW, AH);

    cv_fill(&cv, hex(0x7E3624));
    bake_fire(&cv);
    s_bg[BG_BOARD3] = tex_from(s_buf, AW, AH);

    cv_fill(&cv, hex(0x46602F));
    bake_forest(&cv);
    s_bg[BG_BOARD5] = tex_from(s_buf, AW, AH);

    cv_fill(&cv, hex(0x485C6C));
    bake_water(&cv);
    s_bg[BG_BOARD10] = tex_from(s_buf, AW, AH);

    bake_shaft(&cv);
    s_shaft = tex_from(s_buf, AW, AH);

    /* soft glow: white with quantised radial alpha */
    Color glow[32 * 32];
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            float dx = (x + 0.5f - 16.0f) / 16.0f, dy = (y + 0.5f - 16.0f) / 16.0f;
            float d = sqrtf(dx * dx + dy * dy);
            float a = d >= 1.0f ? 0.0f : (1.0f - d) * (1.0f - d);
            int q = (int)(a * 8.0f) * 32;
            glow[y * 32 + x] = (Color){ 255, 255, 255, (unsigned char)clampi(q, 0, 255) };
        }
    }
    s_glow = tex_from(glow, 32, 32);

    bake_mascots();
}

void art_unload(void)
{
    if (!s_inited) return;
    for (int i = 0; i < BG_COUNT; i++) UnloadTexture(s_bg[i]);
    for (int m = 0; m < MASCOT_COUNT; m++)
        for (int v = 0; v < 3; v++) {
            /* [2] may alias [0] for non-flicker mascots: unload each distinct id once */
            if (v == 2 && m != MASCOT_FIRE && m != MASCOT_WATER) continue;
            UnloadTexture(s_mascot[m][v]);
        }
    UnloadTexture(s_glow);
    UnloadTexture(s_shaft);
    if (s_title_ok) UnloadTexture(s_title);
    s_title_ok = false;
    free(s_buf);
    s_buf = NULL;
    s_inited = false;
}

/* ---------- public: backgrounds ---------- */
ArtBg art_board_bg(int board_n)
{
    return board_n == 3 ? BG_BOARD3 : board_n == 5 ? BG_BOARD5 : BG_BOARD10;
}

static void draw_bg_tex(Texture2D tex, Color tint)
{
    DrawTexturePro(tex, (Rectangle){ 0, 0, AW, AH }, (Rectangle){ 0, 0, 1280, 720 },
                   (Vector2){ 0, 0 }, 0.0f, tint);
}

/* torch flame: six rows of art pixels from base (ax, ay) upward, flickering every ~80 ms */
static void draw_flame(int ax, int ay, float t, unsigned seed)
{
    static const int widths[7] = { 3, 3, 3, 3, 2, 2, 1 };       /* from the base upward */
    static const unsigned cols[7] = { 0xFFF2B0, 0xFFD45A, 0xFFB040, 0xFF8A2A, 0xFF6A24, 0xFF4A1A, 0xD9452B };
    int frame = (int)floorf(t * 12.0f);
    for (int r = 0; r < 7; r++) {
        unsigned h = hash2((int)seed + r * 7, frame);
        int w = widths[r];
        if (r >= 3 && (h >> 9) % 4 == 0 && w > 1) w--;
        int sway = (r >= 3) ? (int)((h >> 3) % 3) - 1 : 0;
        int x = ax - (w - 1) / 2 + sway;
        apx(x, ay - r, w, 1, hex(cols[r]));
    }
}

static void draw_torch(int tx, int ty, float t, unsigned seed)
{
    float flick = 0.5f * sinf(t * 17.0f + (float)seed) + 0.5f * sinf(t * 7.3f + 0.7f * (float)seed);
    float cx = (float)tx * PX + 2.0f, cy = (float)(ty - 2) * PX;
    DrawTexturePro(s_glow, (Rectangle){ 0, 0, 32, 32 },
                   (Rectangle){ cx - 72.0f, cy - 72.0f, 144.0f, 144.0f },
                   (Vector2){ 0, 0 }, 0.0f, ca(hex(0xFF7A2A), (int)(96.0f + 36.0f * flick)));
    draw_flame(tx, ty, t, seed);
}

static void overlay_fire(float t)
{
    draw_torch(12, 10, t, 11u);
    draw_torch(308, 10, t, 29u);

    /* lava surface flicker */
    int step = (int)floorf(t * 4.0f);
    for (int i = 0; i < 10; i++) {
        unsigned h = hash2(i, step);
        apx((int)(h % AW), 163 + (int)((h >> 9) % 3), 2 + (int)((h >> 12) % 3), 1, ca(hex(0xFFE28A), 150));
    }

    /* rising bubbles that pop at the surface */
    for (int i = 0; i < 12; i++) {
        float per = 1.3f + 0.37f * (float)(i % 5);
        float u = fmodf(t / per + 0.173f * (float)i, 1.0f);
        int bx = 8 + (i * 79) % 300;
        if (u < 0.9f) {
            int by = 176 - (int)(u * 14.0f);
            apx(bx, by, 1, 1, hex(0xFFD980));
        } else {
            apx(bx - 1, 161, 3, 1, ca(hex(0xFFB040), 200));
        }
    }
}

static void overlay_forest(float t)
{
    /* fireflies: drift on slow paths and blink on and off */
    for (int i = 0; i < 22; i++) {
        unsigned s = hash2(i, 77);
        float bx = (float)(s % 300u) + 10.0f;
        float by = 30.0f + (float)((s >> 8) % 130u);
        float ph = (float)(s % 628u) / 100.0f;
        float x = bx + sinf(t * 0.5f + ph) * 8.0f + sinf(t * 0.23f + ph * 2.0f) * 4.0f;
        float y = by + cosf(t * 0.37f + ph) * 6.0f;
        float blink = sinf(t * 1.3f + ph * 3.0f);
        if (blink > -0.2f) {
            float a = (blink + 0.2f) / 1.2f;
            int ix = (int)floorf(x), iy = (int)floorf(y);
            apx(ix - 2, iy - 2, 5, 5, ca(hex(0xC8FF70), (int)(40.0f * a)));
            apx(ix - 1, iy - 1, 3, 3, ca(hex(0xC8FF70), (int)(60.0f * a)));
            apx(ix, iy, 1, 1, ca(hex(0xF4FFC0), (int)(255.0f * a)));
        }
    }
}

static void overlay_water(float t)
{
    /* waterfall stripes scrolling down the streams */
    static const int falls[2] = { 14, 288 };
    for (int w = 0; w < 2; w++) {
        int x0 = falls[w];
        for (int k = 0; k < 12; k++) {
            float y = fmodf((float)k * 14.0f + t * 36.0f, 146.0f) + 6.0f;
            int iy = (int)y;
            apx(x0 + 2, iy, 8, 2, ca(hex(0xC8F2FF), 120));
            apx(x0 + 4, iy + 3, 4, 1, ca(hex(0xFFFFFF), 80));
        }
        /* splash at the foot of the fall */
        int sp = (int)(t * 9.0f) % 4;
        apx(x0 + 1 + sp * 2, 150, 2, 1, ca(hex(0xE0F8FF), 200));
    }

    /* ripples on the pool */
    for (int i = 0; i < 4; i++) {
        float x = fmodf(t * 6.0f + (float)i * 80.0f, 320.0f);
        apx((int)x, 156 + (i % 3) * 5, 4, 1, ca(hex(0xA0E4FF), 90));
    }

    /* rising bubbles in the pool */
    for (int i = 0; i < 14; i++) {
        float per = 2.2f + 0.3f * (float)(i % 4);
        float u = fmodf(t / per + 0.31f * (float)i, 1.0f);
        int bx = 40 + (i * 97) % 240;
        int wob = (int)lroundf(sinf(t * 3.0f + (float)i));
        int by = 176 - (int)(u * 24.0f);
        apx(bx + wob, by, 2, 2, ca(hex(0x9AE0FF), 150));
        apx(bx + wob, by, 1, 1, ca(hex(0xFFFFFF), 200));
    }
}

void art_draw_background(ArtBg bg, float t)
{
    if (!s_inited || bg < 0 || bg >= BG_COUNT) {
        ClearBackground(COL_STONE_900);
        return;
    }
    if (g_app.settings.reduced_motion) t = 0.0f;   /* everything animated freezes on its t = 0 frame */

    draw_bg_tex(s_bg[bg], WHITE);

    switch (bg) {
    case BG_HOME: {
        int a = 170 + (int)(60.0f * sinf(t * 0.9f));
        draw_bg_tex(s_shaft, ca(WHITE, a));
    } break;
    case BG_MENU: {
        int a = 90 + (int)(30.0f * sinf(t * 0.9f));
        draw_bg_tex(s_shaft, ca(WHITE, a));
    } break;
    case BG_BOARD3:
        overlay_fire(t);
        break;
    case BG_BOARD5:
        overlay_forest(t);
        break;
    case BG_BOARD10:
        overlay_water(t);
        break;
    default:
        break;
    }
}

/* ---------- public: mascots ---------- */
/* Integer-scales the mascot's visible content box into dst (nearest neighbour, centred).
 * Falls back to a fractional scale when dst is smaller than 1x. Returns the top-left of the
 * 16x16 sprite and the art-px scale; the content box lands inside dst. */
static void mascot_fit(MascotId m, Rectangle dst, float *ox, float *oy, float *sc)
{
    float bw = (float)(s_bx1[m] - s_bx0[m] + 1), bh = (float)(s_by1[m] - s_by0[m] + 1);
    float fit = fminf(dst.width / bw, dst.height / bh);
    float s = floorf(fit);
    if (s < 1.0f) s = fit > 0.0f ? fit : 1.0f;
    float cw = bw * s, ch = bh * s;
    *ox = dst.x + floorf((dst.width - cw) * 0.5f) - (float)s_bx0[m] * s;
    *oy = dst.y + floorf((dst.height - ch) * 0.5f) - (float)s_by0[m] * s;
    *sc = s;
}

void art_draw_mascot(MascotId m, Rectangle dst, float t, bool animate)
{
    if (!s_inited || m < 0 || m >= MASCOT_COUNT) return;
    bool calm = g_app.settings.reduced_motion;

    int var = 0;
    float period = 3.0f + 0.4f * (float)m;             /* 3.0 .. 5.0 s per mascot */
    float ph = fmodf(t + 0.9f * (float)m, period);
    if (!calm && ph < 0.12f) {
        var = 1;                                        /* blink */
    } else if (!calm) {
        if (m == MASCOT_FIRE && fmodf(t, 0.3f) >= 0.15f) var = 2;        /* flame tips flicker */
        if (m == MASCOT_WATER && fmodf(t, 2.0f) < 0.2f) var = 2;         /* droplet shimmer */
    }

    float x, y, sc;
    mascot_fit(m, dst, &x, &y, &sc);
    if (animate && !calm) {
        int up = ((int)floorf(t * 2.0f)) & 1;           /* 1 art px bob, 1 Hz */
        y -= (float)up * sc;
    }
    float w = 16.0f * sc;
    DrawTexturePro(s_mascot[m][var], (Rectangle){ 0, 0, 16, 16 }, (Rectangle){ x, y, w, w },
                   (Vector2){ 0, 0 }, 0.0f, WHITE);
}

void art_draw_avatar(int avatar, Rectangle d, float t)
{
    int m = avatar % MASCOT_COUNT;
    if (m < 0) m += MASCOT_COUNT;

    /* stone frame with bevel and gold corner rivets */
    DrawRectangleRec(d, COL_STONE_900);
    Rectangle body = { d.x + 4, d.y + 4, d.width - 8, d.height - 8 };
    DrawRectangleRec(body, COL_STONE_700);
    DrawRectangle((int)body.x, (int)body.y, (int)body.width, 4, COL_STONE_400);
    DrawRectangle((int)body.x, (int)body.y, 4, (int)body.height, COL_STONE_400);
    DrawRectangle((int)body.x, (int)(body.y + body.height - 4), (int)body.width, 4, COL_STONE_800);
    DrawRectangle((int)(body.x + body.width - 4), (int)body.y, 4, (int)body.height, COL_STONE_800);

    float ix = d.x + 10, iy = d.y + 10, iw = d.width - 20, ih = d.height - 20;
    Rectangle inner = { ix, iy, iw, ih };
    DrawRectangleRec(inner, COL_STONE_900);
    Rectangle back = { ix + 4, iy + 4, iw - 8, ih - 8 };
    Color top = COL_STONE_700, bot = COL_STONE_900;
    if (m == MASCOT_FIRE) { top = hex(0x5C2412); bot = hex(0x240C08); }
    else if (m == MASCOT_WATER) { top = hex(0x123456); bot = hex(0x061628); }
    DrawRectangleGradientV((int)back.x, (int)back.y, (int)back.width, (int)back.height, top, bot);

    if (m == MASCOT_FIRE || m == MASCOT_WATER) {
        Color gc = m == MASCOT_FIRE ? hex(0xFF7A2A) : hex(0x3CB4FF);
        float gs = back.width * 0.9f;
        DrawTexturePro(s_glow, (Rectangle){ 0, 0, 32, 32 },
                       (Rectangle){ back.x + back.width * 0.5f - gs * 0.5f, back.y + back.height * 0.5f - gs * 0.5f, gs, gs },
                       (Vector2){ 0, 0 }, 0.0f, ca(gc, 110));
    }

    /* gold rivets in the frame corners */
    float rv[4][2] = { { d.x + 6, d.y + 6 }, { d.x + d.width - 14, d.y + 6 },
                       { d.x + 6, d.y + d.height - 14 }, { d.x + d.width - 14, d.y + d.height - 14 } };
    for (int i = 0; i < 4; i++) {
        DrawRectangle((int)rv[i][0], (int)rv[i][1], 8, 8, COL_GOLD_DARK);
        DrawRectangle((int)rv[i][0] + 2, (int)rv[i][1] + 2, 4, 4, COL_GOLD);
        DrawRectangle((int)rv[i][0] + 2, (int)rv[i][1] + 2, 2, 2, hex(0xFFEBA6));
    }

    if (!s_inited) return;
    /* creature: ~90% of the recess height (integer scale), standing low with feet near the bottom */
    float bh = back.height * 0.9f;
    Rectangle box = { back.x, back.y + back.height - bh - 2.0f, back.width, bh };
    float ox, oy, sc;
    mascot_fit((MascotId)m, box, &ox, &oy, &sc);
    float feet = oy + (float)(s_by1[m] + 1) * sc;          /* bottom edge of the content (static) */
    float cx = ox + ((float)(s_bx0[m] + s_bx1[m] + 1) * 0.5f) * sc;
    float rw = (float)(s_bx1[m] - s_bx0[m] + 1) * sc * 0.42f;
    DrawEllipse((int)roundf(cx), (int)roundf(feet - sc * 0.5f), rw, sc * 0.9f, ca(COL_STONE_900, 150));

    art_draw_mascot((MascotId)m, box, t, true);
}

/* ---------- public: title ---------- */
void art_draw_title(float cx, float y, float scale, float t)
{
    if (!s_inited) return;
    const char *txt = tr(S_GAME_TITLE);
    /* VT323 has a small x-height, so the art-pixel font is 1.5x the nominal FS_HUGE size */
    int size = (int)lroundf((float)FS_HUGE * scale * 1.5f / (float)PX);
    if (size < 6) size = 6;
    if (!s_title_ok || size != s_title_size || txt != s_title_str) build_title(size, txt);
    if (!s_title_ok) {
        text_draw_aligned(txt, cx, y, (float)FS_HUGE * scale, COL_GOLD, 1);
        return;
    }

    int frame = 0;
    if (!g_app.settings.reduced_motion) {
        float ph = fmodf(t, 4.5f);
        if (ph < 1.2f) frame = 1 + (int)(ph / 1.2f * (float)(TITLE_FRAMES - 2));
        frame = clampi(frame, 0, TITLE_FRAMES - 1);
    }
    Rectangle src = { 0, (float)(frame * s_title_h), (float)s_title_w, (float)s_title_h };
    Rectangle dst = { floorf(cx - s_title_w * PX * 0.5f), floorf(y),
                      (float)(s_title_w * PX), (float)(s_title_h * PX) };
    DrawTexturePro(s_title, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
}

/* ---------- public: board slab ---------- */
typedef struct {
    Color fl, fm, fd;     /* frame light / mid / dark */
    Color ta, tb;         /* cell tones (checker) */
    Color lud, lul;       /* engraved line dark / light */
    Color orn;            /* corner ornament */
} SlabPal;

static SlabPal slab_pal(ArtBg theme)
{
    SlabPal p;
    if (theme == BG_BOARD3) {
        p.fl = hex(0x96503A); p.fm = hex(0x68321F); p.fd = hex(0x30140C);
        p.ta = hex(0x54281B); p.tb = hex(0x4A2118);
        p.lud = hex(0x1A0A07); p.lul = hex(0x884A30);
        p.orn = COL_FIRE_LIGHT;
    } else if (theme == BG_BOARD5) {
        p.fl = hex(0x708C48); p.fm = hex(0x465E30); p.fd = hex(0x1C2814);
        p.ta = hex(0x3C522C); p.tb = hex(0x334626);
        p.lud = hex(0x14200E); p.lul = hex(0x6C8448);
        p.orn = hex(0x8CB048);
    } else {
        p.fl = hex(0x78969F); p.fm = hex(0x465C70); p.fd = hex(0x1E2A36);
        p.ta = hex(0x425668); p.tb = hex(0x3A4C5E);
        p.lud = hex(0x141C26); p.lul = hex(0x708CA0);
        p.orn = hex(0x7CD8FF);
    }
    return p;
}

void art_draw_board_slab(Rectangle r, int n, ArtBg theme)
{
    if (n < 1) n = 1;
    SlabPal p = slab_pal(theme);
    const float fw = 16.0f;   /* frame thickness, drawn outside r */
    Rectangle o = { r.x - fw, r.y - fw, r.width + 2 * fw, r.height + 2 * fw };

    /* bevelled frame */
    DrawRectangleRec(o, p.fm);
    DrawRectangle((int)o.x, (int)o.y, (int)o.width, 4, p.fl);
    DrawRectangle((int)o.x, (int)o.y, 4, (int)o.height, p.fl);
    DrawRectangle((int)o.x, (int)(o.y + o.height - 4), (int)o.width, 4, p.fd);
    DrawRectangle((int)(o.x + o.width - 4), (int)o.y, 4, (int)o.height, p.fd);
    /* carved block joints in the frame */
    for (int x = 96; x < (int)o.width - 8; x += 96) {
        DrawRectangle((int)o.x + x, (int)o.y + 4, 2, (int)fw - 4, p.fd);
        DrawRectangle((int)o.x + x, (int)(o.y + o.height - fw), 2, (int)fw - 4, p.fd);
    }
    for (int y = 96; y < (int)o.height - 8; y += 96) {
        DrawRectangle((int)o.x + 4, (int)o.y + y, (int)fw - 4, 2, p.fd);
        DrawRectangle((int)(o.x + o.width - fw), (int)o.y + y, (int)fw - 4, 2, p.fd);
    }
    DrawRectangleLinesEx(o, 2, p.fd);
    /* dark recess line just outside the board */
    DrawRectangleLinesEx((Rectangle){ r.x - 4, r.y - 4, r.width + 8, r.height + 8 }, 4, p.lud);

    /* corner ornaments */
    float cc[4][2] = { { o.x + 8, o.y + 8 }, { o.x + o.width - 8, o.y + 8 },
                       { o.x + 8, o.y + o.height - 8 }, { o.x + o.width - 8, o.y + o.height - 8 } };
    for (int i = 0; i < 4; i++) {
        int cx = (int)cc[i][0], cy = (int)cc[i][1];
        DrawRectangle(cx - 4, cy - 4, 8, 8, p.lud);
        DrawRectangle(cx - 2, cy - 2, 4, 4, p.orn);
    }

    /* cells: subtle two-tone checker */
    float cs = r.width / (float)n;
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            int x0 = (int)roundf(r.x + (float)i * cs), x1 = (int)roundf(r.x + (float)(i + 1) * cs);
            int y0 = (int)roundf(r.y + (float)j * cs), y1 = (int)roundf(r.y + (float)(j + 1) * cs);
            DrawRectangle(x0, y0, x1 - x0, y1 - y0, ((i + j) & 1) ? p.tb : p.ta);
        }
    }

    /* engraved grid: dark groove with a light lip */
    for (int k = 1; k < n; k++) {
        int x = (int)roundf(r.x + (float)k * cs);
        int y = (int)roundf(r.y + (float)k * cs);
        DrawRectangle(x - 2, (int)r.y, 2, (int)r.height, p.lud);
        DrawRectangle(x, (int)r.y, 1, (int)r.height, p.lul);
        DrawRectangle((int)r.x, y - 2, (int)r.width, 2, p.lud);
        DrawRectangle((int)r.x, y, (int)r.width, 1, p.lul);
    }
}
