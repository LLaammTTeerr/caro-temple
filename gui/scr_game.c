/* STUB - replaced by unit "Game screen". */
#include "screens.h"
#include "scene.h"
#include "ui.h"
#include "theme.h"
#include "art.h"
#include "fx.h"
#include "input.h"
#include "app.h"
static Rectangle board_rect(void) { return (Rectangle){340, 60, 600, 600}; }
static void update(float dt)
{
    Game *g = &g_app.game;
    if (overlay_update(dt)) return;
    game_tick(g, dt, true);
    if (input_pressed(ACT_BACK, -1)) { overlay_open(OVL_PAUSE); return; }
    if (game_is_ai_turn(g)) { Pos p = ai_choose_move(&g->board, game_cell_of(g->turn), g->diff, &g_app.rng); game_play(g, p.r, p.c); return; }
    Rectangle br = board_rect(); float cs = br.width / g->board.n; Vector2 m = input_mouse();
    if (input_mouse_pressed() && CheckCollisionPointRec(m, br)) game_play(g, (int)((m.y - br.y) / cs), (int)((m.x - br.x) / cs));
}
static void draw(void)
{
    Game *g = &g_app.game;
    art_draw_background(art_board_bg(g->board.n), g_app.time);
    ui_frame_begin();
    Rectangle br = board_rect(); float cs = br.width / g->board.n;
    art_draw_board_slab(br, g->board.n, art_board_bg(g->board.n));
    for (int r = 0; r < g->board.n; r++) for (int c = 0; c < g->board.n; c++) fx_draw_piece(g->board.c[r][c], (Rectangle){br.x + c * cs, br.y + r * cs, cs, cs}, g_app.time, 1);
    ui_frame_end();
    overlay_draw();
}
void scr_game_register(void) { scene_register(SCENE_GAME, (Scene){0, 0, update, draw}); }
