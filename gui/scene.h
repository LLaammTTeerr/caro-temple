/* scene.h - scene stack with time-based transitions (gui/scene.c). */
#ifndef CARO_SCENE_H
#define CARO_SCENE_H

#include <stdbool.h>

typedef enum {
    SCENE_HOME = 0,
    SCENE_SETUP,     /* new game: mode / board / difficulty / rule */
    SCENE_PROFILE,   /* names + avatars, then start */
    SCENE_SETTINGS,
    SCENE_SAVES,     /* load or save depending on g_app.saves_mode */
    SCENE_GAME,
    SCENE_COUNT
} SceneId;

typedef struct {
    void (*enter)(void);        /* called when scene becomes active (after push/goto, or pop back to it) */
    void (*leave)(void);        /* called when scene stops being active */
    void (*update)(float dt);   /* input + logic; only for the top scene, not during transitions' first half */
    void (*draw)(void);         /* draw in virtual coords; full screen (backgrounds included) */
} Scene;

void scene_register(SceneId id, Scene s);
void scene_init(SceneId first);        /* clears stack, activates `first` instantly (no transition) */

/* Transitions: fade-through-dark ~0.35 s (0.12 s if reduced motion). Requests made while a
 * transition is running are queued (latest wins). Never blocks; driven by scene_update(dt). */
void scene_goto(SceneId id);           /* clear stack and switch to id */
void scene_push(SceneId id);           /* push id on top (e.g. Settings over Home/Game) */
void scene_pop(void);                  /* back to previous scene (no-op if stack has 1) */
SceneId scene_current(void);
SceneId scene_previous(void);          /* scene under the top one, or SCENE_COUNT if none */
bool scene_transitioning(void);

void scene_update(float dt);           /* updates top scene + transition state */
void scene_draw(void);                 /* draws top scene and the transition overlay */

#endif
