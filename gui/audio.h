/* audio.h - procedurally synthesized chiptune music and sound effects (gui/audio.c).
 * No audio files. Safe to call everything even if the audio device failed (silent). */
#ifndef CARO_AUDIO_H
#define CARO_AUDIO_H

#include "../core/storage.h"

typedef enum {
    SFX_HOVER = 0, SFX_CLICK, SFX_BACK, SFX_PLACE_FIRE, SFX_PLACE_WATER, SFX_UNDO,
    SFX_WIN, SFX_LOSE, SFX_TICK, SFX_POPUP, SFX_ERROR, SFX_TRANSITION, SFX_COUNT
} SfxId;

typedef enum { BGM_NONE = 0, BGM_MENU, BGM_GAME, BGM_COUNT } BgmId;

void audio_init(void);                          /* InitAudioDevice + synthesize everything */
void audio_shutdown(void);
void audio_update(void);                        /* stream music; call every frame */
void audio_apply_settings(const Settings *s);   /* volumes + mutes */
void audio_play(SfxId id);
void audio_music(BgmId id);                     /* crossfade (~0.5 s) to track; same track = no-op */

#endif
