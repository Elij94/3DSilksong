#pragma once
// Shared helpers: little-endian reads, file loading, the view (camera -> screen) and drawing.
#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include <string.h>

static inline u16 rd16(const u8 *p) { return p[0] | (p[1] << 8); }
static inline s16 rds16(const u8 *p) { return (s16)rd16(p); }
static inline u32 rd32(const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }
static inline float rdf(const u8 *p) { u32 v = rd32(p); float f; memcpy(&f, &v, 4); return f; }
// copies a fixed-size, \0-padded string field into out (always terminated)
static inline void rdstr(char *out, const u8 *p, int n) { memcpy(out, p, n); out[n] = 0; }

static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float approachf(float v, float t, float d) { return v < t ? (v + d > t ? t : v + d) : (v - d < t ? t : v - d); }
static inline bool rects_overlap(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh) {
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

// whole file into a malloc'd buffer (NULL if missing); size optional
void *file_load(const char *path, size_t *size);
// "romfs:/" + rel
const char *data_path(const char *fmt, ...);

// The camera: (cx, cy) is the centre of the view in world units, y up. sx, sy: screen pixels per unit (equal on the
// 400x240 screen). hw, hh: half the view in units.
// the step of the frame the game is in (the watchdog writes it to the autotest log when the game stops)
extern volatile const char *g_where;

typedef struct {
    float cx, cy;
    float sx, sy;
    float sw, sh;
    float hw, hh;
    u32 frame;       // frame counter, for the texture caches
} View;
extern View g_view;

enum { BLEND_NORMAL = 0, BLEND_ADD = 1, BLEND_MULTIPLY = 2 };
void gfx_blend(int mode);
void gfx_blend_reset(void);   // after C2D_Prepare / at the start of a frame

// draws the texture region (u0,v0)-(u1,v1) (v from the bottom) on the world rectangle x0..x1, y0..y1 (y up),
// seen through parallax par. Swapped u or v flip the image. tint: NULL, or a citro2d tint.
void gfx_draw_world(C3D_Tex *tex, float u0, float v0, float u1, float v1,
                    float x0, float y0, float x1, float y1, float par, const C2D_ImageTint *tint);
// the same on a screen rectangle (pixels, y down)
// the depth images are drawn at (citro2d tests it): 0.5, more for what goes over the menus's shading
extern float g_draw_depth;
void gfx_draw_screen(C3D_Tex *tex, float u0, float v0, float u1, float v1, float l, float t, float r, float b,
                     const C2D_ImageTint *tint);
// a plain rectangle in world units (debug, effects)
void gfx_rect_world(float x0, float y0, float x1, float y1, u32 color);
