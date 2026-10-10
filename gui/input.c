/* input.c - unified keyboard / mouse / gamepad snapshot in virtual coordinates (see input.h).
 * input_update() samples every source once per frame; all queries read that snapshot. */
#include <math.h>
#include <stdbool.h>
#include "raylib.h"
#include "input.h"
#include "app.h" /* VIRTUAL_W / VIRTUAL_H */

#define REPEAT_DELAY   0.35f
#define REPEAT_RATE    0.08f
#define STICK_DEADZONE 0.5f
#define STICK_MOVE_EPS 0.05f
#define MAX_PADS       4
#define CHAR_QUEUE     16

/* Slots: 0 = player 1 (player 0), 1 = player 2 (player 1), 2 = any source. */
#define SLOT_ANY 2
#define SLOTS    3

typedef struct {
    bool down;   /* held this frame */
    bool prev;   /* held last frame */
    bool fire;   /* edge, or an auto-repeat tick this frame */
    float rep_t; /* seconds until the next repeat tick while held */
} Btn;

typedef struct {
    bool up, down, left, right;
} Dirs;

static Btn btn[ACT_COUNT][SLOTS];
static Btn bs_btn;                    /* backspace (repeats) */
static bool pad_state[MAX_PADS][ACT_COUNT];
static int pad_n;
static float prev_axis[MAX_PADS][2];

static float vp_scale = 1.0f, vp_ox, vp_oy;
static bool consumed;
static bool act_any;
static InputDevice last_device = DEVICE_MOUSE;
static Vector2 mouse_v;
static bool m_moved, m_pressed, m_released, m_down;
static float m_wheel;
static int chars[CHAR_QUEUE];
static int char_n, char_i;

/* Keys whose press/release counts as activity (other keys count via GetKeyPressed). */
static const int mapped_keys[] = {
    KEY_W, KEY_A, KEY_S, KEY_D, KEY_SPACE, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_ENTER, KEY_KP_ENTER, KEY_ESCAPE, KEY_HOME, KEY_BACKSPACE
};

static bool repeats(ActionType a)
{
    return a == ACT_UP || a == ACT_DOWN || a == ACT_LEFT || a == ACT_RIGHT;
}

static int slot_of(int player)
{
    if (player == 0) return 0;
    if (player == 1) return 1;
    return SLOT_ANY;
}

/* Edge detection plus auto-repeat. Repeat: first tick after REPEAT_DELAY, then every REPEAT_RATE. */
static void update_btn(Btn *b, bool down, bool rep, float dt)
{
    b->fire = false;
    if (down) {
        if (!b->prev) {
            b->fire = true;
            b->rep_t = REPEAT_DELAY;
        } else if (rep) {
            b->rep_t -= dt;
            if (b->rep_t <= 0.0f) {
                b->fire = true;
                b->rep_t += REPEAT_RATE;
                if (b->rep_t <= 0.0f) b->rep_t = REPEAT_RATE;
            }
        }
    }
    b->down = down;
    b->prev = down;
}

/* Keyboard sources for one player slot (0 or 1). Esc and Home are shared by both players. */
static bool key_state(ActionType a, int slot)
{
    if (a == ACT_BACK) return IsKeyDown(KEY_ESCAPE);
    if (a == ACT_HOME) return IsKeyDown(KEY_HOME);
    if (slot == 0) {
        switch (a) {
        case ACT_UP: return IsKeyDown(KEY_W);
        case ACT_DOWN: return IsKeyDown(KEY_S);
        case ACT_LEFT: return IsKeyDown(KEY_A);
        case ACT_RIGHT: return IsKeyDown(KEY_D);
        case ACT_CONFIRM: return IsKeyDown(KEY_SPACE);
        default: return false;
        }
    }
    switch (a) {
    case ACT_UP: return IsKeyDown(KEY_UP);
    case ACT_DOWN: return IsKeyDown(KEY_DOWN);
    case ACT_LEFT: return IsKeyDown(KEY_LEFT);
    case ACT_RIGHT: return IsKeyDown(KEY_RIGHT);
    case ACT_CONFIRM: return IsKeyDown(KEY_ENTER) || IsKeyDown(KEY_KP_ENTER);
    default: return false;
    }
}

/* Left stick as digital directions: dominant axis, magnitude must reach the 0.5 deadzone. */
static Dirs stick_dirs(float x, float y)
{
    Dirs d = { false, false, false, false };
    if (fabsf(y) >= fabsf(x)) {
        d.up = y <= -STICK_DEADZONE;
        d.down = y >= STICK_DEADZONE;
    } else {
        d.left = x <= -STICK_DEADZONE;
        d.right = x >= STICK_DEADZONE;
    }
    return d;
}

static bool pad_button_state(int pad, ActionType a, const Dirs *st)
{
    switch (a) {
    case ACT_UP: return IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_UP) || st->up;
    case ACT_DOWN: return IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_DOWN) || st->down;
    case ACT_LEFT: return IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_LEFT) || st->left;
    case ACT_RIGHT: return IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || st->right;
    case ACT_CONFIRM: return IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    case ACT_BACK: return IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
    case ACT_HOME: return IsGamepadButtonDown(pad, GAMEPAD_BUTTON_MIDDLE_RIGHT);
    default: return false;
    }
}

/* Index into pad_state feeding a player slot, or -1. A single pad serves both players;
 * with two or more pads, pad 0 -> player 1 and pad 1 -> player 2. */
static int pad_for_slot(int slot)
{
    if (pad_n == 1) return 0;
    if (pad_n >= 2) return slot;
    return -1;
}

/* Samples connected gamepads. Returns true if a pad button or stick moved this frame. */
static bool sample_gamepads(void)
{
    int ids[MAX_PADS];
    pad_n = 0;
    for (int i = 0; i < MAX_PADS; i++)
        if (IsGamepadAvailable(i)) ids[pad_n++] = i;

    bool act = false;
    for (int k = 0; k < pad_n; k++) {
        float x = GetGamepadAxisMovement(ids[k], GAMEPAD_AXIS_LEFT_X);
        float y = GetGamepadAxisMovement(ids[k], GAMEPAD_AXIS_LEFT_Y);
        Dirs st = stick_dirs(x, y);
        for (int a = ACT_UP; a < ACT_COUNT; a++)
            pad_state[k][a] = pad_button_state(ids[k], (ActionType)a, &st);
        if (fabsf(x - prev_axis[k][0]) + fabsf(y - prev_axis[k][1]) > STICK_MOVE_EPS) act = true;
        prev_axis[k][0] = x;
        prev_axis[k][1] = y;
        for (int b = GAMEPAD_BUTTON_LEFT_FACE_UP; b <= GAMEPAD_BUTTON_RIGHT_THUMB; b++)
            if (IsGamepadButtonPressed(ids[k], b)) act = true;
    }
    return act;
}

static bool pad_held(int k, ActionType a)
{
    return k >= 0 && k < pad_n && pad_state[k][a];
}

static bool sample_keyboard_activity(void)
{
    bool act = false;
    while (GetKeyPressed() != 0) act = true; /* drain queue; any key counts */
    for (unsigned i = 0; i < sizeof mapped_keys / sizeof mapped_keys[0]; i++)
        if (IsKeyReleased(mapped_keys[i])) act = true;
    return act;
}

static bool sample_mouse(void)
{
    Vector2 m = GetMousePosition();
    if (vp_scale > 0.0f) {
        m.x = (m.x - vp_ox) / vp_scale;
        m.y = (m.y - vp_oy) / vp_scale;
    }
    if (m.x < 0.0f) m.x = 0.0f;
    if (m.y < 0.0f) m.y = 0.0f;
    if (m.x > VIRTUAL_W) m.x = VIRTUAL_W;
    if (m.y > VIRTUAL_H) m.y = VIRTUAL_H;
    mouse_v = m;

    Vector2 d = GetMouseDelta();
    m_moved = d.x != 0.0f || d.y != 0.0f;
    m_pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    m_released = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    m_down = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    m_wheel = GetMouseWheelMove();
    return m_moved || m_pressed || m_wheel != 0.0f ||
           IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE);
}

void input_set_viewport(float scale, float offset_x, float offset_y)
{
    vp_scale = scale;
    vp_ox = offset_x;
    vp_oy = offset_y;
}

void input_update(float dt)
{
    consumed = false;

    bool pad_act = sample_gamepads();
    bool key_act = sample_keyboard_activity();
    bool mouse_act = sample_mouse();

    for (int a = ACT_UP; a < ACT_COUNT; a++) {
        bool rep = repeats((ActionType)a);
        bool any = false;
        for (int p = 0; p < 2; p++) {
            bool k = key_state((ActionType)a, p);
            bool g = pad_held(pad_for_slot(p), (ActionType)a);
            update_btn(&btn[a][p], k || g, rep, dt);
            any = any || k;
        }
        /* "any" is one combined source: both keyboard slots plus every connected pad. */
        for (int k = 0; k < pad_n; k++)
            any = any || pad_held(k, (ActionType)a);
        update_btn(&btn[a][SLOT_ANY], any, rep, dt);
    }
    update_btn(&bs_btn, IsKeyDown(KEY_BACKSPACE), true, dt);

    char_n = 0;
    char_i = 0;
    int c;
    while ((c = GetCharPressed()) != 0)
        if (char_n < CHAR_QUEUE) chars[char_n++] = c;

    act_any = pad_act || key_act || mouse_act;
    if (pad_act) last_device = DEVICE_GAMEPAD;
    else if (key_act) last_device = DEVICE_KEYBOARD;
    else if (mouse_act) last_device = DEVICE_MOUSE;
}

bool input_pressed(ActionType a, int player)
{
    if (consumed || a <= ACT_NONE || a >= ACT_COUNT) return false;
    return btn[a][slot_of(player)].fire;
}

bool input_down(ActionType a, int player)
{
    if (consumed || a <= ACT_NONE || a >= ACT_COUNT) return false;
    return btn[a][slot_of(player)].down;
}

Vector2 input_mouse(void) { return mouse_v; }

bool input_mouse_moved(void) { return !consumed && m_moved; }
bool input_mouse_pressed(void) { return !consumed && m_pressed; }
bool input_mouse_released(void) { return !consumed && m_released; }
bool input_mouse_down(void) { return !consumed && m_down; }
float input_mouse_wheel(void) { return consumed ? 0.0f : m_wheel; }

InputDevice input_last_device(void) { return last_device; }

bool input_any_activity(void) { return !consumed && act_any; }

int input_char(void)
{
    if (consumed || char_i >= char_n) return 0;
    return chars[char_i++];
}

bool input_backspace(void) { return !consumed && bs_btn.fire; }

void input_consume(void) { consumed = true; }
