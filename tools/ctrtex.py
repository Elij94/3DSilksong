# 3DS GPU textures. Turns an RGBA image -> the bytes C3D_Tex expects, so the game can memcpy them straight into a
# texture (no conversion on the console). The Morton order is the same as HK3DS's lyr_split.py (MIT).
#   - the image is flipped vertically first (the GPU's texture origin is the bottom-left corner)
#   - then it is cut into 8x8 blocks, row by row, each block's texels in Morton (Z) order
#   - RGBA8: u32 little-endian (R << 24 | G << 16 | B << 8 | A); RGBA4: u16 (R4 << 12 | G4 << 8 | B4 << 4 | A4)
# Width and height must be powers of two, 8 to 1024.
import numpy as np

FMT_RGBA8, FMT_RGBA4 = 0, 4          # the GPU_TEXCOLOR values
FORMATS = {'rgba8': FMT_RGBA8, 'rgba4': FMT_RGBA4}

_M = None


def _morton_index():
    """position (y*8+x) in an 8x8 block -> index in the block's Morton order"""
    global _M
    if _M is None:
        m = np.zeros(64, int)
        for k in range(64):
            x = (k & 1) | ((k >> 1) & 2) | ((k >> 2) & 4)
            y = ((k >> 1) & 1) | ((k >> 2) & 2) | ((k >> 3) & 4)
            m[y * 8 + x] = k
        _M = m
    return _M


def _swizzle(px):
    """px: (h, w) array of packed texels, top row first -> flat array in GPU order"""
    h, w = px.shape
    px = px[::-1]
    blocks = px.reshape(h // 8, 8, w // 8, 8).transpose(0, 2, 1, 3).reshape(h // 8, w // 8, 64)
    out = np.zeros_like(blocks)
    out[:, :, _morton_index()] = blocks
    return out.reshape(-1)


def encode(rgba, fmt=FMT_RGBA8):
    """rgba: (h, w, 4) uint8 array or PIL RGBA image -> bytes"""
    a = np.asarray(rgba, dtype=np.uint8)
    h, w = a.shape[:2]
    if w & (w - 1) or h & (h - 1) or min(w, h) < 8 or max(w, h) > 1024:
        raise ValueError(f'texture size {w}x{h}: must be powers of two, 8 to 1024')
    r, g, b, al = (a[..., i].astype(np.uint32) for i in range(4))
    if fmt == FMT_RGBA8:
        return _swizzle((r << 24) | (g << 16) | (b << 8) | al).astype('<u4').tobytes()
    if fmt == FMT_RGBA4:
        q = lambda c: (c * 15 + 127) // 255
        return _swizzle(((q(r) << 12) | (q(g) << 8) | (q(b) << 4) | q(al)).astype(np.uint16)).astype('<u2').tobytes()
    raise ValueError(f'unknown texture format {fmt}')


def pow2(n, lo=8):
    p = lo
    while p < n:
        p *= 2
    return p
