#include "tex.h"
#include "sprite.h"
#include "room.h"
#include "inflate.h"
#include <stdlib.h>

size_t tex_bytes(int w, int h, int fmt) {
    static const u8 bpp8[16] = {32, 24, 16, 16, 16, 16, 16, 8, 8, 4, 8, 4, 4, 4, 4, 4};
    return (size_t)w * h * bpp8[fmt & 15] / 8;
}

bool tex_alloc(C3D_Tex *t, int w, int h, int fmt) {
    // keep some linear memory for citro3d's command buffers and citro2d's vertices
    for (;;) {
        if (linearSpaceFree() > tex_bytes(w, h, fmt) + (1 << 20) && C3D_TexInit(t, w, h, (GPU_TEXCOLOR)fmt)) return true;
        if (!sprites_evict_one() && !room_evict_tile()) return false;
    }
}

// offset of texel (x, y) in a texture w wide, in GPU order: 8x8 blocks row by row, Morton order inside
static inline u32 tex_index(u32 x, u32 y, u32 w) {
    u32 m = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
    return ((y >> 3) * (w >> 3) + (x >> 3)) * 64 + m;
}

static u8 *s_big;
static size_t s_big_size;

bool tex_inflate_scaled(const void *z, size_t zlen, int w, int h, int fmt, int f, void *dst) {
    size_t full = tex_bytes(w, h, fmt);
    if (f <= 1) return zinflate(z, zlen, dst, full) == (long)full;
    if (full > s_big_size) {
        free(s_big);
        s_big = malloc(full);
        s_big_size = s_big ? full : 0;
        if (!s_big) return false;
    }
    if (zinflate(z, zlen, s_big, full) != (long)full) return false;
    int ow = w / f, oh = h / f, n = f * f;
    // y runs in memory order here, the same in both textures, so the image keeps its orientation
    if (fmt == GPU_RGBA4) {
        const u16 *in = (const u16 *)s_big;
        u16 *out = dst;
        for (int y = 0; y < oh; y++)
            for (int x = 0; x < ow; x++) {
                u32 c0 = 0, c1 = 0, c2 = 0, c3 = 0;
                for (int j = 0; j < f; j++)
                    for (int i = 0; i < f; i++) {
                        u16 t = in[tex_index(x * f + i, y * f + j, w)];
                        c0 += t >> 12; c1 += (t >> 8) & 15; c2 += (t >> 4) & 15; c3 += t & 15;
                    }
                c0 = (c0 + n / 2) / n; c1 = (c1 + n / 2) / n; c2 = (c2 + n / 2) / n; c3 = (c3 + n / 2) / n;
                out[tex_index(x, y, ow)] = (u16)(c0 << 12 | c1 << 8 | c2 << 4 | c3);
            }
        return true;
    }
    if (fmt != GPU_RGBA8 && fmt != GPU_RGB8 && fmt != GPU_A8 && fmt != GPU_L8) return false;
    int bpp = (int)(tex_bytes(8, 8, fmt) / 64);
    const u8 *in = s_big;
    u8 *out = dst;
    for (int y = 0; y < oh; y++)
        for (int x = 0; x < ow; x++) {
            u32 acc[4] = {0, 0, 0, 0};
            for (int j = 0; j < f; j++)
                for (int i = 0; i < f; i++) {
                    const u8 *t = in + tex_index(x * f + i, y * f + j, w) * bpp;
                    for (int k = 0; k < bpp; k++) acc[k] += t[k];
                }
            u8 *o = out + tex_index(x, y, ow) * bpp;
            for (int k = 0; k < bpp; k++) o[k] = (u8)((acc[k] + n / 2) / n);
        }
    return true;
}
