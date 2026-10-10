/* STUB - replaced by unit "Menu screens". */
#include "screens.h"
#include "scene.h"
#include "ui.h"
#include "theme.h"
#include "art.h"
#include "app.h"
static void draw(void)
{
    art_draw_background(BG_HOME, g_app.time);
    ui_frame_begin();
    art_draw_title(VIRTUAL_W / 2, 80, 1, g_app.time);
    if (ui_button((Rectangle){520, 300, 240, 64}, "Play", BTN_PRIMARY)) app_start_new_game();
    if (ui_button((Rectangle){520, 380, 240, 64}, "Settings", BTN_NORMAL)) scene_push(SCENE_SETTINGS);
    if (ui_button((Rectangle){520, 460, 240, 64}, "Quit", BTN_DANGER)) g_app.quit = true;
    ui_frame_end();
}
void scr_home_register(void) { scene_register(SCENE_HOME, (Scene){0, 0, 0, draw}); }
