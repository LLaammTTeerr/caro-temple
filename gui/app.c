/* app.c - global app state helpers for the GUI frontend. */
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "scene.h"
#include "audio.h"
#include "fx.h"
#include "../core/i18n.h"

App g_app;

void app_toast(const char *msg)
{
    snprintf(g_app.toast, sizeof g_app.toast, "%s", msg ? msg : "");
    g_app.toast_t = 2.5f;
}

void app_save_settings(void)
{
    settings_save(&g_app.settings);
    i18n_set_lang(g_app.settings.lang);
    audio_apply_settings(&g_app.settings);
    fx_set_reduced_motion(g_app.settings.reduced_motion);
}

void app_start_new_game(void)
{
    Settings *s = &g_app.settings;
    PlayerInfo p[2];
    p[0] = s->profile[0];
    if (s->last_mode == MODE_PVE) {
        memset(&p[1], 0, sizeof p[1]);
        snprintf(p[1].name, sizeof p[1].name, "%s", tr((StrId)(S_CHAMP_EASY + s->last_diff)));
        p[1].avatar = game_champion_avatar(s->last_diff);
    } else {
        p[1] = s->profile[1];
    }
    game_new(&g_app.game, s->last_mode, s->last_diff, s->last_board, s->block_rule, p);
    g_app.has_game = true;
    fx_clear();
    scene_goto(SCENE_GAME);
}

bool app_load_slot(int slot)
{
    Game tmp;
    if (!storage_load_game(slot, &tmp)) {
        audio_play(SFX_ERROR);
        app_toast(tr(S_LOAD_FAILED));
        return false;
    }
    g_app.game = tmp;
    g_app.has_game = true;
    fx_clear();
    scene_goto(SCENE_GAME);
    return true;
}

bool app_save_slot(int slot)
{
    char buf[128];
    if (!g_app.has_game || !storage_save_game(slot, &g_app.game)) {
        audio_play(SFX_ERROR);
        app_toast(tr(S_LOAD_FAILED));
        return false;
    }
    snprintf(buf, sizeof buf, tr(S_SAVED_FMT), slot + 1);
    app_toast(buf);
    return true;
}
