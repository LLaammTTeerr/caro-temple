/* fx.c - pixel-art pieces (X = fire, O = water) and particle effects.
 * Everything is built from hard-edged squares on an integer pixel grid; no anti-aliasing.
 * Particles live in a fixed pool (no per-frame allocation). Reduced motion keeps ~25% of
 * particles, no flicker and a static highlight. */
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "fx.h"
#include "theme.h"
#include "app.h" /* VIRTUAL_W / VIRTUAL_H */

#define FX_PI        3.14159265f
#define FX_TAU       6.28318531f
#define FX_MAX_PARTS 1024
#define CELEB_DUR    2.5f

/* Piece palette (beyond theme.h). */
#define FX_CORE         ((Color){255, 224, 102, 255})  /* #FFE066 white-hot core */
#define FX_FIRE_OUTLINE ((Color){ 74,  20,   8, 255})  /* #4A1408 ember outline */
#define FX_WATER_DEEP   ((Color){ 26, 114, 196, 255})  /* #1A72C4 lower shade */
#define FX_WATER_OUT    ((Color){ 11,  39,  71, 255})  /* #0B2747 outline */
#define FX_WATER_CORE   ((Color){224, 251, 255, 255})  /* #E0FBFF glint */
#define FX_GOLD_LIGHT   ((Color){255, 226, 140, 255})

typedef enum { PK_SQ = 0, PK_RING } PKind;

typedef struct {
    bool alive;
    PKind kind;
    float x, y, vx, vy;   /* PK_RING: x,y = centre, r = radius */
    float grav;           /* px/s^2, negative = upward */
    float age, life;
    float size;           /* square size, 2..4 px */
    float r, vr;          /* ring radius and growth speed */
    Color col;
} Particle;

static Particle g_parts[FX_MAX_PARTS];
static bool g_reduced = false;
static unsigned g_rng = 2463534242u;
static float g_clock = 0.0f;       /* animation clock for ghost previews */

static bool g_celeb_on = false;
static Cell g_celeb_who = CELL_EMPTY;
static float g_celeb_t = 0.0f;
static float g_celeb_acc_a = 0.0f; /* main spawn stream */
static float g_celeb_acc_b = 0.0f; /* water bubbles */

/* ---------- helpers ---------- */

static unsigned xs32(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static float rnd01(void) { return (float)(xs32() & 0xFFFFFFu) / 16777216.0f; }
static float rnd_range(float a, float b) { return a + (b - a) * rnd01(); }

static unsigned hash2(unsigned a, unsigned b)
{
    unsigned h = a * 374761393u + b * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static float hash01(unsigned a, unsigned b) { return (float)(hash2(a, b) & 0xFFFFu) / 65536.0f; }

static float clampf(float v, float lo, float hi)
{
    if (!(v >= lo)) return lo; /* also catches NaN */
    if (v > hi) return hi;
    return v;
}

static int round_i(float v) { return (int)floorf(v + 0.5f); }

static int scaled_count(int n) { return g_reduced ? (n + 3) / 4 : n; }

static Color with_alpha(Color c, float a)
{
    a = clampf(a, 0.0f, 1.0f);
    c.a = (unsigned char)((float)c.a * a + 0.5f);
    return c;
}

static void blk(int x, int y, int s, Color c)
{
    if (c.a == 0) return;
    DrawRectangle(x, y, s, s, c);
}

/* Pixel size for a cell: cell.width / 24, rounded, at least 2. */
static int px_of(Rectangle cell)
{
    int p = round_i(cell.width / 24.0f);
    return p < 2 ? 2 : p;
}

static float ease_out3(float x) { x = clampf(x, 0.0f, 1.0f); float u = 1.0f - x; return 1.0f - u * u * u; }
static float smooth01(float x) { x = clampf(x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); }

static float cell_phase(Rectangle cell)
{
    return hash01((unsigned)(int)cell.x, (unsigned)(int)cell.y) * FX_TAU;
}

/* Piece pixel grid: gs x gs units of blk pixels, centred in the cell. */
typedef struct { int ox, oy, blk, gs, px; } PGrid;

static PGrid pgrid_make(Rectangle cell, float scale)
{
    PGrid g;
    g.px = px_of(cell);
    g.gs = round_i(cell.width * 0.72f / (float)g.px);
    if (g.gs < 8) g.gs = 8;
    g.blk = round_i((float)g.px * scale);
    if (g.blk < 1) g.blk = 1;
    int span = g.gs * g.blk;
    g.ox = round_i(cell.x + (cell.width - (float)span) * 0.5f);
    g.oy = round_i(cell.y + (cell.height - (float)span) * 0.5f);
    return g;
}

/* ---------- particle pool ---------- */

static Particle *part_new(void)
{
    for (int i = 0; i < FX_MAX_PARTS; i++) {
        if (!g_parts[i].alive) {
            memset(&g_parts[i], 0, sizeof g_parts[i]);
            g_parts[i].alive = true;
            return &g_parts[i];
        }
    }
    return NULL; /* pool full: drop the effect */
}

static void spawn_sq(float x, float y, float vx, float vy, float grav, float life, float size, Color c)
{
    Particle *p = part_new();
    if (!p) return;
    p->kind = PK_SQ;
    p->x = x; p->y = y; p->vx = vx; p->vy = vy;
    p->grav = grav; p->life = life; p->size = size; p->col = c;
}

static void spawn_ring(float x, float y, float r0, float vr, float life, Color c)
{
    Particle *p = part_new();
    if (!p) return;
    p->kind = PK_RING;
    p->x = x; p->y = y; p->r = r0; p->vr = vr; p->life = life; p->size = 2.0f; p->col = c;
}

/* ---------- lifecycle ---------- */

void fx_init(void) { fx_clear(); }
void fx_shutdown(void) { fx_clear(); }

void fx_clear(void)
{
    memset(g_parts, 0, sizeof g_parts);
    g_celeb_on = false;
    g_celeb_who = CELL_EMPTY;
    g_celeb_t = 0.0f;
    g_celeb_acc_a = 0.0f;
    g_celeb_acc_b = 0.0f;
}

void fx_set_reduced_motion(bool on) { g_reduced = on; }

static void celebrate_spawn_one(void)
{
    if (g_celeb_who == CELL_X) {
        /* fire embers rising from the bottom */
        Color pal[3] = { COL_FIRE, COL_FIRE_LIGHT, FX_CORE };
        Color c = pal[xs32() % 3u];
        float s = (xs32() & 1u) ? 2.0f : 4.0f;
        spawn_sq(rnd_range(0.0f, VIRTUAL_W), VIRTUAL_H + 4.0f,
                 rnd_range(-25.0f, 25.0f), rnd_range(-260.0f, -120.0f),
                 -15.0f, rnd_range(2.5f, 4.0f), s, c);
    } else {
        /* water: rain streaks falling across the screen */
        Color c = (xs32() & 1u) ? COL_WATER : COL_WATER_LIGHT;
        spawn_sq(rnd_range(0.0f, VIRTUAL_W), -8.0f,
                 -60.0f, rnd_range(480.0f, 640.0f), 0.0f, 1.6f, 2.0f, c);
    }
}

void fx_spawn_celebration(Cell who)
{
    if (who != CELL_X && who != CELL_O) return;
    g_celeb_on = true;
    g_celeb_who = who;
    g_celeb_t = 0.0f;
    g_celeb_acc_a = 0.0f;
    g_celeb_acc_b = 0.0f;
    /* gold confetti burst that falls through the whole screen */
    int n = scaled_count(140);
    for (int i = 0; i < n; i++) {
        Color c = (xs32() & 1u) ? COL_GOLD : FX_GOLD_LIGHT;
        float s = (xs32() & 1u) ? 3.0f : 4.0f;
        spawn_sq(rnd_range(0.0f, VIRTUAL_W), rnd_range(-VIRTUAL_H * 0.6f, 0.0f),
                 rnd_range(-40.0f, 40.0f), rnd_range(60.0f, 160.0f),
                 60.0f, rnd_range(3.0f, 4.5f), s, c);
    }
}

static void celebrate_update(float dt)
{
    if (!g_celeb_on) return;
    g_celeb_t += dt;
    if (g_celeb_t >= CELEB_DUR) { g_celeb_on = false; return; }
    float mult = g_reduced ? 0.25f : 1.0f;
    /* stream: fire 45/s embers, water 70/s rain */
    g_celeb_acc_a += dt * mult * (g_celeb_who == CELL_X ? 45.0f : 70.0f);
    while (g_celeb_acc_a >= 1.0f) { g_celeb_acc_a -= 1.0f; celebrate_spawn_one(); }
    if (g_celeb_who == CELL_O) {
        /* water bubbles rising */
        g_celeb_acc_b += dt * mult * 10.0f;
        while (g_celeb_acc_b >= 1.0f) {
            g_celeb_acc_b -= 1.0f;
            spawn_sq(rnd_range(0.0f, VIRTUAL_W), VIRTUAL_H + 4.0f,
                     rnd_range(-15.0f, 15.0f), rnd_range(-120.0f, -70.0f),
                     0.0f, 3.0f, 4.0f, COL_WATER_LIGHT);
        }
    }
}

void fx_update(float dt)
{
    if (!(dt > 0.0f)) dt = 0.0f; /* also NaN */
    if (dt > 0.1f) dt = 0.1f;
    g_clock += dt;
    celebrate_update(dt);
    for (int i = 0; i < FX_MAX_PARTS; i++) {
        Particle *p = &g_parts[i];
        if (!p->alive) continue;
        p->age += dt;
        if (p->age >= p->life) { p->alive = false; continue; }
        if (p->kind == PK_RING) {
            p->r += p->vr * dt;
        } else {
            p->vy += p->grav * dt;
            p->x += p->vx * dt;
            p->y += p->vy * dt;
        }
    }
}

/* ---------- particle drawing ---------- */

void fx_draw(void)
{
    for (int i = 0; i < FX_MAX_PARTS; i++) {
        const Particle *p = &g_parts[i];
        if (!p->alive) continue;
        float a = 1.0f - p->age / p->life;
        if (!(a > 0.0f)) continue;
        Color c = with_alpha(p->col, a);
        if (p->kind == PK_SQ) {
            int s = (int)p->size;
            int x = (int)floorf(p->x / (float)s) * s;
            int y = (int)floorf(p->y / (float)s) * s;
            DrawRectangle(x, y, s, s, c);
        } else {
            /* pixel ring: sample points along the circle, snapped to 2 px */
            int n = (int)(FX_TAU * p->r / 3.0f);
            if (n < 12) n = 12;
            if (n > 180) n = 180;
            for (int k = 0; k < n; k++) {
                float ang = FX_TAU * (float)k / (float)n;
                int x = (int)floorf((p->x + cosf(ang) * p->r) / 2.0f) * 2;
                int y = (int)floorf((p->y + sinf(ang) * p->r) / 2.0f) * 2;
                DrawRectangle(x, y, 2, 2, c);
            }
        }
    }
}

/* ---------- spawn bursts ---------- */

void fx_spawn_place(Cell who, Rectangle cell)
{
    if (who != CELL_X && who != CELL_O) return;
    float cx = cell.x + cell.width * 0.5f;
    float cy = cell.y + cell.height * 0.5f;
    float spread = cell.width * 0.2f;
    if (who == CELL_X) {
        int n = scaled_count(18 + (int)(xs32() % 13u)); /* 18..30 */
        Color pal[3] = { FX_CORE, COL_FIRE_LIGHT, COL_FIRE };
        for (int i = 0; i < n; i++) {
            float ang = -FX_PI * 0.5f + rnd_range(-1.1f, 1.1f);
            float sp = rnd_range(90.0f, 230.0f);
            float s = (float)(2 + (int)(xs32() % 3u)); /* 2..4 */
            spawn_sq(cx + rnd_range(-spread, spread), cy + rnd_range(-spread * 0.5f, spread * 0.5f),
                     cosf(ang) * sp, sinf(ang) * sp, -90.0f, rnd_range(0.6f, 1.0f), s,
                     pal[xs32() % 3u]);
        }
    } else {
        int n = scaled_count(14 + (int)(xs32() % 7u)); /* 14..20 */
        for (int i = 0; i < n; i++) {
            float ang = -FX_PI * 0.5f + rnd_range(-FX_PI * 0.5f, FX_PI * 0.5f);
            float sp = rnd_range(160.0f, 300.0f);
            float s = (xs32() & 1u) ? 2.0f : 3.0f;
            spawn_sq(cx, cy, cosf(ang) * sp, sinf(ang) * sp, 900.0f, rnd_range(0.55f, 0.9f), s,
                     (xs32() & 1u) ? COL_WATER : COL_WATER_LIGHT);
        }
        /* expanding ripple ring */
        spawn_ring(cx, cy, cell.width * 0.25f, (cell.width * 0.7f) / 0.6f, 0.6f, COL_WATER_LIGHT);
    }
}

/* ---------- piece drawing ---------- */

/* Fire: diagonal strokes on the unit grid, layered by distance from the stroke centre line.
 * level 0 = core (#FFE066), 1 = mid (#FFB13B), 2 = edge (#FF5A1F, flickers),
 * 3 = ember outline (#4A1408). Flames are short pixel columns above each stroke. */
static int fire_level(int i, int j, int n)
{
    int d1 = abs(i - j);
    int d2 = abs(i + j - (n - 1));
    return d1 < d2 ? d1 : d2;
}

static void fire_draw(PGrid g, Rectangle cell, float t, float alpha, bool animate)
{
    const int n = g.gs, b = g.blk;
    const float ph = cell_phase(cell);

    /* flame licks above each stroke */
    for (int i = 0; i < n; i++) {
        for (int s = 0; s < 2; s++) {
            int y0 = (s == 0) ? i : (n - 1 - i);
            int hgt = 2;
            if (animate) {
                float w1 = 0.5f + 0.5f * sinf(t * 6.5f + (float)i * 0.9f + ph + (float)s * 2.1f);
                float w2 = 0.5f + 0.5f * sinf(t * 11.0f + (float)i * 2.3f + ph * 1.7f + (float)s);
                float v = 0.65f * w1 + 0.35f * w2;
                hgt = 1 + (int)(v * 4.0f); /* 1..4 pixels tall */
                if (hgt > 4) hgt = 4;
            }
            for (int h = 0; h < hgt; h++) {
                int r = y0 - 4 - h;
                if (r < 0 || r >= n) continue;
                if (fire_level(i, r, n) <= 3) continue; /* would sit on a stroke */
                Color c = (h == hgt - 1) ? FX_CORE : (h == 0 ? COL_FIRE : COL_FIRE_LIGHT);
                blk(g.ox + i * b, g.oy + r * b, b, with_alpha(c, alpha));
            }
        }
    }

    /* strokes */
    unsigned tick = animate ? (unsigned)(int)(t * 10.0f) : 0u;
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            int lvl = fire_level(i, j, n);
            if (lvl > 3) continue;
            Color c;
            if (lvl == 0) c = FX_CORE;
            else if (lvl == 1) c = COL_FIRE_LIGHT;
            else if (lvl == 2) {
                bool flick = animate && hash01((unsigned)(i * 31 + j), tick) < 0.25f;
                c = flick ? COL_FIRE_LIGHT : COL_FIRE;
            } else c = FX_FIRE_OUTLINE;
            blk(g.ox + i * b, g.oy + j * b, b, with_alpha(c, alpha));
        }
    }

    /* occasional rising ember pixels */
    if (animate) {
        int es = g.px >= 4 ? g.px / 2 : 2;
        for (int k = 0; k < 2; k++) {
            float P = 1.6f + 0.5f * (float)k;
            float tt = t + ph * 0.3f + (float)k * 0.77f;
            float cyc = floorf(tt / P);
            if (hash01((unsigned)(int)cyc + (unsigned)k * 97u, (unsigned)(int)cell.x * 3u + (unsigned)(int)cell.y) > 0.45f)
                continue;
            float u = (tt - cyc * P) / P;
            float ex = cell.x + cell.width * (0.5f + 0.25f * sinf(t * 2.1f + (float)k * 2.0f + ph));
            float ey = cell.y + cell.height * (0.85f - 0.7f * u);
            int x = (int)floorf(ex / (float)es) * es;
            int y = (int)floorf(ey / (float)es) * es;
            blk(x, y, es, with_alpha(COL_FIRE_LIGHT, alpha * (1.0f - u)));
        }
    }
}

/* Water: a pixel ring with a rotating glint arc, deep lower shade, a faint ripple inside
 * the hole and a droplet sheen. */
static void water_draw(PGrid g, Rectangle cell, float t, float alpha, bool animate)
{
    (void)cell;
    const int n = g.gs, b = g.blk;
    const float c0 = (float)(n - 1) * 0.5f;
    const float R = (float)n * 0.5f - 1.5f;
    const float rin = R * 0.6f;
    const float rm = (rin + R) * 0.5f;
    const float arc = animate ? t * 0.7f : -2.35f;   /* glint centre angle */
    const float rr = animate ? (rin - 1.5f) * fmodf(t * 0.35f, 1.0f) : rin * 0.5f;

    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) {
            float dx = (float)i - c0, dy = (float)j - c0;
            float d = sqrtf(dx * dx + dy * dy);
            Color c;
            if (d >= rin && d <= R) {
                c = COL_WATER;
                if (dy > 0.25f * R && d > rm) c = FX_WATER_DEEP;
                float diff = fmodf(atan2f(dy, dx) - arc, FX_TAU);
                if (diff > FX_PI) diff -= FX_TAU;
                if (diff < -FX_PI) diff += FX_TAU;
                float ad = fabsf(diff);
                float width = 0.22f + (animate ? 0.05f * sinf(t * 2.3f) : 0.0f);
                if (d > rm && ad < width) c = FX_WATER_CORE;
                else if (d > rm - 0.5f && ad < width + 0.35f) c = COL_WATER_LIGHT;
            } else if ((d > R && d <= R + 1.0f) || (d >= rin - 1.0f && d < rin)) {
                c = FX_WATER_OUT;
            } else if (d < rin - 1.0f) {
                /* hole: dark translucent water with a ripple ring */
                if (animate && fabsf(d - rr) < 0.6f) c = with_alpha(COL_WATER, 0.8f);
                else c = with_alpha(FX_WATER_OUT, 0.45f);
            } else {
                continue;
            }
            blk(g.ox + i * b, g.oy + j * b, b, with_alpha(c, alpha));
        }
    }

    /* droplet sheen at the upper-left of the ring */
    float sx = c0 + cosf(-2.35f) * rm;
    float sy = c0 + sinf(-2.35f) * rm;
    int ix = round_i(sx), iy = round_i(sy);
    blk(g.ox + ix * b, g.oy + iy * b, b, with_alpha(FX_WATER_CORE, alpha));
    blk(g.ox + (ix + 1) * b, g.oy + iy * b, b, with_alpha(COL_WATER_LIGHT, alpha));
}

void fx_draw_piece(Cell who, Rectangle cell, float t, float appear01)
{
    if (who != CELL_X && who != CELL_O) return;
    float a = clampf(appear01, 0.0f, 1.0f);
    float scale = 1.0f, alpha = 1.0f;
    if (a < 1.0f) {
        /* pop-in: 0.4 -> 1.1 (overshoot) -> 1.0, fade in over the first part */
        if (a < 0.6f) scale = 0.4f + 0.7f * ease_out3(a / 0.6f);
        else scale = 1.1f - 0.1f * smooth01((a - 0.6f) / 0.4f);
        alpha = clampf(a * 2.5f, 0.0f, 1.0f);
    }
    bool animate = !g_reduced;
    PGrid g = pgrid_make(cell, scale);

    if (who == CELL_X) {
        if (a < 0.4f && animate) {
            /* ignition flash over the piece area */
            float fa = 0.55f * (1.0f - a / 0.4f);
            DrawRectangle(g.ox, g.oy, g.gs * g.blk, g.gs * g.blk, with_alpha(FX_CORE, fa));
        }
        fire_draw(g, cell, t, alpha, animate);
    } else {
        if (a < 0.5f) {
            /* splash: an expanding pixel ring in the water colour */
            float k = a / 0.5f;
            float rad = (float)g.gs * 0.5f * (0.3f + 0.7f * k);
            float cc = (float)g.gs * 0.5f;
            float ra = 0.9f * (1.0f - k);
            for (int s = 0; s < 32; s++) {
                float ang = FX_TAU * (float)s / 32.0f;
                int ix = round_i(cc + cosf(ang) * rad);
                int iy = round_i(cc + sinf(ang) * rad);
                blk(g.ox + ix * g.blk, g.oy + iy * g.blk, g.blk, with_alpha(COL_WATER_LIGHT, ra));
            }
        }
        water_draw(g, cell, t, alpha, animate);
    }
}

void fx_draw_ghost(Cell who, Rectangle cell, float alpha)
{
    if (who != CELL_X && who != CELL_O) return;
    float a = clampf(alpha, 0.0f, 1.0f) * 0.5f; /* translucent preview */
    PGrid g = pgrid_make(cell, 1.0f);
    if (who == CELL_X) fire_draw(g, cell, g_clock, a, !g_reduced);
    else water_draw(g, cell, g_clock, a, !g_reduced);
}

/* ---------- highlights and cursor ---------- */

/* Corner bracket. (cx,cy) = top-left of the corner square, (dx,dy) = inward direction. */
static void bracket_corner(int cx, int cy, int dx, int dy, int arm, int thick, Color c)
{
    int hx = dx > 0 ? cx : cx + thick - arm;
    int vy = dy > 0 ? cy : cy + thick - arm;
    DrawRectangle(hx, cy, arm, thick, c);
    DrawRectangle(cx, vy, thick, arm, c);
}

static void brackets(Rectangle r, float inset, float arm, float thick, Color c)
{
    int T = round_i(thick);
    if (T < 1) T = 1;
    int A = round_i(arm);
    if (A < T) A = T;
    int x0 = round_i(r.x + inset);
    int y0 = round_i(r.y + inset);
    int x1 = round_i(r.x + r.width - inset - thick);
    int y1 = round_i(r.y + r.height - inset - thick);
    bracket_corner(x0, y0, 1, 1, A, T, c);
    bracket_corner(x1, y0, -1, 1, A, T, c);
    bracket_corner(x0, y1, 1, -1, A, T, c);
    bracket_corner(x1, y1, -1, -1, A, T, c);
}

void fx_draw_last_move(Rectangle cell, Cell who, float t)
{
    if (who != CELL_X && who != CELL_O) return;
    int px = px_of(cell);
    /* 1.2 Hz pulse; reduced motion holds a static mid-level highlight */
    float p = g_reduced ? 0.5f : 0.5f + 0.5f * sinf(FX_TAU * 1.2f * t);
    Color mover = (who == CELL_X) ? COL_FIRE : COL_WATER;

    /* soft glow fill in the mover colour, plus a stepped inner frame */
    DrawRectangleRec(cell, with_alpha(mover, 0.12f + 0.10f * p));
    int x = round_i(cell.x), y = round_i(cell.y);
    int w = round_i(cell.width), h = round_i(cell.height);
    Color frame = with_alpha(mover, 0.35f + 0.35f * p);
    if (w > 2 * px && h > 2 * px) {
        DrawRectangle(x + px, y + px, w - 2 * px, px, frame);
        DrawRectangle(x + px, y + h - 2 * px, w - 2 * px, px, frame);
        DrawRectangle(x + px, y + px, px, h - 2 * px, frame);
        DrawRectangle(x + w - 2 * px, y + px, px, h - 2 * px, frame);
    }

    /* gold corner brackets: thick pixel L-shapes, breathing */
    float arm = cell.width * 0.26f;
    float thick = (float)px;
    brackets(cell, 0.0f, arm, thick, with_alpha(COL_GOLD, 0.75f + 0.25f * p));
}

void fx_draw_cursor(Rectangle cell, int player, float t)
{
    int px = px_of(cell);
    Color col = (player == 0) ? COL_FIRE_LIGHT : COL_WATER_LIGHT;
    Color base = (player == 0) ? COL_FIRE : COL_WATER;
    float thick = (float)px * 0.5f;
    if (thick < 2.0f) thick = 2.0f;
    /* breathing inset and marching dot; reduced motion holds both still */
    float breathe = g_reduced ? 0.5f : 0.5f + 0.5f * sinf(t * 4.0f);
    float inset = (float)px * (0.5f + 0.5f * breathe);
    float arm = cell.width * 0.2f;
    brackets(cell, inset, arm, thick, col);

    /* marching dot along each top arm */
    int T = round_i(thick);
    float run = arm - (float)T;
    if (run < 1.0f) return;
    float s = g_reduced ? 0.5f : fmodf(t * 0.8f, 1.0f);
    int dotx = round_i(cell.x + inset + s * run);
    int doty = round_i(cell.y + inset);
    DrawRectangle(dotx, doty, T, T, with_alpha(base, 1.0f));
    DrawRectangle(dotx, round_i(cell.y + cell.height - inset - thick), T, T, with_alpha(base, 1.0f));
}

void fx_draw_win_line(Vector2 a, Vector2 b, Cell who, float t)
{
    if (who != CELL_X && who != CELL_O) return;
    Color glow = (who == CELL_X) ? COL_FIRE : COL_WATER;
    Color core = (who == CELL_X) ? FX_CORE : FX_WATER_CORE;
    float dx = b.x - a.x, dy = b.y - a.y;
    float len = sqrtf(dx * dx + dy * dy);
    if (!(len > 1.0f)) return;
    const int S = 4; /* beam pixel */
    float ux = dx / len, uy = dy / len;
    int steps = (int)(len / (float)S);
    float pulse = g_reduced ? 0.9f : 0.75f + 0.25f * sinf(t * 5.0f);

    for (int k = 0; k <= steps; k++) {
        float s = (float)(k * S);
        int cx = round_i(a.x + ux * s);
        int cy = round_i(a.y + uy * s);
        DrawRectangle(cx - 8, cy - 8, 16, 16, with_alpha(glow, 0.20f * pulse)); /* outer glow */
        DrawRectangle(cx - 6, cy - 6, 12, 12, with_alpha(glow, 0.45f * pulse)); /* glow */
        DrawRectangle(cx - 4, cy - 4, 8, 8, with_alpha(core, 0.9f));            /* core */
        DrawRectangle(cx - 2, cy - 2, 4, 4, with_alpha(WHITE, 0.9f));           /* hot centre */
    }

    /* travelling sparkle */
    float sp = g_reduced ? 0.5f : fmodf(t * 0.55f, 1.0f);
    for (int k = 0; k < 3; k++) {
        float q = sp - 0.03f * (float)k;
        if (q < 0.0f) q += 1.0f;
        int sx = round_i(a.x + dx * q);
        int sy = round_i(a.y + dy * q);
        int sz = k == 0 ? 8 : 4;
        DrawRectangle(sx - sz / 2, sy - sz / 2, sz, sz, with_alpha(FX_WATER_CORE, k == 0 ? 1.0f : 0.6f));
    }
    int fx = round_i(a.x + dx * sp), fy = round_i(a.y + dy * sp);
    DrawRectangle(fx - 12, fy - 2, 24, 4, with_alpha(WHITE, 0.5f));
    DrawRectangle(fx - 2, fy - 12, 4, 24, with_alpha(WHITE, 0.5f));
}
