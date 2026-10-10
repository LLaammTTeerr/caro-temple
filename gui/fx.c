/* STUB - replaced by unit "Piece FX". */
#include "fx.h"
#include "theme.h"
void fx_init(void) {}
void fx_shutdown(void) {}
void fx_clear(void) {}
void fx_update(float dt) { (void)dt; }
void fx_draw(void) {}
void fx_set_reduced_motion(bool on) { (void)on; }
void fx_draw_piece(Cell w, Rectangle c, float t, float a) { (void)t; (void)a; Vector2 m = {c.x + c.width / 2, c.y + c.height / 2}; if (w == CELL_X) { DrawLineEx((Vector2){c.x + 8, c.y + 8}, (Vector2){c.x + c.width - 8, c.y + c.height - 8}, 6, COL_FIRE); DrawLineEx((Vector2){c.x + c.width - 8, c.y + 8}, (Vector2){c.x + 8, c.y + c.height - 8}, 6, COL_FIRE); } else if (w == CELL_O) DrawRing(m, c.width * 0.25f, c.width * 0.35f, 0, 360, 24, COL_WATER); }
void fx_draw_ghost(Cell w, Rectangle c, float a) { (void)w; DrawRectangleRec(c, Fade(WHITE, 0.1f * a)); }
void fx_spawn_place(Cell w, Rectangle c) { (void)w; (void)c; }
void fx_draw_last_move(Rectangle c, Cell w, float t) { (void)w; (void)t; DrawRectangleLinesEx(c, 3, COL_GOLD); }
void fx_draw_cursor(Rectangle c, int p, float t) { (void)t; DrawRectangleLinesEx(c, 2, p ? COL_WATER_LIGHT : COL_FIRE_LIGHT); }
void fx_draw_win_line(Vector2 a, Vector2 b, Cell w, float t) { (void)t; DrawLineEx(a, b, 8, w == CELL_X ? COL_FIRE_LIGHT : COL_WATER_LIGHT); }
void fx_spawn_celebration(Cell w) { (void)w; }
