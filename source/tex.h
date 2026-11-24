#pragma once
#include "common.h"

// bytes of a w x h texture in GPU_TEXCOLOR fmt
size_t tex_bytes(int w, int h, int fmt);
// C3D_TexInit in linear memory; when it is full, drops cached sprite pages and scenery tiles (those not drawn this
// frame) until it fits. false if nothing more can be dropped.
bool tex_alloc(C3D_Tex *t, int w, int h, int fmt);
