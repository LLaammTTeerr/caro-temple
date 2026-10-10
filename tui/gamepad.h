/* gamepad.h - Xbox-style gamepad input for the terminal version using only OS APIs
 * (no libraries): Linux joystick device /dev/input/js*, Windows XInput loaded at runtime
 * (LoadLibrary "xinput1_4.dll" / "xinput1_3.dll" / "xinput9_1_0.dll"), macOS: not supported
 * (pad_init returns false). Mapping: D-pad / left stick -> UP/DOWN/LEFT/RIGHT (with
 * auto-repeat 0.35 s then every 0.08 s), A -> CONFIRM, B -> BACK, Start/Menu -> HOME. */
#ifndef CARO_GAMEPAD_H
#define CARO_GAMEPAD_H

#include <stdbool.h>
#include "../core/actions.h"

#define PAD_MAX 2

typedef struct {
    int pad;          /* 0 or 1 (which controller) */
    ActionType act;   /* ACT_UP..ACT_HOME */
} PadEvent;

bool pad_init(void);              /* true if gamepad support is available on this OS */
void pad_shutdown(void);
int  pad_count(void);             /* currently connected controllers (0..PAD_MAX) */
/* Poll all controllers (non-blocking). Writes up to max edge/repeat events; returns count.
 * dt = seconds since the previous call (for auto-repeat). Handles hot-plug. */
int  pad_poll(PadEvent *out, int max, double dt);

#endif
