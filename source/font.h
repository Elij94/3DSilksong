#pragma once
// The game's TextMeshPro fonts (fonts/<name>.ssf, tools/build_font.py): distance-field atlases drawn with the GPU's
// alpha test at the 0.5 contour, so the letters stay sharp at any size.
#include "common.h"

typedef struct Font Font;

enum { FONT_LEFT, FONT_CENTER, FONT_RIGHT };

// "trajanpro_bold_sdf" (titles and menus), "perpetua_tmpro" (body text)...; cached, NULL if missing
Font *font_get(const char *name);
// a line's width in screen pixels at this size (the height of a capital letter, about, in pixels)
float font_width(Font *f, const char *utf8, float size);
// draws a UTF-8 string (one line; '\n' starts another) at screen pixels (x, y = its top), coloured
void font_draw(Font *f, const char *utf8, float x, float y, float size, u32 color, int align);
// wrapped to max_w pixels; returns the height used
float font_draw_wrapped(Font *f, const char *utf8, float x, float y, float size, u32 color, float max_w, int align);
