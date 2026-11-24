#!/usr/bin/env python3
"""Read back converted files and check them against the formats of docs/FORMATS.md (a reference reader for the
game's loaders, too).
  python tools/inspect_data.py romfs/rooms/Tut_01.ssr [romfs/rooms/Tut_01.sso romfs/sprites/hero.sss ...]"""
import os, sys, struct, zlib


def cstr(b):
    return b.split(b'\0', 1)[0].decode('utf-8', 'replace')


def room(path):
    b = open(path, 'rb').read()
    assert b[:4] == b'SSRM', 'not a room file'
    ver, cpu, gx, gy, gw, gh, sx, sy, ng, nt, nl, _ = struct.unpack_from('<HHffHHffHHHH', b, 4)
    o = 4 + struct.calcsize('<HHffHHffHHHH')
    rowb = (gw + 7) // 8
    solid = sum(bin(x).count('1') for x in b[o:o + rowb * gh])
    o += rowb * gh
    print(f'{os.path.basename(path)}: v{ver}, grid {gw}x{gh} at ({gx}, {gy}), {cpu} cells/unit, {solid} solid, '
          f'spawn ({sx:.1f}, {sy:.1f})')
    for _ in range(ng):
        name, tgt, ent = cstr(b[o:o + 16]), cstr(b[o + 16:o + 48]), cstr(b[o + 48:o + 64])
        x, y, w, h, dx, dy, el, er = struct.unpack_from('<6fBB2x', b, o + 64)
        o += 64 + 28
        print(f'  gate {name:8s} -> {tgt}:{ent}  rect ({x:.1f}, {y:.1f}, {w:.1f}, {h:.1f})')
    kinds = {}
    for _ in range(nt):
        k = b[o]
        o += 4 + 32 + 32
        kinds[k] = kinds.get(k, 0) + 1
    print('  triggers:', {({1: 'respawn', 2: 'hazard', 3: 'camlock'}).get(k, k): n for k, n in kinds.items()})
    data = open(path[:-4] + '.ssl', 'rb').read() if nl else b''
    for _ in range(nl):
        name = cstr(b[o:o + 8])
        z, par, ox, oy, ppu, tile, ntx, nty, n, fmt, blend = struct.unpack_from('<5fHHHHBB2x', b, o + 8)
        o += 8 + struct.calcsize('<5fHHHHBB2x')
        tiles = [struct.unpack_from('<HHII', b, o + 12 * i) for i in range(n)]
        o += 12 * n
        size = tile * tile * (4 if fmt == 0 else 2)
        for tx, ty, off, ln in tiles[:3] + tiles[-1:]:
            assert len(zlib.decompress(data[off:off + ln])) == size, 'bad tile'
        print(f'  layer {name:5s} z {z:6.2f} parallax {par:.3f}  {ntx}x{nty} tiles of {tile}, {n} stored, fmt {fmt}, blend {('normal', 'add', 'multiply')[blend]}')
    assert o == len(b), f'{len(b) - o} bytes left over'


def sprites(path):
    b = open(path, 'rb').read()
    assert b[:4] == b'SSSP', 'not a sprite set'
    ver, npg, nf, nc, _ = struct.unpack_from('<HHHHH', b, 4)
    o = 14
    pages = [struct.unpack_from('<HHBBxxII', b, o + 16 * i) for i in range(npg)]
    o += 16 * npg
    frames = [struct.unpack_from('<HH4f4f', b, o + 36 * i) for i in range(nf)]
    o += 36 * nf
    clips = []
    for _ in range(nc):
        name = cstr(b[o:o + 32])
        fps, loop, wrap, _x, n = struct.unpack_from('<fHBBH', b, o + 32)
        ids = struct.unpack_from(f'<{n}h', b, o + 42)
        o += 42 + 2 * n
        assert all(-1 <= i < nf for i in ids)
        clips.append((name, n, fps))
    for w, h, fmt, _x, _y, ln in pages:
        assert len(zlib.decompress(b[o:o + ln])) == w * h * (4 if fmt == 0 else 2)
        o += ln
    assert o == len(b), f'{len(b) - o} bytes left over'
    print(f'{os.path.basename(path)}: v{ver}, {npg} pages, {nf} frames, {nc} clips: ' +
          ', '.join(f'{n} ({c} @ {fps:g} fps)' for n, c, fps in clips[:8]) + (' ...' if nc > 8 else ''))


def objs(path):
    b = open(path, 'rb').read()
    assert b[:4] == b'SSOB', 'not an objects file'
    ver, n = struct.unpack_from('<HH', b, 4)
    rec = struct.calcsize('<HhfffffffffiiH2x') + 32 * 4 + 64
    assert len(b) == 8 + n * rec, f'size {len(b)} != {8 + n * rec}'
    kinds = {}
    for i in range(n):
        fl, parent = struct.unpack_from('<Hh', b, 8 + i * rec)
        assert -1 <= parent < n
        for bit, k in ((1, 'enemy'), (2, 'breakable'), (4, 'interact'), (8, 'animated'), (16, 'fsm'), (32, 'hazard')):
            if fl & bit:
                kinds[k] = kinds.get(k, 0) + 1
    print(f'{os.path.basename(path)}: v{ver}, {n} objects', kinds)


if __name__ == '__main__':
    for p in sys.argv[1:]:
        {'.ssr': room, '.sso': objs}.get(os.path.splitext(p)[1], sprites)(p)
