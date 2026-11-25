#pragma once
#include "common.h"

// bytes of a w x h texture in GPU_TEXCOLOR fmt
size_t tex_bytes(int w, int h, int fmt);
// C3D_TexInit in linear memory; when it is full, drops cached sprite pages and scenery tiles (those not drawn this
// frame) until it fits. false if nothing more can be dropped.
void tex_premultiply(void *data, int w, int h, int fmt);
bool tex_alloc(C3D_Tex *t, int w, int h, int fmt);
// inflates a w x h texture (GPU order) into dst, shrunk by f (1, 2, 4, 8: each texel the mean of f x f), so dst is
// (w/f) x (h/f) in GPU order too
bool tex_inflate_scaled(const void *z, size_t zlen, int w, int h, int fmt, int f, void *dst);
// the same with a factor across and one down (the 800 wide mode has twice the pixels across)
bool tex_inflate_scaled2(const void *z, size_t zlen, int w, int h, int fmt, int fx, int fy, void *dst);
