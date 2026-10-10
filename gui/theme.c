/* STUB - replaced by unit "UI widgets + theme". Minimal working version. */
#include <math.h>
#include <stddef.h>
#include "theme.h"
Font g_font;
void theme_load(void) { g_font = LoadFontEx("assets/fonts/VT323-Regular.ttf", 64, NULL, 0); if (g_font.texture.id == 0) g_font = GetFontDefault(); }
void theme_unload(void) { if (g_font.texture.id != GetFontDefault().texture.id) UnloadFont(g_font); }
Vector2 text_measure(const char *s, float size) { return MeasureTextEx(g_font, s, size, 0); }
void text_draw(const char *s, float x, float y, float size, Color c) { DrawTextEx(g_font, s, (Vector2){x, y}, size, 0, c); }
void text_draw_shadow(const char *s, float x, float y, float size, Color c) { text_draw(s, x + 3, y + 3, size, COL_SHADOW); text_draw(s, x, y, size, c); }
void text_draw_bold(const char *s, float x, float y, float size, Color c) { text_draw(s, x, y, size, c); text_draw(s, x + 1, y, size, c); }
void text_draw_outline(const char *s, float x, float y, float size, Color c, Color o, int px) { for (int dx = -px; dx <= px; dx++) for (int dy = -px; dy <= px; dy++) if (dx || dy) text_draw(s, x + dx, y + dy, size, o); text_draw(s, x, y, size, c); }
void text_draw_aligned(const char *s, float x, float y, float size, Color c, int align) { float w = text_measure(s, size).x; text_draw(s, align == 1 ? x - w / 2 : align == 2 ? x - w : x, y, size, c); }
float text_draw_wrapped(const char *s, Rectangle box, float size, Color c, int align) { (void)align; text_draw(s, box.x, box.y, size, c); return size; }
Color color_lerp(Color a, Color b, float t) { return (Color){ (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t), (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t) }; }
float ease_out_cubic(float t) { t = 1 - t; return 1 - t * t * t; }
float ease_in_out(float t) { return t < 0.5f ? 2 * t * t : 1 - powf(-2 * t + 2, 2) / 2; }
