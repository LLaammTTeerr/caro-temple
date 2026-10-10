/* STUB - replaced by unit "Art". */
#include "art.h"
#include "theme.h"
#include "app.h"
void art_init(void) {}
void art_unload(void) {}
void art_draw_background(ArtBg bg, float t) { (void)t; Color c[] = {COL_STONE_800, COL_STONE_800, {80, 30, 20, 255}, {30, 60, 25, 255}, {20, 40, 70, 255}}; ClearBackground(c[bg]); }
ArtBg art_board_bg(int n) { return n == 3 ? BG_BOARD3 : n == 5 ? BG_BOARD5 : BG_BOARD10; }
void art_draw_mascot(MascotId m, Rectangle d, float t, bool a) { (void)t; (void)a; DrawRectangleRec(d, m == MASCOT_FIRE ? COL_FIRE : m == MASCOT_WATER ? COL_WATER : COL_MOSS); }
void art_draw_avatar(int av, Rectangle d, float t) { DrawRectangleLinesEx(d, 3, COL_STONE_600); art_draw_mascot((MascotId)(av % MASCOT_COUNT), (Rectangle){d.x + 6, d.y + 6, d.width - 12, d.height - 12}, t, true); }
void art_draw_title(float cx, float y, float s, float t) { (void)t; text_draw_aligned("CARO TEMPLE", cx, y, FS_HUGE * s, COL_GOLD, 1); }
void art_draw_board_slab(Rectangle r, int n, ArtBg th) { (void)th; DrawRectangleRec(r, COL_STONE_700); for (int i = 0; i <= n; i++) { DrawLine(r.x + r.width * i / n, r.y, r.x + r.width * i / n, r.y + r.height, COL_STONE_600); DrawLine(r.x, r.y + r.height * i / n, r.x + r.width, r.y + r.height * i / n, COL_STONE_600); } }
