#!/usr/bin/env python3
"""Draws every room at its spawn point with tools/render_ref.py and lists the darkest views: a quick way to catch a
room the converter got wrong (a black veil, a missing layer) without walking through the game.

  python tools/scan_rooms.py romfs [--out work/scan] [--worst 30]
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import render_ref  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('romfs')
    ap.add_argument('--out', default='work/scan')
    ap.add_argument('--worst', type=int, default=30)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    res = []
    for f in sorted(os.listdir(os.path.join(a.romfs, 'rooms'))):
        if not f.endswith('.ssr'):
            continue
        scene = f[:-4]
        try:
            room = render_ref.read_room(a.romfs, scene)
            img = render_ref.render_view(room, room['spawn'][0], room['spawn'][1] - 0.3)
        except Exception as ex:   # noqa: BLE001
            res.append((-1.0, scene, str(ex)))
            continue
        px = np.asarray(img, dtype=np.float32)
        lit = float((px.max(2) > 40).mean())   # share of the view that isn't (nearly) black
        res.append((lit, scene, ''))
        if lit < 0.15:
            img.save(os.path.join(a.out, scene + '.png'))
    res.sort()
    for lit, scene, err in res[:a.worst]:
        print(f'{lit:6.2f}  {scene}  {err}')
    print(f'{sum(1 for r in res if 0 <= r[0] < 0.15)} of {len(res)} rooms nearly black at their spawn point '
          f'(pictures in {a.out})')


if __name__ == '__main__':
    main()
