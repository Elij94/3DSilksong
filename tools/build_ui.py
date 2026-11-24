#!/usr/bin/env python3
"""Interface sprites (HUD, inventory, map, menus, prompts, journal, shops...) -> named sprite sets:
<out>/ui/<atlas>.sss, one 1-frame clip per sprite, named after the sprite (format: docs/FORMATS.md, the same as the
tk2d sets). The game packs its Unity sprites in sprite atlases, whose textures the export names
"sactx-<n>-<w>x<h>-<format>-<Atlas>-<hash>.png"; the loose textures under Sprites/UI make one set per subfolder.

  python tools/build_ui.py <out_dir>                 the interface atlases (names matching UI_ATLASES below)
  python tools/build_ui.py <out_dir> HUD Inventory   only these atlases
  python tools/build_ui.py --list                    every atlas of the export, with its sprite count
Options: --scale S (default 0.4375, 64 -> 28 px per unit, as the other sprites), --fmt rgba8|rgba4 (default rgba4).
Needs tools/index_export.py to have run (tex2sprites.pkl, metamaps.pkl)."""
import os, re, sys, pickle, argparse, collections
import numpy as np
from PIL import Image
from sscommon import ROOT, EX
import ctrtex
from build_room import sprite_image
from build_sprites import add_image, write_set

UI_ATLASES = re.compile(r'(^|_)(UI|HUD|Inventory|Menu|Map|Prompts?|journal|Quest|Achievement|Currency|Buttons|Controller|'
                        r'Credits|Title|Tools|Crest|Masks|Load_Icon|LongSaveIcon|Shop|Lore_Board|Premenu|Traders|'
                        r'Bellway_Map|Tube_Map|Area_Art|Signs?|Sprites_UI)($|_)', re.I)


def safe(name):
    return re.sub(r'[^a-z0-9_]+', '_', name.lower()).strip('_')


def groups():
    """atlas name -> [(sprite name, sprite guid)]"""
    spr, tex = pickle.load(open(os.path.join(ROOT, 'metamaps.pkl'), 'rb'))
    t2s = pickle.load(open(os.path.join(ROOT, 'tex2sprites.pkl'), 'rb'))
    out = collections.defaultdict(list)
    for tg, sprites in t2s.items():
        rel = (tex.get(tg) or '').replace('\\', '/')
        base = os.path.basename(rel)
        m = re.match(r'sactx-\d+-\d+x\d+-[^-]+-(.+)-[0-9a-f]{8}\.png$', base)
        if m:
            key = m.group(1)
        elif rel.startswith('Sprites/UI/'):
            parts = rel.split('/')
            key = 'Sprites_UI_' + (parts[2] if len(parts) > 3 else 'misc')
        else:
            continue
        out[key] += [(name, g) for name, _rect, g in sprites]
    return out


def build(out, atlas, sprites, scale, fmt):
    frames, images, seen, clips = [], [], {}, {}
    used = collections.Counter()
    for name, g in sorted(sprites):
        got = sprite_image(g, 21300000)
        if not got:
            continue
        img, quad = got
        if scale != 1:
            h, w = img.shape[:2]
            img = np.asarray(Image.fromarray(img).resize((max(1, round(w * scale)), max(1, round(h * scale))),
                                                         Image.LANCZOS))
        used[name] += 1
        cname = name if used[name] == 1 else f'{name}#{used[name]}'     # two sprites of the same name: keep both
        clips[cname] = dict(fps=0.0, loop_start=0, wrap=0, ids=[add_image(img, quad, frames, images, seen, trim=False)])
    if not frames:
        return False
    write_set(os.path.join(out, 'ui', safe(atlas) + '.sss'), clips, frames, images, fmt)
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('out', nargs='?')
    ap.add_argument('atlases', nargs='*')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--scale', type=float, default=28 / 64)
    ap.add_argument('--fmt', choices=list(ctrtex.FORMATS), default='rgba4')
    a = ap.parse_args()
    g = groups()
    if a.list:
        for k in sorted(g):
            print(f'{len(g[k]):5d}  {k}' + ('   (interface)' if UI_ATLASES.search(k) else ''))
        return
    if not a.out:
        ap.error('give an output folder (or --list)')
    want = a.atlases or [k for k in g if UI_ATLASES.search(k)]
    missing = [k for k in want if k not in g]
    if missing:
        raise SystemExit('no atlas named: ' + ', '.join(missing))
    done = sum(build(a.out, k, g[k], a.scale, ctrtex.FORMATS[a.fmt]) for k in sorted(want))
    print(f'ui: {done} of {len(want)} atlases, {sum(len(g[k]) for k in want)} sprites')


if __name__ == '__main__':
    main()
