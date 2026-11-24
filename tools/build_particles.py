#!/usr/bin/env python3
"""The particle textures the converted rooms use -> <out>/particles/textures.sss, one 1-frame clip per texture,
named like the "texture" field of the rooms' .spt emitters (format: docs/FORMATS.md). Texture sheets (tilesX x
tilesY animation frames, in the emitter) stay whole: the game picks the tile.

  python tools/build_particles.py <out_dir> [--max 128]     run after build_room.py (it reads work/rooms/*.parts.json)"""
import os, glob, json, argparse
import numpy as np
from PIL import Image
from sscommon import WORK, guidmap
import ctrtex
from build_sprites import add_image, write_set


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('out')
    ap.add_argument('--max', type=int, default=128, help='largest side of a texture, in pixels (default 128)')
    ap.add_argument('--fmt', choices=list(ctrtex.FORMATS), default='rgba4')
    a = ap.parse_args()
    textures = {}
    for p in glob.glob(os.path.join(WORK, 'rooms', '*.parts.json')):
        for e in json.load(open(p)):
            if e.get('texture') and e.get('texture_guid'):
                textures[e['texture']] = e['texture_guid']
    frames, images, seen, clips, bad = [], [], {}, {}, []
    for name, g in sorted(textures.items()):
        path = guidmap().get(g)
        if not path or not os.path.exists(path) or not path.lower().endswith(('.png', '.tga', '.jpg')):
            bad.append(name)
            continue
        img = Image.open(path).convert('RGBA')
        k = min(1.0, a.max / max(img.size))
        if k < 1:
            img = img.resize((max(1, round(img.width * k)), max(1, round(img.height * k))), Image.LANCZOS)
        arr = np.asarray(img)
        # a quad of one unit; the emitter's size scales it
        clips[name] = dict(fps=0.0, loop_start=0, wrap=0,
                           ids=[add_image(arr, (-0.5, -0.5, 0.5, 0.5), frames, images, seen, trim=False)])
    if not frames:
        raise SystemExit('no particle texture (run build_room.py first)')
    write_set(os.path.join(a.out, 'particles', 'textures.sss'), clips, frames, images, ctrtex.FORMATS[a.fmt])
    print(f'particles: {len(clips)} textures' + (f', {len(bad)} not found: {" ".join(bad[:10])}' if bad else ''))


if __name__ == '__main__':
    main()
