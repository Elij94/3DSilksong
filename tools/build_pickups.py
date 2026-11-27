#!/usr/bin/env python3
"""The items lying in the rooms (CollectableItemPickup) -> romfs/text/pickups.txt and romfs/sprites/pickups.sss.

pickups.txt, one line a pickup:
  <room> <index> <x> <y> <kind> <item> <pd bool or -> <clip> <icon clip> <name sheet> <name key> <link>
kind: the item asset's class (CollectableItem, ToolItem, ...); item: the asset's name; pd bool: the PlayerData bool the
pickup sets (playerDataBool), '-' none; clip: its picture's clip in pickups.sss ('-' none); icon clip: the item's
icon (the popup); name: its display name's text sheet and key; link: PlayerDataCollectable's PlayerData field
(b:<bool> or i:<int>), '-' none. Spaces in names are written '|'.
The engine draws them, gives the item when Hornet touches one and remembers it in the save (pickup:<room>:<index>).

  python tools/build_pickups.py <romfs dir> [--jobs 6]
"""
import os, sys, re, glob, json, argparse, collections
from concurrent.futures import ProcessPoolExecutor
import numpy as np
from PIL import Image

EXS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'ssdata', 'ex', 'Scenes')
SCALE = 28 / 64


def one_scene(args):
    path, romfs = args
    room = os.path.basename(path)[:-6]
    if not os.path.exists(os.path.join(romfs, 'rooms', room + '.ssr')):
        return room, [], {}
    txt = open(path, encoding='utf-8', errors='replace').read()
    if 'pickupAnim' not in txt:
        return room, [], {}
    import build_room as br
    from sscommon import guidmap, f
    gm = guidmap()
    scripts = br.script_names()
    sc = br.Scene(path)
    out, kinds = [], {}
    for gid, comps in sc.comps.items():
        pk = None
        spr = None
        for t, v in comps:
            if t == 'MonoBehaviour' and scripts.get((v.get('m_Script') or {}).get('guid'), '') == 'CollectableItemPickup':
                pk = v
            if t == 'SpriteRenderer' and (v.get('m_Sprite') or {}).get('guid'):
                spr = (v['m_Sprite']['guid'], v['m_Sprite'].get('fileID'))
        if not pk or not sc.active(gid):
            continue
        g = (pk.get('item') or {}).get('guid')
        ipath = gm.get(g, '')
        if not ipath:
            continue
        item = os.path.basename(ipath).rsplit('.', 1)[0]
        if ipath not in kinds:
            a = open(ipath, encoding='utf-8', errors='replace').read()
            m = re.search(r'm_Script: \{fileID: \d+, guid: (\w+)', a)
            kinds[ipath] = scripts.get(m.group(1), '?') if m else '?'
        # its picture: its own sprite, else one of its children's
        if not spr:
            for cg, cc in sc.comps.items():
                if br.parent_gid(sc, cg) == gid:
                    for t, v in cc:
                        if t == 'SpriteRenderer' and (v.get('m_Sprite') or {}).get('guid'):
                            spr = (v['m_Sprite']['guid'], v['m_Sprite'].get('fileID'))
        m = sc.world(gid)
        out.append(dict(x=round(float(m[0, 3]), 3), y=round(float(m[1, 3]), 3), kind=kinds[ipath], item=item,
                        pd=str(pk.get('playerDataBool') or '') or '-', sprite=spr))
    return room, out, {}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('romfs')
    ap.add_argument('--jobs', type=int, default=6)
    ap.add_argument('--reuse', action='store_true', help='the scenes as last read (work/pickups.json)')
    a = ap.parse_args()
    paths = sorted(glob.glob(os.path.join(EXS, '*', '*.unity')))
    cache = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'work', 'pickups.json')
    if a.reuse and os.path.exists(cache):
        rows = [(r, i, dict(p, sprite=tuple(p['sprite']) if p['sprite'] else None)) for r, i, p in json.load(open(cache))]
    else:
        rows = []
        with ProcessPoolExecutor(max_workers=a.jobs) as ex:
            for room, out, _ in ex.map(one_scene, [(p, a.romfs) for p in paths]):
                for i, p in enumerate(out):
                    rows.append((room, i, p))
        json.dump(rows, open(cache, 'w'))
    # each item's display name, icon and PlayerData link (its asset)
    from sscommon import guidmap, yaml_file
    gm = guidmap()
    by_name = {os.path.basename(p).rsplit('.', 1)[0]: p for p in gm.values() if p.endswith('.asset')}
    info = {}
    for _, _, p in rows:
        it = p['item']
        if it in info:
            continue
        d = {}
        ap = by_name.get(it)
        if ap:
            for t, v in yaml_file(ap).values():
                if t == 'MonoBehaviour':
                    d = v
                    break
        dn = d.get('displayName') or {}
        ic = d.get('icon') or {}
        link = ('b:' + d['linkedPDBool']) if d.get('linkedPDBool') else ('i:' + d['linkedPDInt']) if d.get('linkedPDInt') else '-'
        info[it] = dict(sheet=dn.get('Sheet') or '-', key=dn.get('Key') or '-', link=link,
                        icon=(ic.get('guid'), ic.get('fileID')) if ic.get('guid') else None)
    # the pictures, one clip a sprite
    from build_room import sprite_image
    from build_sprites import add_image, write_set
    import ctrtex
    clips, frames, images, seen = {}, [], [], {}
    clip_of = {}
    for s in [p['sprite'] for _, _, p in rows] + [v['icon'] for v in info.values()]:
        if not s or s in clip_of:
            continue
        got = sprite_image(*s)
        if not got:
            clip_of[s] = '-'
            continue
        img, quad = got[0], got[1]
        h, w = img.shape[:2]
        img = np.asarray(Image.fromarray(img).resize((max(1, round(w * SCALE)), max(1, round(h * SCALE))), Image.LANCZOS))
        name = f'p{len(clips)}'
        clips[name] = dict(fps=12, loop_start=0, wrap=6, ids=[add_image(img, quad, frames, images, seen)])
        clip_of[s] = name
    if clips:
        write_set(os.path.join(a.romfs, 'sprites', 'pickups.sss'), clips, frames, images, ctrtex.FORMATS['rgba4'])
    sp = lambda t: str(t).replace(' ', '|') if t else '-'
    lines = [f"{room} {i} {p['x']} {p['y']} {p['kind']} {sp(p['item'])} {sp(p['pd'])} "
             f"{clip_of.get(p['sprite'], '-') if p['sprite'] else '-'} "
             f"{clip_of.get(info[p['item']]['icon'], '-') if info[p['item']]['icon'] else '-'} "
             f"{sp(info[p['item']]['sheet'])} {sp(info[p['item']]['key'])} {sp(info[p['item']]['link'])}"
             for room, i, p in rows]
    os.makedirs(os.path.join(a.romfs, 'text'), exist_ok=True)
    open(os.path.join(a.romfs, 'text', 'pickups.txt'), 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
    print(f'{len(rows)} pickups in {len(set(r for r, _, _ in rows))} rooms, {len(clips)} pictures')
    print(collections.Counter(p['kind'] for _, _, p in rows).most_common())
    print(collections.Counter(p['item'] for _, _, p in rows).most_common(40))


if __name__ == '__main__':
    main()
