/* ui.h - immediate-mode widgets with keyboard/gamepad focus navigation (gui/ui.c).
 *
 * Usage per frame inside a scene's update+draw: widgets are both updated and drawn in the
 * scene's draw() call (immediate mode). Scenes call ui_frame_begin() at the start of draw()
 * and ui_frame_end() at the end.
 *
 * Focus: every interactive widget gets an index in call order. ACT_UP/ACT_DOWN (any player)
 * move focus prev/next (wrapping); ACT_LEFT/ACT_RIGHT also move focus, EXCEPT on sliders and
 * choices where they change the value. ACT_CONFIRM activates the focused widget. Mouse hover
 * moves focus to the hovered widget; click activates. Hover/focus visual (PRD): label turns
 * faux-bold, a 2 px WHITE outline appears, and the button lifts 2 px with ~150 ms easing.
 * Plays SFX_HOVER when focus changes and SFX_CLICK on activation.
 *
 * Modal layers: ui_set_interactive(false) makes subsequent widgets draw-only (used for the
 * game HUD when an overlay is open). Focus index is per "focus scope" (ui_focus_scope). */
#ifndef CARO_UI_H
#define CARO_UI_H

#include <stdbool.h>
#include "raylib.h"

typedef enum { BTN_NORMAL = 0, BTN_PRIMARY, BTN_DANGER, BTN_GHOST } ButtonStyle;

void ui_frame_begin(void);
void ui_frame_end(void);                  /* applies navigation for next frame, clamps focus */
void ui_focus_scope(int scope_id);        /* switch focus scope (e.g. 0 = screen, 1 = popup); each remembers its focus */
void ui_focus_reset(void);                /* focus first widget of the current scope */
void ui_focus_set(int index);
int  ui_focus_get(void);
void ui_set_interactive(bool on);         /* default true each frame */
/* Default true each frame. false: widgets drawn after this call (until frame end) are
 * mouse-only - no keyboard/gamepad focus ring, navigation or confirm (game HUD, where the
 * keys drive the board cursors). Widgets are also inert while a scene transition runs. */
void ui_set_keyboard_nav(bool on);

bool ui_button(Rectangle r, const char *label, ButtonStyle style);          /* true when activated */
bool ui_icon_button(Rectangle r, const char *glyph, const char *tooltip);   /* small square button */
bool ui_slider(Rectangle r, const char *label, float *value);               /* 0..1, step 0.05 with keys; true if changed */
bool ui_toggle(Rectangle r, const char *label, bool *value);                /* true if changed */
/* Left/right selector "< option >"; true if changed */
bool ui_choice(Rectangle r, const char *label, const char *const *options, int count, int *selected);
/* Single-line UTF-8 text box. Focus + confirm (or click) enters editing; typing edits;
 * Enter/Esc/click outside leaves editing. Returns true while editing. max_bytes incl NUL. */
bool ui_textbox(Rectangle r, char *buf, int max_bytes, const char *placeholder);
bool ui_textbox_editing(void);            /* any textbox currently editing (scenes ignore ACT_BACK then) */

/* Decorative */
void ui_panel(Rectangle r);               /* carved stone panel: dark fill, brick-ish border, inner bevel */
void ui_panel_titled(Rectangle r, const char *title);
void ui_dim_screen(float alpha01);        /* translucent dark overlay over the whole virtual screen */
/* Modal popup frame centered on screen with title; returns the content rectangle. */
Rectangle ui_popup(float w, float h, const char *title, float appear01);
void ui_hint_bar(const char *text);       /* bottom-of-screen control hints */

#endif
