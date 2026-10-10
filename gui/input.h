/* input.h - unified keyboard / mouse / gamepad input in virtual coordinates (gui/input.c).
 * Key map (PRD):
 *   Player 1 (player 0): W A S D move, Space place/confirm
 *   Player 2 (player 1): Arrow keys move, Enter place/confirm
 *   Esc = ACT_BACK, Home key = ACT_HOME (any player)
 *   Gamepad 0 -> player 0, gamepad 1 -> player 1 (if only one pad, it reports as player -1 / any):
 *     D-pad (and left stick) move, A confirm, B back, Start/Menu home.
 * Directions auto-repeat while held (0.35 s delay, then every 0.08 s). */
#ifndef CARO_INPUT_H
#define CARO_INPUT_H

#include <stdbool.h>
#include "raylib.h"
#include "../core/actions.h"

typedef enum { DEVICE_MOUSE = 0, DEVICE_KEYBOARD, DEVICE_GAMEPAD } InputDevice;

/* Letterbox mapping: set by main each frame before input_update(). */
void input_set_viewport(float scale, float offset_x, float offset_y);
void input_update(float dt);                     /* call once per frame */

/* player: 0 or 1 for that player's keys/pad only; -1 = from any source. */
bool input_pressed(ActionType a, int player);    /* edge (with auto-repeat for directions) */
bool input_down(ActionType a, int player);       /* held */

Vector2 input_mouse(void);                       /* mouse position in virtual coords */
bool input_mouse_moved(void);                    /* moved this frame */
bool input_mouse_pressed(void);                  /* left button went down this frame */
bool input_mouse_released(void);
bool input_mouse_down(void);
float input_mouse_wheel(void);

InputDevice input_last_device(void);             /* most recently used device (for focus ring display) */
bool input_any_activity(void);                   /* any key/button/mouse movement this frame (AFK) */
int  input_char(void);                           /* next typed unicode codepoint this frame, 0 if none */
bool input_backspace(void);                      /* backspace pressed (with repeat) */
void input_consume(void);                        /* swallow all remaining input this frame */

#endif
