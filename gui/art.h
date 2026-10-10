/* art.h - procedurally generated pixel art (gui/art.c). No image files.
 * All textures are generated at art_init() with Image* functions and drawn with point filtering. */
#ifndef CARO_ART_H
#define CARO_ART_H

#include "raylib.h"

typedef enum {
    BG_HOME = 0,  /* grand temple hall: brick wall, rows of fire/water doors, vines, light shaft */
    BG_MENU,      /* calmer variant of the hall for setup/settings/saves */
    BG_BOARD3,    /* "Fire Shrine": warm red-brick chamber, lava glow at bottom, torches */
    BG_BOARD5,    /* "Forest Temple": mossy green bricks, hanging vines, fireflies */
    BG_BOARD10,   /* "Water Sanctum": blue-grey stone, waterfalls/pools, drifting bubbles */
    BG_COUNT
} ArtBg;

/* Mascots: the 16x16 creature (docs/DESIGN.md "Mascot"). Avatar i uses the variant table. */
typedef enum {
    MASCOT_FIRE = 0,   /* fire twin: flame crest  (avatar 0) */
    MASCOT_WATER,      /* water twin: droplet crest (avatar 1) */
    MASCOT_LEAF,       /* forest: sprout (avatar 2) */
    MASCOT_CROWN,      /* gold temple king: crown (avatar 3) */
    MASCOT_STONE,      /* stone guardian: helmet (avatar 4) */
    MASCOT_ROBOT,      /* temple automaton: antenna (avatar 5; also the Medium computer champion) */
    MASCOT_NOVICE,     /* computer champion, Easy: hooded apprentice (avatar 6, not selectable) */
    MASCOT_WARDEN,     /* computer champion, Hard: Obsidian Warden, horns + gem (avatar 7, not selectable) */
    MASCOT_COUNT
} MascotId;

void art_init(void);
void art_unload(void);

/* Full-screen background in virtual coords, with subtle animation (t = seconds).
 * Animated bits (torch flicker, light shaft, bubbles, fireflies) are frozen when reduced motion. */
void art_draw_background(ArtBg bg, float t);
ArtBg art_board_bg(int board_n);              /* 3 -> BG_BOARD3, 5 -> BG_BOARD5, else BG_BOARD10 */

/* Draw mascot scaled into dst (nearest-neighbour), blinking occasionally, with optional idle bob. */
void art_draw_mascot(MascotId m, Rectangle dst, float t, bool animate);
void art_draw_avatar(int avatar, Rectangle dst, float t);   /* framed portrait: stone frame + mascot; avatar 0..AVATAR_TOTAL-1 == MascotId */

/* "CARO TEMPLE" title: gold carved letters with dark outline + soft glow, centered at (cx, y). */
void art_draw_title(float cx, float y, float scale, float t);

/* Board pieces area: carved stone slab with inset grid for an n x n board in rect. */
void art_draw_board_slab(Rectangle r, int n, ArtBg theme);

#endif
