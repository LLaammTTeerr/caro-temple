/* scene.c - scene stack with timed "temple door" transitions (see scene.h).
 *
 * Transition timeline (duration D, midpoint D/2):
 *   first half : old top scene is drawn, no update; doors close and the screen darkens;
 *                at D/2 the requested switch runs (leave/enter, input_consume).
 *   second half: new top scene is drawn and updated; doors open.
 * Requests made while a transition runs replace a single queued slot (latest wins); the
 * queued request starts a new transition as soon as the current one finishes. */
#include <stdbool.h>
#include "raylib.h"
#include "scene.h"
#include "input.h"
#include "app.h"
#include "theme.h"

#define STACK_MAX        8
#define TR_DUR_NORMAL    0.35f
#define TR_DUR_REDUCED   0.12f
#define TR_DIM_ALPHA     0.5f

typedef enum { REQ_NONE = 0, REQ_GOTO, REQ_PUSH, REQ_POP } ReqKind;

typedef struct {
    ReqKind kind;
    SceneId id;
} Req;

static Scene scenes[SCENE_COUNT];
static SceneId stack[STACK_MAX];
static int depth;

static bool tr_active;
static bool tr_in;      /* false: first half (old scene shown); true: second half */
static float tr_t;      /* seconds since the transition started */
static float tr_dur;
static Req tr_req;      /* executed at the midpoint */
static Req queued;      /* latest request made during a transition */

static void top_enter(void)
{
    if (depth > 0 && scenes[stack[depth - 1]].enter) scenes[stack[depth - 1]].enter();
}

static void top_leave(void)
{
    if (depth > 0 && scenes[stack[depth - 1]].leave) scenes[stack[depth - 1]].leave();
}

static bool req_valid(Req r)
{
    switch (r.kind) {
    case REQ_GOTO: return (int)r.id >= 0 && r.id < SCENE_COUNT;
    case REQ_PUSH: return (int)r.id >= 0 && r.id < SCENE_COUNT && depth < STACK_MAX;
    case REQ_POP: return depth > 1;
    default: return false;
    }
}

static void start_transition(Req r)
{
    tr_req = r;
    tr_active = true;
    tr_in = false;
    tr_t = 0.0f;
    tr_dur = g_app.settings.reduced_motion ? TR_DUR_REDUCED : TR_DUR_NORMAL;
}

static void request(ReqKind kind, SceneId id)
{
    Req r = { kind, id };
    if (!req_valid(r)) return;
    if (tr_active) {
        queued = r; /* latest wins */
        return;
    }
    start_transition(r);
}

/* Runs at the midpoint of a transition. Re-checks validity since the stack may have changed. */
static void execute(Req r)
{
    switch (r.kind) {
    case REQ_GOTO:
        if (req_valid(r)) {
            top_leave();
            depth = 1;
            stack[0] = r.id;
            top_enter();
        }
        break;
    case REQ_PUSH:
        if (req_valid(r)) {
            top_leave();
            stack[depth++] = r.id;
            top_enter();
        }
        break;
    case REQ_POP:
        if (req_valid(r)) {
            top_leave();
            depth--;
            top_enter();
        }
        break;
    default:
        break;
    }
    input_consume();
}

void scene_register(SceneId id, Scene s)
{
    if ((int)id >= 0 && id < SCENE_COUNT) scenes[id] = s;
}

void scene_init(SceneId first)
{
    if ((int)first < 0 || first >= SCENE_COUNT) return;
    top_leave();
    tr_active = false;
    tr_in = false;
    tr_req.kind = REQ_NONE;
    queued.kind = REQ_NONE;
    depth = 1;
    stack[0] = first;
    top_enter();
}

void scene_goto(SceneId id) { request(REQ_GOTO, id); }
void scene_push(SceneId id) { request(REQ_PUSH, id); }
void scene_pop(void) { request(REQ_POP, SCENE_COUNT); }

SceneId scene_current(void)
{
    return depth > 0 ? stack[depth - 1] : SCENE_COUNT;
}

SceneId scene_previous(void)
{
    return depth > 1 ? stack[depth - 2] : SCENE_COUNT;
}

bool scene_transitioning(void) { return tr_active; }

void scene_update(float dt)
{
    if (tr_active) {
        float mid = tr_dur * 0.5f;
        tr_t += dt;
        if (!tr_in && tr_t >= mid) {
            tr_in = true;
            execute(tr_req);
        }
        if (tr_t >= tr_dur) {
            tr_active = false;
            tr_in = false;
            if (queued.kind != REQ_NONE) {
                Req next = queued;
                queued.kind = REQ_NONE;
                if (req_valid(next)) {
                    start_transition(next);
                    return; /* the chained transition starts fading out next frame */
                }
            }
        } else if (!tr_in) {
            return; /* first half: nothing updates */
        }
    }
    if (depth > 0 && scenes[stack[depth - 1]].update) scenes[stack[depth - 1]].update(dt);
}

void scene_draw(void)
{
    if (depth > 0 && scenes[stack[depth - 1]].draw) scenes[stack[depth - 1]].draw();
    if (!tr_active) return;

    /* Progress toward "fully dark": 0 at the start, 1 at the midpoint, back to 0 at the end. */
    float half = tr_dur * 0.5f;
    float s = tr_in ? 1.0f - (tr_t - half) / half : tr_t / half;
    if (s < 0.0f) s = 0.0f;
    if (s > 1.0f) s = 1.0f;
    float e = s * s * (3.0f - 2.0f * s); /* smoothstep */

    DrawRectangleRec((Rectangle){0, 0, VIRTUAL_W, VIRTUAL_H}, Fade(COL_STONE_900, TR_DIM_ALPHA * e));

    /* Two stone doors close from the screen edges to the centre, with a thin gold seam. */
    float door = (VIRTUAL_W * 0.5f) * e;
    if (door > 0.5f) {
        DrawRectangleRec((Rectangle){0, 0, door, VIRTUAL_H}, COL_STONE_900);
        DrawRectangleRec((Rectangle){VIRTUAL_W - door, 0, door, VIRTUAL_H}, COL_STONE_900);
        DrawRectangleRec((Rectangle){door - 2.0f, 0, 2.0f, VIRTUAL_H}, Fade(COL_GOLD, e));
        DrawRectangleRec((Rectangle){VIRTUAL_W - door, 0, 2.0f, VIRTUAL_H}, Fade(COL_GOLD, e));
    }
}
