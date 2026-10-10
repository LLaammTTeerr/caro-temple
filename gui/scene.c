/* STUB - replaced by unit "Input + scene manager". Instant switching, no transitions. */
#include "scene.h"
static Scene scenes[SCENE_COUNT];
static SceneId stack[8];
static int depth;
void scene_register(SceneId id, Scene s) { scenes[id] = s; }
static void enter(void) { if (scenes[stack[depth - 1]].enter) scenes[stack[depth - 1]].enter(); }
static void leave(void) { if (depth && scenes[stack[depth - 1]].leave) scenes[stack[depth - 1]].leave(); }
void scene_init(SceneId f) { depth = 1; stack[0] = f; enter(); }
void scene_goto(SceneId id) { leave(); depth = 1; stack[0] = id; enter(); }
void scene_push(SceneId id) { leave(); if (depth < 8) stack[depth++] = id; enter(); }
void scene_pop(void) { if (depth > 1) { leave(); depth--; enter(); } }
SceneId scene_current(void) { return stack[depth - 1]; }
SceneId scene_previous(void) { return depth > 1 ? stack[depth - 2] : SCENE_COUNT; }
bool scene_transitioning(void) { return false; }
void scene_update(float dt) { Scene *s = &scenes[stack[depth - 1]]; if (s->update) s->update(dt); }
void scene_draw(void) { Scene *s = &scenes[stack[depth - 1]]; if (s->draw) s->draw(); }
