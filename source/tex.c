#include "tex.h"
#include "sprite.h"
#include "room.h"

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
