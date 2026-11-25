#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <sys/stat.h>
#include "common.h"
#include "version.h"

View g_view;
volatile const char *g_where = "start";

void *file_load(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    void *buf = n > 0 ? malloc(n) : NULL;
    if (buf && fread(buf, 1, n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (size) *size = buf ? (size_t)n : 0;
    return buf;
}

const char *data_path(const char *fmt, ...) {
    static char buf[256];
    va_list ap;
    va_start(ap, fmt);
    strcpy(buf, "romfs:/");
    vsnprintf(buf + 7, sizeof buf - 7, fmt, ap);
    va_end(ap);
    // for testing: files in SD:/3ds/3dsilksong/override/ (same paths) win over the data file's
    static int s_override = -1;
    struct stat st;
    if (s_override < 0) s_override = stat("sdmc:" SS_DATADIR "override", &st) == 0;
    if (s_override) {
        static char ob[256];
        snprintf(ob, sizeof ob, "sdmc:" SS_DATADIR "override/%s", buf + 7);
        if (stat(ob, &st) == 0) return ob;
    }
    return buf;
}

static int s_blend = -1;

void gfx_blend(int mode) {
    if (mode == s_blend) return;
    C2D_Flush();
    s_blend = mode;
    switch (mode) {
    case BLEND_ADD:
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_ZERO, GPU_ONE);
        break;
    case BLEND_MULTIPLY:
        // dst * (src + 1 - a): a multiply that leaves the transparent texels (black, alpha 0) alone
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_DST_COLOR, GPU_ONE_MINUS_SRC_ALPHA, GPU_ZERO, GPU_ONE);
        break;
    default:
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE,
                       GPU_ONE_MINUS_SRC_ALPHA);
        break;
    }
}

// called by main at the start of each frame (C2D_Prepare resets the blending)
void gfx_blend_reset(void) { s_blend = -1; gfx_blend(BLEND_NORMAL); }

void gfx_draw_world(C3D_Tex *tex, float u0, float v0, float u1, float v1,
                    float x0, float y0, float x1, float y1, float par, const C2D_ImageTint *tint) {
    const View *v = &g_view;
    float l = v->sw * 0.5f + (x0 - v->cx) * par * v->sx;
    float r = v->sw * 0.5f + (x1 - v->cx) * par * v->sx;
    float t = v->sh * 0.5f - (y1 - v->cy) * par * v->sy;
    float b = v->sh * 0.5f - (y0 - v->cy) * par * v->sy;
    gfx_draw_screen(tex, u0, v0, u1, v1, l, t, r, b, tint);
}

void gfx_draw_screen(C3D_Tex *tex, float u0, float v0, float u1, float v1, float l, float t, float r, float b,
                     const C2D_ImageTint *tint) {
    const View *v = &g_view;
    if ((l < 0 && r < 0) || (l > v->sw && r > v->sw) || b < 0 || t > v->sh) return;
    // The converted textures come out with their first row at the top of the image on the GPU, the other way from
    // what tools/ctrtex.py assumes (v counted from the bottom): the region's top is at 1 - v1, its bottom at 1 - v0.
    // citro2d reads a subtexture whose top < bottom as rotated 90 degrees, and doesn't take negative sizes either, so
    // keep top > bottom (top = 1 - v0: the region upside down) and turn the quad half a turn around its centre, which
    // puts it the right way up; swapping left and right undoes the mirror the half turn adds.
    Tex3DS_SubTexture sub = {(u16)tex->width, (u16)tex->height, u1, 1.0f - v0, u0, 1.0f - v1};
    C2D_Image img = {tex, &sub};
    float w = r - l, h = b - t;
    C2D_DrawParams p = {{l + w * 0.5f, t + h * 0.5f, w, h}, {w * 0.5f, h * 0.5f}, 0.5f, (float)M_PI};
    C2D_DrawImage(img, &p, tint);
}

void gfx_rect_world(float x0, float y0, float x1, float y1, u32 color) {
    const View *v = &g_view;
    float l = v->sw * 0.5f + (x0 - v->cx) * v->sx;
    float r = v->sw * 0.5f + (x1 - v->cx) * v->sx;
    float t = v->sh * 0.5f - (y1 - v->cy) * v->sy;
    float b = v->sh * 0.5f - (y0 - v->cy) * v->sy;
    C2D_DrawRectSolid(l, t, 0.5f, r - l, b - t, color);
}
