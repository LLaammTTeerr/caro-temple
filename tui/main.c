/* main.c - terminal frontend entry point and main loop (no external libraries). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tui.h"
#include "../core/i18n.h"

TuiApp g_tui;

void tui_toast(const char *msg)
{
    snprintf(g_tui.toast, sizeof g_tui.toast, "%s", msg ? msg : "");
    g_tui.toast_until = term_now() + 2.5;
}

void tui_save_settings(void)
{
    settings_save(&g_tui.settings);
    i18n_set_lang(g_tui.settings.lang);
}

void tui_goto(TuiScreen s)
{
    if (s != g_tui.screen) g_tui.prev_screen = g_tui.screen;
    g_tui.screen = s;
    term_invalidate();
    if (s == TS_GAME) tui_game_enter();
    else tui_menu_enter(s);
}

void tui_start_new_game(void)
{
    Settings *s = &g_tui.settings;
    PlayerInfo p[2];
    p[0] = s->profile[0];
    if (s->last_mode == MODE_PVE) {
        memset(&p[1], 0, sizeof p[1]);
        snprintf(p[1].name, sizeof p[1].name, "%s", tr(S_COMPUTER));
        p[1].avatar = 5;
    } else {
        p[1] = s->profile[1];
    }
    game_new(&g_tui.game, s->last_mode, s->last_diff, s->last_board, s->block_rule, p);
    g_tui.has_game = true;
    tui_goto(TS_GAME);
}

ActionType tui_key_action(const TermEvent *ev, int *player)
{
    int p = -1;
    ActionType a = ACT_NONE;
    if (ev->type != TEV_KEY) { if (player) *player = -1; return ACT_NONE; }
    switch (ev->key) {
    case TK_UP:    a = ACT_UP;    p = 1; break;
    case TK_DOWN:  a = ACT_DOWN;  p = 1; break;
    case TK_LEFT:  a = ACT_LEFT;  p = 1; break;
    case TK_RIGHT: a = ACT_RIGHT; p = 1; break;
    case TK_ENTER: a = ACT_CONFIRM; p = 1; break;
    case TK_ESC:   a = ACT_BACK;  p = -1; break;
    case TK_HOME:  a = ACT_HOME;  p = -1; break;
    case TK_CHAR:
        switch (ev->ch) {
        case 'w': case 'W': a = ACT_UP;    p = 0; break;
        case 's': case 'S': a = ACT_DOWN;  p = 0; break;
        case 'a': case 'A': a = ACT_LEFT;  p = 0; break;
        case 'd': case 'D': a = ACT_RIGHT; p = 0; break;
        case ' ':           a = ACT_CONFIRM; p = 0; break;
        default: break;
        }
        break;
    default: break;
    }
    if (player) *player = p;
    return a;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    const char *e;
    if ((e = getenv("CARO_AFK_IDLE")) != NULL) game_afk_idle_s = atof(e);
    if ((e = getenv("CARO_AFK_GRACE")) != NULL) game_afk_grace_s = atof(e);

    memset(&g_tui, 0, sizeof g_tui);
    g_tui.rng = (unsigned)time(NULL) | 1u;
    settings_load(&g_tui.settings);
    i18n_set_lang(g_tui.settings.lang);

    if (!term_init()) {
        fprintf(stderr, "caro_tui: this program needs an interactive terminal.\n");
        return 1;
    }
    g_tui.screen = TS_HOME;
    g_tui.prev_screen = TS_HOME;
    tui_menu_enter(TS_HOME);

    double last = term_now();
    while (!g_tui.quit) {
        TermEvent ev;
        int got = 0;
        while (term_poll(&ev, got ? 0 : 33)) {
            got = 1;
            if (ev.type == TEV_RESIZE) { term_invalidate(); continue; }
            if (g_tui.screen == TS_GAME) tui_game_event(&ev);
            else tui_menu_event(&ev);
            if (g_tui.quit) break;
        }
        double now = term_now();
        double dt = now - last;
        last = now;
        if (dt > 0.25) dt = 0.25;
        if (g_tui.screen == TS_GAME) tui_game_update(dt);
        else tui_menu_update(dt);

        int cols, rows;
        term_size(&cols, &rows);
        term_clear(TC_BG);
        if (cols < TUI_MIN_COLS || rows < TUI_MIN_ROWS) {
            char buf[128];
            snprintf(buf, sizeof buf, tr(S_TERM_TOO_SMALL_FMT), TUI_MIN_COLS, TUI_MIN_ROWS);
            term_print(1, rows / 2, buf, TC_GOLD, TC_BG, TATTR_BOLD);
        } else {
            if (g_tui.screen == TS_GAME) tui_game_draw();
            else tui_menu_draw();
            if (g_tui.toast[0] && now < g_tui.toast_until) {
                int w = utf8_cell_width(g_tui.toast) + 4;
                int x = (cols - w) / 2, y = rows - 4;
                term_box(x, y - 1, w, 3, 3, TC_GOLD, TC_PANEL);
                term_print(x + 2, y, g_tui.toast, TC_TEXT, TC_PANEL, TATTR_BOLD);
            }
        }
        term_present();
    }
    term_shutdown();
    return 0;
}
