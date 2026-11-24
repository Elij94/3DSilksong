#!/usr/bin/env python3
"""Reference render of a room, from the converted files, with the engine's own math (source/room.c, game.c).

Shows what the 3DS should display: a frame of the view at a camera position, and (with --full) every layer of the room
assembled whole. Used to tell a bug in the engine's placement math from one in the texture data or the GPU side.

  python tools/render_ref.py romfs Tut_01 --cam 47 92 -o work/ref_Tut_01.png
  python tools/render_ref.py romfs Tut_01 --full -o work/ref_Tut_01_full.png
"""
import argparse
import os
import struct
import sys
import zlib

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
import ctrtex  # noqa: E402

SY = 14.0          # screen pixels per unit (source/main.c)
SW, SH = 400, 240  # the 400x240 frame (the 800x240 mode only doubles x)


def unswizzle(data, w, h, fmt):
    """GPU-order texture -> (h, w, 4) RGBA, top row first: the inverse of ctrtex._swizzle"""
    if fmt == ctrtex.FMT_RGBA4:
        px = np.frombuffer(data, '<u2').astype(np.uint32)
        rgba = np.stack([(px >> 12) & 15, (px >> 8) & 15, (px >> 4) & 15, px & 15], -1) * 17
    elif fmt == ctrtex.FMT_RGBA8:
        px = np.frombuffer(data, '<u4')
        rgba = np.stack([(px >> 24) & 255, (px >> 16) & 255, (px >> 8) & 255, px & 255], -1)
    else:
        raise ValueError(f'format {fmt}')
    blocks = rgba.reshape(h // 8, w // 8, 64, 4)
    out = blocks[:, :, ctrtex._morton_index()]          # back to y*8+x inside each block
    img = out.reshape(h // 8, w // 8, 8, 8, 4).transpose(0, 2, 1, 3, 4).reshape(h, w, 4)
    return img[::-1].astype(np.uint8)                   # ctrtex flipped it: unflip


def read_room(romfs, scene):
    d = open(os.path.join(romfs, 'rooms', scene + '.ssr'), 'rb').read()
    ssl = open(os.path.join(romfs, 'rooms', scene + '.ssl'), 'rb').read()
    (cpu,) = struct.unpack_from('<H', d, 6)
    gx, gy, gw, gh, sx, sy, ng, nt, nl = struct.unpack_from('<ffHHffHHH', d, 8)
    o = 36 + ((gw + 7) // 8) * gh + 92 * ng + 68 * nt
    layers = []
    for _ in range(nl):
        name = d[o:o + 8].split(b'\0')[0].decode()
        z, par, ox, oy, ppu = struct.unpack_from('<5f', d, o + 8)
        tile, ntx, nty, n = struct.unpack_from('<4H', d, o + 28)
        fmt, blend = d[o + 36], d[o + 37]
        o += 40
        tiles = []
        for _ in range(n):
            tx, ty, off, size = struct.unpack_from('<HHII', d, o)
            o += 12
            tiles.append((tx, ty, unswizzle(zlib.decompress(ssl[off:off + size]), tile, tile, fmt)))
        layers.append(dict(name=name, z=z, par=par, ox=ox, oy=oy, ppu=ppu, tile=tile, ntx=ntx, nty=nty,
                           fmt=fmt, blend=blend, tiles=tiles))
    return dict(spawn=(sx, sy), layers=layers)


def rank(L):
    n = L['name']
    if n.startswith('bg'):
        return 4 - int(n[2])
    if n.startswith('main'):
        return 4
    if n.startswith('front'):
        return 5
    return 3 if L['z'] > 0 else 5


def blend_into(canvas, img, x, y, mode):
    """canvas float RGB (H, W, 3); img uint8 RGBA placed with its top-left at (x, y)"""
    H, W = canvas.shape[:2]
    h, w = img.shape[:2]
    x0, y0, x1, y1 = max(0, x), max(0, y), min(W, x + w), min(H, y + h)
    if x1 <= x0 or y1 <= y0:
        return
    src = img[y0 - y:y1 - y, x0 - x:x1 - x].astype(np.float32) / 255
    a = src[..., 3:4]
    dst = canvas[y0:y1, x0:x1]
    if mode == 1:
        dst += src[..., :3] * a
    elif mode == 2:
        dst *= src[..., :3] + 1 - a
    else:
        dst *= 1 - a
        dst += src[..., :3] * a
    np.clip(dst, 0, 1, out=dst)


def render_view(room, cx, cy):
    """the frame the engine draws with the camera centred on (cx, cy)"""
    canvas = np.full((SH, SW, 3), 8 / 255, np.float32)
    for L in sorted(room['layers'], key=rank):
        T = L['tile'] / L['ppu']
        for tx, ty, img in L['tiles']:
            x0, y1 = L['ox'] + tx * T, L['oy'] - ty * T
            l = SW / 2 + (x0 - cx) * L['par'] * SY
            t = SH / 2 - (y1 - cy) * L['par'] * SY
            size = T * L['par'] * SY
            if l > SW or t > SH or l + size < 0 or t + size < 0:
                continue
            s = max(1, int(round(size)))
            blend_into(canvas, np.asarray(Image.fromarray(img).resize((s, s), Image.BILINEAR)), int(round(l)),
                       int(round(t)), L['blend'])
    return Image.fromarray((canvas * 255).astype(np.uint8))


def render_full(room, scale=0.25):
    """every layer assembled at parallax 1, at scale x its pixels: is the data itself right?"""
    xs, ys = [], []
    for L in room['layers']:
        T = L['tile'] / L['ppu']
        xs += [L['ox'], L['ox'] + L['ntx'] * T]
        ys += [L['oy'], L['oy'] - L['nty'] * T]
    ppu = max(L['ppu'] for L in room['layers']) * scale
    X0, Y1 = min(xs), max(ys)
    W, H = int((max(xs) - X0) * ppu) + 1, int((Y1 - min(ys)) * ppu) + 1
    canvas = np.full((H, W, 3), 8 / 255, np.float32)
    for L in sorted(room['layers'], key=rank):
        T = L['tile'] / L['ppu']
        s = max(1, int(round(T * ppu)))
        for tx, ty, img in L['tiles']:
            x = int(round((L['ox'] + tx * T - X0) * ppu))
            y = int(round((Y1 - (L['oy'] - ty * T)) * ppu))
            blend_into(canvas, np.asarray(Image.fromarray(img).resize((s, s), Image.BILINEAR)), x, y, L['blend'])
    return Image.fromarray((canvas * 255).astype(np.uint8))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('romfs')
    ap.add_argument('scene')
    ap.add_argument('--cam', nargs=2, type=float, help='camera centre (default: the spawn point)')
    ap.add_argument('--full', action='store_true', help='the whole room, every layer at parallax 1')
    ap.add_argument('-o', '--out', required=True)
    a = ap.parse_args()
    room = read_room(a.romfs, a.scene)
    if a.full:
        img = render_full(room)
    else:
        cx, cy = a.cam or (room['spawn'][0], room['spawn'][1] + 2)
        img = render_view(room, cx, cy).resize((800, 480), Image.NEAREST)
    img.save(a.out)
    print(a.out, img.size)


if __name__ == '__main__':
    main()
