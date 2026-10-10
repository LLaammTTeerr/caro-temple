/* fx.h - piece rendering and particle effects (gui/fx.c). X = fire, O = water (PRD). */
#ifndef CARO_FX_H
#define CARO_FX_H

#include <stdbool.h>
#include "raylib.h"
#include "../core/rules.h"

void fx_init(void);
void fx_shutdown(void);
void fx_clear(void);                       /* remove all particles */
void fx_update(float dt);
void fx_draw(void);                        /* draw particles (call after pieces) */
void fx_set_reduced_motion(bool on);       /* fewer particles, no flicker/shake */

/* Animated piece inside cell rect. appear01: 0..1 spawn animation (scale/pop); 1 = settled.
 * Fire X: two crossed pixel flame strokes, flickering orange/yellow, small ember particles.
 * Water O: pixel ring of water with rippling highlight and a droplet sheen. */
void fx_draw_piece(Cell who, Rectangle cell, float t, float appear01);
/* Preview (ghost) piece for the hovered/cursor cell, alpha 0..1. */
void fx_draw_ghost(Cell who, Rectangle cell, float alpha);

void fx_spawn_place(Cell who, Rectangle cell);           /* burst: embers (fire) / splash droplets (water) */
void fx_draw_last_move(Rectangle cell, Cell who, float t);/* pulsing highlight ring/corners around the opponent's latest move */
void fx_draw_cursor(Rectangle cell, int player, float t);/* player cursor: P1 fire-colored corners, P2 water-colored */
void fx_draw_win_line(Vector2 a, Vector2 b, Cell who, float t); /* glowing beam across winning cells */
void fx_spawn_celebration(Cell who);                      /* screen-wide embers or rain for game over */

#endif
