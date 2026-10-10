/* main.c - raylib frontend entry point: window, letterboxed virtual screen, main loop.
 *
 * Test/e2e flags:
 *   --scene home|setup|profile|settings|saves|game|pause|gameover|afk|undo|surrender
 *   --board 3|5|10   --pvp   --frames N (default 45)   --shot out.png (save virtual screen, exit)
 * Env CARO_TRACE=<file>: append "scene N", "overlay N" and "moves N" lines whenever they
 * change (used by the end-to-end tests to assert on what input did).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "raylib.h"
#include "app.h"
#include "scene.h"
#include "screens.h"
#include "input.h"
#include "theme.h"
#include "ui.h"
#include "art.h"
#include "fx.h"
#include "audio.h"
#include "ai_worker.h"
#include "../core/i18n.h"

static void demo_game(int board_n, bool pvp, bool finished)
{
    Settings *s = &g_app.settings;
    s->last_board = board_n;
    s->last_mode = pvp ? MODE_PVP : MODE_PVE;
    PlayerInfo p[2] = { s->profile[0], s->profile[1] };
    if (!pvp) { snprintf(p[1].name, sizeof p[1].name, "%s", tr(S_COMPUTER)); p[1].avatar = 5; }
    game_new(&g_app.game, s->last_mode, s->last_diff, board_n, false, p);
    g_app.has_game = true;
    int need = board_n == 3 ? 3 : 5;
    int r0 = board_n / 2 - 1 < 0 ? 0 : board_n / 2 - 1;
    int c0 = (board_n - need) / 2;
    for (int i = 0; i < need; i++) {
        if (!finished && i == need - 1) break;
        game_play(&g_app.game, r0, c0 + i);                        /* X */
        if (i < need - 1) game_play(&g_app.game, (r0 + 1) % board_n, c0 + i); /* O */
    }
    g_app.game.clock[0] = 512.0; g_app.game.clock[1] = 87.0;
}

int main(int argc, char **argv)
{
    const char *scene_arg = NULL, *shot = NULL;
    int board_n = 10, frames = 45;
    bool pvp = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--scene") && i + 1 < argc) scene_arg = argv[++i];
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--board") && i + 1 < argc) board_n = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pvp")) pvp = true;
    }
    if (board_n != 3 && board_n != 5) board_n = 10;

    const char *e;
    if ((e = getenv("CARO_AFK_IDLE")) != NULL) game_afk_idle_s = atof(e);
    if ((e = getenv("CARO_AFK_GRACE")) != NULL) game_afk_grace_s = atof(e);

    memset(&g_app, 0, sizeof g_app);
    g_app.rng = (unsigned)time(NULL) | 1u;
    settings_load(&g_app.settings);
    i18n_set_lang(g_app.settings.lang);

    SetTraceLogLevel(LOG_WARNING);
    /* No MSAA: the pixel art is rendered through a point-filtered render texture anyway, and
     * some GL drivers (e.g. virtualised macOS) offer no multisampled pixel format at all. */
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(VIRTUAL_W, VIRTUAL_H, "Caro Temple");
    if (!IsWindowReady()) {
        fprintf(stderr, "caro: could not create a window / OpenGL 3.3 context.\n");
        return 1;
    }
    SetWindowMinSize(640, 360);
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    if (g_app.settings.fullscreen && !shot) ToggleBorderlessWindowed();

    RenderTexture2D target = LoadRenderTexture(VIRTUAL_W, VIRTUAL_H);
    SetTextureFilter(target.texture, TEXTURE_FILTER_POINT);

    theme_load();
    art_init();
    fx_init();
    audio_init();
    app_save_settings(); /* applies language, audio, reduced motion (also writes defaults on first run) */

    scr_home_register();
    scr_setup_register();
    scr_profile_register();
    scr_settings_register();
    scr_saves_register();
    scr_game_register();

    SceneId first = SCENE_HOME;
    OverlayId ovl = OVL_NONE;
    if (scene_arg) {
        if (!strcmp(scene_arg, "setup")) first = SCENE_SETUP;
        else if (!strcmp(scene_arg, "profile")) { first = SCENE_PROFILE; g_app.settings.last_mode = pvp ? MODE_PVP : MODE_PVE; }
        else if (!strcmp(scene_arg, "settings")) first = SCENE_SETTINGS;
        else if (!strcmp(scene_arg, "saves")) first = SCENE_SAVES;
        else if (!strcmp(scene_arg, "game")) { first = SCENE_GAME; demo_game(board_n, pvp, false); }
        else if (!strcmp(scene_arg, "pause")) { first = SCENE_GAME; demo_game(board_n, pvp, false); ovl = OVL_PAUSE; }
        else if (!strcmp(scene_arg, "surrender")) { first = SCENE_GAME; demo_game(board_n, pvp, false); ovl = OVL_SURRENDER_CONFIRM; }
        else if (!strcmp(scene_arg, "gameover")) { first = SCENE_GAME; demo_game(board_n, pvp, true); }
        else if (!strcmp(scene_arg, "afk")) { first = SCENE_GAME; demo_game(board_n, pvp, false); g_app.game.idle = game_afk_idle_s; }
        else if (!strcmp(scene_arg, "undo")) { first = SCENE_GAME; demo_game(board_n, true, false); game_request_undo(&g_app.game, 1 - g_app.game.turn); }
    }
    scene_init(first);
    if (ovl != OVL_NONE) overlay_open(ovl);

    FILE *trace = NULL;
    if ((e = getenv("CARO_TRACE")) != NULL) trace = fopen(e, "a");
    int tr_scene = -1, tr_ovl = -1, tr_moves = -1;

    int frame = 0;
    while (!WindowShouldClose() && !g_app.quit) {
        float dt = GetFrameTime();
        if (dt > 0.1f) dt = 0.1f; /* avoid jumps after stalls */
        g_app.time += dt;
        if (g_app.toast_t > 0) g_app.toast_t -= dt;

        if (IsKeyPressed(KEY_F11) || ((IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) && IsKeyPressed(KEY_ENTER))) {
            ToggleBorderlessWindowed();
            g_app.settings.fullscreen = !g_app.settings.fullscreen;
            settings_save(&g_app.settings);
        }

        float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
        float scale = sw / VIRTUAL_W < sh / VIRTUAL_H ? sw / VIRTUAL_W : sh / VIRTUAL_H;
        float ox = (sw - VIRTUAL_W * scale) * 0.5f, oy = (sh - VIRTUAL_H * scale) * 0.5f;
        input_set_viewport(scale, ox, oy);
        input_update(dt);

        audio_update();
        fx_update(dt);
        scene_update(dt);
        /* AFK keeps running while Settings / Saves is opened over a live match: the game scene
         * is not updated then, so tick it here and return to the match when the prompt fires
         * (the game's overlay then shows the countdown). */
        if (g_app.has_game && g_app.game.status == GS_PLAYING && !scene_transitioning() &&
            (scene_current() == SCENE_SETTINGS || scene_current() == SCENE_SAVES) &&
            scene_previous() == SCENE_GAME) {
            if (input_any_activity()) game_activity(&g_app.game);
            game_tick(&g_app.game, dt, false);
            if (g_app.game.afk != AFK_NONE) scene_pop();
        }

        BeginTextureMode(target);
        ClearBackground(COL_STONE_900);
        scene_draw();
        if (g_app.toast_t > 0 && g_app.toast[0]) {
            float a = g_app.toast_t < 0.3f ? g_app.toast_t / 0.3f : 1.0f;
            Vector2 m = text_measure(g_app.toast, FS_BODY);
            Rectangle r = { (VIRTUAL_W - m.x) / 2 - SP_3, VIRTUAL_H - 120, m.x + SP_6, m.y + SP_2 };
            DrawRectangleRec(r, Fade(COL_STONE_900, 0.92f * a));
            DrawRectangleLinesEx(r, 2, Fade(COL_GOLD, a));
            text_draw(g_app.toast, r.x + SP_3, r.y + SP_1, FS_BODY, Fade(COL_TEXT, a));
        }
        EndTextureMode();

        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexturePro(target.texture, (Rectangle){0, 0, VIRTUAL_W, -VIRTUAL_H},
                       (Rectangle){ox, oy, VIRTUAL_W * scale, VIRTUAL_H * scale}, (Vector2){0, 0}, 0, WHITE);
        EndDrawing();

        if (trace) {
            int sc = scene_transitioning() ? tr_scene : (int)scene_current();
            int ov = (int)overlay_current();
            int mv = g_app.has_game ? g_app.game.move_count : -1;
            if (sc != tr_scene) fprintf(trace, "scene %d\n", sc);
            if (ov != tr_ovl) fprintf(trace, "overlay %d\n", ov);
            if (mv != tr_moves) fprintf(trace, "moves %d\n", mv);
            if (sc != tr_scene || ov != tr_ovl || mv != tr_moves) fflush(trace);
            tr_scene = sc; tr_ovl = ov; tr_moves = mv;
        }

        frame++;
        if (shot && frame >= frames) {
            Image img = LoadImageFromTexture(target.texture);
            ImageFlipVertical(&img);
            ExportImage(img, shot);
            UnloadImage(img);
            break;
        }
    }

    if (trace) fclose(trace);
    ai_worker_shutdown();
    audio_shutdown();
    fx_shutdown();
    art_unload();
    theme_unload();
    UnloadRenderTexture(target);
    CloseWindow();
    return 0;
}
