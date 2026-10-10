/* STUB - replaced by unit "Input + scene manager". */
#include "input.h"
static float sc = 1, ox, oy;
void input_set_viewport(float s, float x, float y) { sc = s; ox = x; oy = y; }
void input_update(float dt) { (void)dt; }
static bool key_for(ActionType a, int p, bool (*f)(int))
{
    switch (a) {
    case ACT_UP: return (p != 1 && f(KEY_W)) || (p != 0 && f(KEY_UP));
    case ACT_DOWN: return (p != 1 && f(KEY_S)) || (p != 0 && f(KEY_DOWN));
    case ACT_LEFT: return (p != 1 && f(KEY_A)) || (p != 0 && f(KEY_LEFT));
    case ACT_RIGHT: return (p != 1 && f(KEY_D)) || (p != 0 && f(KEY_RIGHT));
    case ACT_CONFIRM: return (p != 1 && f(KEY_SPACE)) || (p != 0 && f(KEY_ENTER));
    case ACT_BACK: return f(KEY_ESCAPE);
    case ACT_HOME: return f(KEY_HOME);
    default: return false;
    }
}
bool input_pressed(ActionType a, int p) { return key_for(a, p, IsKeyPressed); }
bool input_down(ActionType a, int p) { return key_for(a, p, IsKeyDown); }
Vector2 input_mouse(void) { Vector2 m = GetMousePosition(); return (Vector2){(m.x - ox) / sc, (m.y - oy) / sc}; }
bool input_mouse_moved(void) { Vector2 d = GetMouseDelta(); return d.x || d.y; }
bool input_mouse_pressed(void) { return IsMouseButtonPressed(MOUSE_BUTTON_LEFT); }
bool input_mouse_released(void) { return IsMouseButtonReleased(MOUSE_BUTTON_LEFT); }
bool input_mouse_down(void) { return IsMouseButtonDown(MOUSE_BUTTON_LEFT); }
float input_mouse_wheel(void) { return GetMouseWheelMove(); }
InputDevice input_last_device(void) { return DEVICE_MOUSE; }
bool input_any_activity(void) { return GetKeyPressed() || input_mouse_moved() || input_mouse_pressed(); }
int input_char(void) { return GetCharPressed(); }
bool input_backspace(void) { return IsKeyPressed(KEY_BACKSPACE); }
void input_consume(void) {}
