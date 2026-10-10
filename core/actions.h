/* actions.h - frontend-agnostic input actions.
 * Frontends translate raw input (keyboard/mouse/gamepad/terminal keys) into these. */
#ifndef CARO_ACTIONS_H
#define CARO_ACTIONS_H

typedef enum {
    ACT_NONE = 0,
    ACT_UP,
    ACT_DOWN,
    ACT_LEFT,
    ACT_RIGHT,
    ACT_CONFIRM, /* Space (P1) / Enter (P2) / gamepad A / mouse click on focused */
    ACT_BACK,    /* Esc / gamepad B */
    ACT_HOME,    /* gamepad Start/Menu (keyboard: Home key) */
    ACT_COUNT
} ActionType;

#endif
