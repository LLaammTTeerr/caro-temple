/* STUB - replaced by unit "In-game overlays". */
#include "screens.h"
#include "scene.h"
#include "ui.h"
#include "input.h"
#include "app.h"
static OverlayId cur;
void overlay_open(OverlayId id) { cur = id; }
void overlay_close(void) { cur = OVL_NONE; }
OverlayId overlay_current(void) { return cur; }
bool overlay_update(float dt) { (void)dt; if (cur && input_pressed(ACT_BACK, -1)) cur = OVL_NONE; return cur != OVL_NONE; }
void overlay_draw(void) { if (!cur) return; ui_dim_screen(0.6f); ui_frame_begin(); Rectangle c = ui_popup(520, 300, "Overlay (stub)", 1); if (ui_button((Rectangle){c.x, c.y + c.height - 64, 200, 56}, "Home", BTN_NORMAL)) { cur = OVL_NONE; scene_goto(SCENE_HOME); } ui_frame_end(); }
