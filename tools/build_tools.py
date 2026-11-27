#!/usr/bin/env python3
"""Hornet's tools and crests (Data Assets/Tools) -> romfs/text/tools.txt and romfs/sprites/tools.sss.

tools.txt:
  T <name> <type> <name sheet> <name key> <desc sheet> <desc key> <icon clip> <hud clip> <storage> <throw prefab>
    <velocity x> <velocity y> <cooldown> <fsm event>
      type: 0 red (attack), 1 blue (defence), 2 yellow (exploration), 3 skill; storage: uses before a refill (red);
      throw prefab: its pack (rooms/pf_<name>.*; tools/build_prefabs.py builds them from work/tool_prefabs.txt)
  C <name> <name sheet> <name key> <desc sheet> <desc key> <crest clip> <slot count>
  S <crest> <index> <type> <x> <y> <attack binding> <locked>
Names with spaces are written with '|'. Clips: sprites/tools.sss.

  python tools/build_tools.py <romfs dir>
"""
import os, sys, glob
import numpy as np
from PIL import Image
from sscommon import EX, guidmap, yaml_file, f
from build_room import sprite_image
from build_sprites import add_image, write_set
import ctrtex

TOOLS = os.path.join(EX, 'Data Assets', 'Tools')
SCALE = 28 / 64


def bar(s):
    return str(s).replace(' ', '|') if s else '-'


def mono(path):
    for t, v in yaml_file(path).values():
        if t == 'MonoBehaviour':
            return v
    return {}


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else 'romfs'
    gm = guidmap()
    clips, frames, images, seen, clip_of = {}, [], [], {}, {}

    def clip(ref):
        if not ref or not ref.get('guid'):
            return '-'
        k = (ref['guid'], ref.get('fileID'))
        if k not in clip_of:
            got = sprite_image(*k)
            if not got:
                clip_of[k] = '-'
            else:
                img, quad = got[0], got[1]
                h, w = img.shape[:2]
                img = np.asarray(Image.fromarray(img).resize((max(1, round(w * SCALE)), max(1, round(h * SCALE))),
                                                             Image.LANCZOS))
                name = f't{len(clips)}'
                clips[name] = dict(fps=12, loop_start=0, wrap=6, ids=[add_image(img, quad, frames, images, seen)])
                clip_of[k] = name
        return clip_of[k]

    lines, prefabs = [], set()
    for p in sorted(glob.glob(os.path.join(TOOLS, 'Tool Items', '*.asset'))):
        v = mono(p)
        if not v or 'usageOptions' not in v and 'type' not in v:
            continue
        name = v.get('m_Name') or os.path.basename(p)[:-6]
        dn, de = v.get('displayName') or {}, v.get('description') or {}
        u = v.get('usageOptions') or {}
        tp = (u.get('ThrowPrefab') or {}).get('guid')
        pref = ''
        if tp and gm.get(tp, '').endswith('.prefab'):
            from build_prefabs import pack_name
            pref = pack_name(gm[tp])
            prefabs.add(tp)
        vel = u.get('ThrowVelocity') or {}
        lines.append(' '.join(str(x) for x in (
            'T', bar(name), int(f(v.get('type'))), bar(dn.get('Sheet')), bar(dn.get('Key')), bar(de.get('Sheet')),
            bar(de.get('Key')), clip(v.get('inventorySprite')), clip(v.get('hudSprite')),
            int(f(v.get('baseStorageAmount'))), bar(pref), round(float(f(vel.get('x'))), 3),
            round(float(f(vel.get('y'))), 3), round(float(f(u.get('ThrowCooldown'))), 3), bar(u.get('FsmEventName')))))
    for p in sorted(glob.glob(os.path.join(TOOLS, 'Crest Items', '*.asset'))):
        v = mono(p)
        slots = v.get('slots') or []
        name = v.get('m_Name') or os.path.basename(p)[:-6]
        dn, de = v.get('displayName') or {}, v.get('description') or {}
        lines.append(' '.join(str(x) for x in ('C', bar(name), bar(dn.get('Sheet')), bar(dn.get('Key')),
                                               bar(de.get('Sheet')), bar(de.get('Key')), clip(v.get('crestSprite')),
                                               len(slots))))
        for i, s in enumerate(slots):
            pos = s.get('Position') or {}
            lines.append(' '.join(str(x) for x in ('S', bar(name), i, int(f(s.get('Type'))),
                                                   round(float(f(pos.get('x'))), 3), round(float(f(pos.get('y'))), 3),
                                                   int(f(s.get('AttackBinding'))), int(f(s.get('IsLocked'))))))
    write_set(os.path.join(out, 'sprites', 'tools.sss'), clips, frames, images, ctrtex.FORMATS['rgba4'])
    os.makedirs(os.path.join(out, 'text'), exist_ok=True)
    open(os.path.join(out, 'text', 'tools.txt'), 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
    open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'work', 'tool_prefabs.txt'), 'w').write(
        '\n'.join(sorted(prefabs)) + '\n')
    print(f"{sum(1 for l in lines if l[0] == 'T')} tools, {sum(1 for l in lines if l[0] == 'C')} crests, "
          f"{len(clips)} pictures, {len(prefabs)} throw prefabs")


if __name__ == '__main__':
    main()
