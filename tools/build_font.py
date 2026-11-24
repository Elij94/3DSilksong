#!/usr/bin/env python3
"""The game's TextMeshPro fonts -> <out>/fonts/<font>.ssf: the glyph table and the signed-distance-field atlas as an
A8 GPU texture (format: docs/FORMATS.md). A distance field stays sharp at any size: draw it with an alpha test
around 0.5, or smoothstep for soft edges.

  python tools/build_font.py <out_dir>               every font of the export but the CJK ones
  python tools/build_font.py <out_dir> --all         the Chinese, Japanese and Korean fonts too (big atlases)
  python tools/build_font.py <out_dir> Perpetua-SDF  only these fonts (asset names)
  python tools/build_font.py --list"""
import os, re, sys, glob, struct, zlib, argparse
import numpy as np
from PIL import Image
from sscommon import EX, guidmap, mono_fields, f
import ctrtex

CJK = re.compile(r'chinese|japanese|korean|simsun|cjk', re.I)


def fonts():
    out = {}
    for p in glob.glob(os.path.join(EX, '**', '*.asset'), recursive=True):
        with open(p, errors='replace') as fh:
            if 'm_glyphInfoList' not in fh.read(4000):
                continue
        out[os.path.basename(p)[:-6]] = p
    return out


def safe(name):
    return re.sub(r'[^a-z0-9_]+', '_', name.lower()).strip('_')


def build(out, name, path):
    v = mono_fields(path)
    info = v.get('m_fontInfo') or {}
    glyphs = v.get('m_glyphInfoList') or []
    tex = guidmap().get((v.get('atlas') or {}).get('guid'))
    if not glyphs or not tex or not os.path.exists(tex):
        print(f'{name}: no glyphs or no atlas in the export')
        return False
    img = Image.open(tex)
    a = np.asarray(img.convert('RGBA'))
    # the distance field is in the alpha channel, or in the colour channels of an opaque atlas
    field = a[..., 3] if a[..., 3].min() < 255 else a[..., 0]
    H, W = field.shape
    aw, ah = int(f(info.get('AtlasWidth')) or W), int(f(info.get('AtlasHeight')) or H)
    tw, th = min(1024, ctrtex.pow2(W)), min(1024, ctrtex.pow2(H))
    if (tw, th) != (W, H):                    # a texture size the GPU takes (powers of two, at most 1024)
        field = np.asarray(Image.fromarray(field).resize((tw, th), Image.LANCZOS))
    rgba = np.zeros((th, tw, 4), np.uint8)
    rgba[..., 3] = field
    blob = zlib.compress(ctrtex.encode(rgba, ctrtex.FMT_A8), 9)
    rows = bytearray()
    n = 0
    for g in glyphs:
        code = int(f(g.get('id')))
        x, y, w, h = (f(g.get(k)) for k in ('x', 'y', 'width', 'height'))
        # glyph rectangles are in atlas pixels from the top-left corner; texture v goes up from the bottom
        u0, u1 = x / aw, (x + w) / aw
        v0, v1 = 1 - (y + h) / ah, 1 - y / ah
        rows += struct.pack('<I4f5f', code, u0, v0, u1, v1, f(g.get('xOffset')), f(g.get('yOffset')), w, h,
                            f(g.get('xAdvance')))
        n += 1
    head = b'SSFN' + struct.pack('<HHfffffHHB3xI', 1, n, f(info.get('PointSize')), f(info.get('LineHeight')),
                                 f(info.get('Ascender')), f(info.get('Descender')), f(info.get('Padding')),
                                 tw, th, ctrtex.FMT_A8, len(blob))
    os.makedirs(os.path.join(out, 'fonts'), exist_ok=True)
    dst = os.path.join(out, 'fonts', safe(name) + '.ssf')
    with open(dst, 'wb') as fh:
        fh.write(head + rows + blob)
    print(f'{os.path.basename(dst)}: {n} glyphs, atlas {tw}x{th}, {os.path.getsize(dst) / 1024:.0f} KB')
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('out', nargs='?')
    ap.add_argument('names', nargs='*')
    ap.add_argument('--all', action='store_true')
    ap.add_argument('--list', action='store_true')
    a = ap.parse_args()
    fs = fonts()
    if a.list:
        for n in sorted(fs):
            print(n + ('   (CJK)' if CJK.search(n) else ''))
        return
    if not a.out:
        ap.error('give an output folder (or --list)')
    want = a.names or [n for n in fs if a.all or not CJK.search(n)]
    missing = [n for n in want if n not in fs]
    if missing:
        raise SystemExit('no font named: ' + ', '.join(missing))
    done = sum(build(a.out, n, fs[n]) for n in sorted(want))
    print(f'fonts: {done} of {len(want)}')


if __name__ == '__main__':
    main()
