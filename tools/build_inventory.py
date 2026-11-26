#!/usr/bin/env python3
"""The inventory's Inv pane (Prefabs/Camera/_GameCameras.prefab, HudCamera/In-game/Inventory/Inv) and the game's
collectables (Data Assets/Collectables/Collectables Master List), for the bottom screen (source/inventory.c):

- ui/inv_pane.sss: the pane's pictures, 64 pixels a unit at most 72 pixels tall: "nail1".."nail5" (the needle's
  levels), "spool_piece", "spool_half", "spool_full", "heart_back", "heart1".."heart4", "spool_wide", "spool_slim",
  each skill's icon by its object's name ("Sprint", "Needolin"...), and "c<i>_<s>" for collectable i's state s.
- text/inventory.txt, one record per line (spaces in a value as '\\x1f', - for none):
  "D <part> <state> <sheet> <name key> <sheet> <desc key>"   the needle (part nail, state 0..4), heart pieces (heart,
     0..4), spool pieces (spool_piece, 0 none 1 half 2 full), the silk spool (thread, 0)
  "K <clip> <test> <sheet> <name key> <sheet> <desc key>"    a skill, shown when its PlayerData test passes
  "C <i> <name> <always> <overrides>"                          a collectable (its PlayerData count "item:<name>";
     always: isAlwaysUnlocked; overrides: shown when one of its states' tests passes, whatever the count)
  "S <i> <s> <sheet> <name key> <sheet> <desc key> <test>"     a state of collectable i (the last whose test
     passes is shown; tests as in build_shops.py)

  python tools/build_inventory.py <romfs dir>"""
import os, re, sys
import numpy as np
from PIL import Image
from sscommon import EX, guidmap, f
from build_room import Scene, sprite_image
from build_sprites import add_image, write_set
from build_shops import tests, w, load_asset
import ctrtex

PPU, MAXH = 64, 72
PANE = '_GameCameras/HudCamera/In-game/Inventory/Inv/Inv_Items/'


def main():
    out = sys.argv[1]
    frames, images, seen, clips, cache = [], [], {}, {}, {}

    def pic(ref, clip, maxh=MAXH):
        g = (ref or {}).get('guid')
        if not g:
            return False
        key = (g, (ref or {}).get('fileID'), maxh)
        if key not in cache:
            got = sprite_image(g, (ref or {}).get('fileID'))
            if not got:
                cache[key] = None
            else:
                img, q = got
                h, wd = img.shape[:2]
                k = min(PPU / (wd / max(1e-6, q[2] - q[0])), maxh / h, 1.0)
                nw, nh = max(1, round(wd * k)), max(1, round(h * k))
                if (nw, nh) != (wd, h):
                    img = np.asarray(Image.fromarray(img).resize((nw, nh), Image.LANCZOS))
                cache[key] = add_image(img, (-nw / 2, -nh / 2, nw / 2, nh / 2), frames, images, seen)
        if cache[key] is None:
            return False
        clips[clip] = dict(fps=12, loop_start=0, wrap=6, ids=[cache[key]])
        return True

    lines = []
    sc = Scene(os.path.join(EX, 'Prefabs', 'Camera', '_GameCameras.prefab'))

    def path(g):
        p = []
        while g:
            p.append(str(sc.go[g]['m_Name']))
            t = sc.tr.get(g)
            fa = sc.tr_id.get(((t or {}).get('m_Father') or {}).get('fileID')) if t else None
            g = fa['m_GameObject']['fileID'] if fa else None
        return '/'.join(reversed(p))

    by_path = {path(g): g for g in sc.go}

    def comps(rel):
        return sc.comps.get(by_path.get(PANE + rel), [])

    def sprite_of(rel):
        return next((v.get('m_Sprite') for t, v in comps(rel) if t == 'SpriteRenderer'), None)

    def mono(rel, key):
        return next((v for t, v in comps(rel) if t == 'MonoBehaviour' and key in v), None)

    def ls(x):
        x = x or {}
        return f"{w(x.get('Sheet'))} {w(x.get('Key'))}"

    # the needle: its levels' pictures and texts
    for i in range(5):
        pic(sprite_of(f'Needle/Nail{i + 1}'), f'nail{i + 1}')
    nail = mono('Needle', 'displayStates') or {}
    for i, st in enumerate(nail.get('displayStates') or []):
        lines.append(f"D nail {i} {ls(st.get('DisplayName'))} {ls(st.get('Description'))}")
    # heart pieces
    pic(sprite_of('Needle Shift/Heart Pieces/Backboard'), 'heart_back')
    for i in range(4):
        pic(sprite_of(f'Needle Shift/Heart Pieces/Pieces {i + 1}'), f'heart{i + 1}')
    hp = mono('Needle Shift/Heart Pieces', 'displayStates') or {}
    for i, st in enumerate(hp.get('displayStates') or []):
        lines.append(f"D heart {i} {ls(st.get('DisplayName'))} {ls(st.get('Description'))}")
    # spool pieces
    pic(sprite_of('Needle Shift/Spool Pieces'), 'spool_piece')
    pic(sprite_of('Needle Shift/Spool Pieces/Half'), 'spool_half')
    pic(sprite_of('Needle Shift/Spool Pieces/Full'), 'spool_full')
    sp = mono('Needle Shift/Spool Pieces', 'halfState') or {}
    for i, k in enumerate(('emptyState', 'halfState', 'fullState')):
        st = sp.get(k) or {}
        lines.append(f"D spool_piece {i} {ls(st.get('DisplayName'))} {ls(st.get('Description'))}")
    # the silk spool
    pic(sprite_of('Needle Shift/Spool Group/Spool/Wide'), 'spool_wide', 96)
    pic(sprite_of('Needle Shift/Spool Group/Spool/Slim'), 'spool_slim', 96)
    th = mono('Needle Shift/Spool Group/Spool', 'heartVerDesc') or {}
    lines.append(f"D thread 0 {ls(th.get('displayName'))} {ls(th.get('description'))}")
    # the skills round it
    base = PANE + 'Needle Shift/Spool Group/Radial Layout/'
    for p, g in sorted(by_path.items()):
        if not p.startswith(base) or '/' in p[len(base):]:
            continue
        name = p[len(base):]
        m = next((v for t, v in sc.comps.get(g, []) if t == 'MonoBehaviour' and 'displayName' in v and 'Test' in v), None)
        sr = next((v.get('m_Sprite') for t, v in sc.comps.get(g, []) if t == 'SpriteRenderer'), None)
        if m and pic(sr, name, 40):
            lines.append(f"K {w(name)} {w(tests(m.get('Test')))} {ls(m.get('displayName'))} {ls(m.get('description'))}")
    # the collectables, in the master list's order
    gm = guidmap()
    master = open(os.path.join(EX, 'Data Assets', 'Collectables', 'Collectables Master List.asset'),
                  encoding='utf-8').read()
    n = 0
    for g in re.findall(r'guid: (\w+), type: 2', master):
        it = load_asset(g)
        p = gm.get(g)
        if not it or not p:
            continue
        name = os.path.splitext(os.path.basename(p))[0]
        if int(f(it.get('isHidden') or 0)):
            continue
        states = []
        if it.get('states'):
            for st in it['states']:
                states.append((st.get('DisplayName'), st.get('Description'), st.get('Icon'), tests(st.get('Test'))))
        else:
            nm = it.get('displayName') or it.get('typeName') or it.get('singleDisplayName')
            ds = it.get('description') or it.get('typeDescription') or it.get('singleDescription')
            ic = it.get('icon') or it.get('typeIcon')
            if not (ic or {}).get('guid'):
                ic = it.get('singleIcon')
            if not (ic or {}).get('guid') and it.get('singleIcons'):
                ic = it['singleIcons'][0]
            states.append((nm, ds, ic, '-'))
        lines.append(f"C {n} {w(name)} {int(f(it.get('isAlwaysUnlocked') or 0))} {int(f(it.get('overridesCollected') or 0))}")
        for s, (nm, ds, ic, t) in enumerate(states):
            pic(ic, f'c{n}_{s}', 48)
            lines.append(f"S {n} {s} {ls(nm)} {ls(ds)} {w(t)}")
        n += 1
    os.makedirs(os.path.join(out, 'ui'), exist_ok=True)
    os.makedirs(os.path.join(out, 'text'), exist_ok=True)
    write_set(os.path.join(out, 'ui', 'inv_pane.sss'), clips, frames, images, ctrtex.FORMATS['rgba4'])
    open(os.path.join(out, 'text', 'inventory.txt'), 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
    print(f'{n} collectables, {sum(1 for l in lines if l[0] == "K")} skills, {len(images)} pictures')


if __name__ == '__main__':
    main()
