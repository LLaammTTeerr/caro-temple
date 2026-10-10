/* STUB - replaced by its unit. */
#include "screens.h"
#include "scene.h"
#include "ui.h"
#include "theme.h"
#include "art.h"
#include "app.h"
static void draw(void) { art_draw_background(BG_MENU, g_app.time); ui_frame_begin(); text_draw("Settings (stub)", 40, 40, FS_H1, COL_GOLD); if (ui_button((Rectangle){40, 600, 240, 64}, "Back", BTN_NORMAL)) scene_goto(SCENE_HOME); ui_frame_end(); }
void scr_settings_register(void) { scene_register(SCENE_SETTINGS, (Scene){0, 0, 0, draw}); }
