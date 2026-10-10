/* i18n.h - UI strings in English (default) and Vietnamese. UTF-8.
 * Strings ending in _FMT are printf formats; their arguments are documented per key. */
#ifndef CARO_I18N_H
#define CARO_I18N_H

typedef enum { LANG_EN = 0, LANG_VI = 1, LANG_COUNT } Lang;

typedef enum {
    /* titles */
    S_GAME_TITLE,          /* "CARO TEMPLE" */
    S_GAME_SUBTITLE,       /* "Fire vs Water" */
    /* home */
    S_PLAY, S_CONTINUE, S_LOAD_GAME, S_SETTINGS, S_QUIT,
    /* common */
    S_BACK, S_NEXT, S_START, S_CONFIRM, S_CANCEL, S_YES, S_NO, S_ON, S_OFF, S_OK,
    /* new game setup */
    S_NEW_GAME, S_MODE, S_MODE_PVE, S_MODE_PVP, S_MODE_PVE_DESC, S_MODE_PVP_DESC,
    S_BOARD_SIZE, S_BOARD_3, S_BOARD_5, S_BOARD_10,   /* "3 x 3" ... */
    S_BOARD_3_DESC, S_BOARD_5_DESC, S_BOARD_10_DESC,  /* "Fire Shrine - 3 in a row wins" etc */
    S_DIFFICULTY, S_DIFF_EASY, S_DIFF_MEDIUM, S_DIFF_HARD,
    S_RULE_BLOCKED, S_RULE_BLOCKED_DESC,               /* blocked-both-ends rule */
    S_COMPUTER,            /* name shown for the AI player */
    /* profile */
    S_PROFILE_TITLE, S_PLAYER_1, S_PLAYER_2, S_NAME, S_AVATAR, S_ENTER_NAME,
    S_NAME_EMPTY,          /* validation error */
    S_PLAYS_FIRE, S_PLAYS_WATER, /* "Plays Fire (X)" / "Plays Water (O)" */
    S_AVATAR_0, S_AVATAR_1, S_AVATAR_2, S_AVATAR_3, S_AVATAR_4, S_AVATAR_5, /* mascot names */
    /* settings */
    S_LANGUAGE, S_LANG_NAME, /* S_LANG_NAME = this language's own name: "English"/"Tiếng Việt" */
    S_MUSIC, S_SOUND_FX, S_VOLUME, S_MUTE, S_REDUCED_MOTION, S_FULLSCREEN,
    S_CONTROLS, S_CTRL_MOUSE, S_CTRL_P1, S_CTRL_P2, S_CTRL_MENU, S_CTRL_PAD_GAME, S_CTRL_PAD_MENU,
    /* saves */
    S_SAVE_GAME, S_LOAD_TITLE, S_SLOT_FMT /* %d slot number 1..10 */, S_EMPTY_SLOT,
    S_OVERWRITE_Q, S_DELETE, S_DELETE_Q, S_SAVED_FMT /* %d slot */, S_LOAD_FAILED,
    S_MOVES_FMT /* %d move count */, S_NO_SAVES,
    /* in-game HUD */
    S_TURN_FMT /* %s player name */, S_STARTED_AT_FMT /* %s "HH:MM" */, S_ELAPSED,
    S_TIME_LEFT, S_THINKING, S_YOUR_TURN, S_PAUSE, S_PAUSED, S_RESUME, S_UNDO, S_SURRENDER,
    S_HOME, S_MENU, S_LAST_MOVE,
    /* popups */
    S_UNDO_REQUEST_FMT /* %s requester name */, S_ACCEPT, S_DECLINE,
    S_UNDO_DECLINED, S_UNDO_DONE, S_UNDO_NOT_ALLOWED,
    S_SURRENDER_Q_FMT /* %s player name */, S_LEAVE_GAME_Q, S_QUIT_Q,
    S_AFK_TITLE, S_AFK_Q, S_AFK_COUNTDOWN_FMT /* %d seconds */, S_AFK_SAVED_FMT /* %d slot */,
    S_AFK_OVERWROTE_FMT /* %d slot: all slots were full, oldest overwritten */,
    /* game over */
    S_GAME_OVER, S_WINS_FMT /* %s winner name */, S_DRAW, S_YOU_WIN, S_YOU_LOSE,
    S_REASON_LINE, S_REASON_TIMEOUT, S_REASON_SURRENDER, S_REASON_DRAW, S_REMATCH,
    /* hints */
    S_HINT_MENU, S_HINT_GAME_PVE, S_HINT_GAME_PVP, S_HINT_TEXT,
    /* terminal-only */
    S_TERM_TOO_SMALL_FMT /* %d %d minimum cols rows */,
    S_COUNT
} StrId;

void i18n_set_lang(Lang l);   /* out-of-range -> LANG_EN */
Lang i18n_lang(void);
const char *tr(StrId id);     /* never NULL; unknown id -> "" */

#endif
