/* screens.h - scene registration and in-game overlays. */
#ifndef CARO_SCREENS_H
#define CARO_SCREENS_H

#include <stdbool.h>

/* Each registers its Scene with scene_register(). Called once from main. */
void scr_home_register(void);      /* gui/scr_home.c    -> SCENE_HOME */
void scr_setup_register(void);     /* gui/scr_setup.c   -> SCENE_SETUP */
void scr_profile_register(void);   /* gui/scr_profile.c -> SCENE_PROFILE */
void scr_settings_register(void);  /* gui/scr_settings.c-> SCENE_SETTINGS */
void scr_saves_register(void);     /* gui/scr_saves.c   -> SCENE_SAVES */
void scr_game_register(void);      /* gui/scr_game.c    -> SCENE_GAME */

/* ---- In-game overlays (gui/scr_overlays.c), driven by scr_game.c ---- */
typedef enum {
    OVL_NONE = 0,
    OVL_PAUSE,              /* Resume / Save / Undo / Surrender / Settings / Home */
    OVL_UNDO_REQUEST,       /* PvP: "<name> asks to undo. Accept?" answered by the opponent */
    OVL_SURRENDER_CONFIRM,  /* "<current player>, surrender?" */
    OVL_LEAVE_CONFIRM,      /* leave to Home? (match stays in memory, Continue resumes it) */
    OVL_AFK,                /* "Do you want to keep playing?" + countdown; Yes -> game_afk_continue */
    OVL_GAME_OVER           /* winner / reason, Rematch / Home */
} OverlayId;

void overlay_open(OverlayId id);       /* plays SFX_POPUP, resets popup focus + appear animation */
void overlay_close(void);
OverlayId overlay_current(void);
/* Called every frame by scr_game before its own input handling. Opens overlays required by
 * g_app.game state (undo_requester >= 0 -> UNDO_REQUEST, afk == AFK_PROMPT -> AFK,
 * status OVER -> GAME_OVER after ~0.8 s delay so the win line is visible) and handles
 * AFK_TIMEOUT: autosave to storage_autosave_slot(), toast S_AFK_SAVED_FMT, scene_goto(HOME).
 * Returns true if an overlay is open (scr_game must not process board input). */
bool overlay_update(float dt);
void overlay_draw(void);               /* draws dim + popup on top of the game screen */

#endif
