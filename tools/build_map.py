#!/usr/bin/env python3
"""The game's map (Prefabs/UI/Map/Game_Map_Hornet.prefab) for the bottom screen:

- ui/map_<zone>.sss (one per area, so only the shown one is in memory): every map piece (GameMapScene) at 48 pixels
  a unit, its colour baked in: clip "<i>i" its sprite as
  placed in the prefab (rough, or the full one for the others), "<i>f" its full sprite (Rough pieces that have one).
- text/map.txt: "Z <zone> <ZONE_NAME> <PlayerData bool or ->" per zone (GameMap.mapZoneInfo, indexed by MapZone),
  then "P <zone> <scene> <state> <x> <y> <sx> <sy> <scenes>" per piece, in clip order: the scene it belongs to (its
  mappedParent's, else its own name), its initial state (0 hidden, 1 rough, 2 full), where it is on the map, the
  scenes that map it once all mapped (mappedIfAllMapped, comma-separated, or -).

  python tools/build_map.py <romfs dir>"""
import os, re, sys
import numpy as np
from PIL import Image
from sscommon import ROOT, EX
from build_room import Scene, sprite_image, f
from build_sprites import add_image, write_set
import ctrtex

PPU = 48


def zone_names():
    src = open(os.path.join(ROOT, 'Assembly-CSharp', 'GlobalEnums', 'MapZone.cs'), encoding='utf-8').read()
    return {int(v): k for k, v in re.findall(r'^\s*([A-Z_0-9]+)\s*=\s*(\d+)', src, re.M)}


def main():
    out = sys.argv[1]
    sc = Scene(os.path.join(EX, 'Prefabs', 'UI', 'Map', 'Game_Map_Hornet.prefab'))
    names = zone_names()
    kids = {}
    for gid in sc.go:
        t = sc.tr.get(gid)
        father = sc.tr_id.get(((t or {}).get('m_Father') or {}).get('fileID')) if t else None
        if father:
            kids.setdefault(father['m_GameObject']['fileID'], []).append(gid)
    zones = None
    for gid, comps in sc.comps.items():
        for t, v in comps:
            if t == 'MonoBehaviour' and 'mapZoneInfo' in v:
                zones = v['mapZoneInfo']
    if zones is None:
        raise SystemExit('no GameMap in the map prefab')
    lines = []
    per = {}   # zone -> [clips, frames, images, seen, cache]

    def image(ref, color, zi):
        clips, frames, images, seen, cache = per.setdefault(zi, [{}, [], [], {}, {}])
        g = (ref or {}).get('guid')
        if not g:
            return -1
        key = (g, tuple(round(c, 3) for c in color))
        if key in cache:
            return cache[key]
        got = sprite_image(g, (ref or {}).get('fileID'))
        if not got:
            cache[key] = -1
            return -1
        img, q = got
        w, h = img.shape[1], img.shape[0]
        k = PPU / (w / max(1e-6, q[2] - q[0]))
        nw, nh = max(1, round(w * k)), max(1, round(h * k))
        img = np.asarray(Image.fromarray(img).resize((nw, nh), Image.LANCZOS)).astype(np.float32)
        img *= np.array(color, np.float32)              # the SpriteRenderer's colour (Unity multiplies)
        img = np.clip(img, 0, 255).astype(np.uint8)
        cache[key] = add_image(img, q, frames, images, seen)
        return cache[key]

    n = 0
    for zi, z in enumerate(zones):
        for pinfo in (z.get('Parents') or []):
            pid = (pinfo.get('Parent') or {}).get('fileID')
            if pid not in sc.go:
                continue
            lines.append(f"Z {zi} {names.get(zi, 'NONE')} {pinfo.get('PlayerDataBool') or '-'}")
            todo = list(kids.get(pid, []))
            while todo:
                gid = todo.pop(0)
                todo += kids.get(gid, [])
                comps = sc.comps.get(gid, [])
                gms = next((v for t, v in comps if t == 'MonoBehaviour' and 'initialState' in v and 'fullSprite' in v), None)
                sr = next((v for t, v in comps if t == 'SpriteRenderer'), None)
                if gms is None or sr is None or not sc.active(gid):
                    continue
                mp = (gms.get('mappedParent') or {}).get('fileID')
                owner = gid
                if mp:   # a reference to the GameMapScene component: its game object
                    comp = sc.objs.get(mp)
                    owner = comp[1]['m_GameObject']['fileID'] if comp else gid
                scene = str(sc.go.get(owner, {}).get('m_Name') or sc.go[gid].get('m_Name'))
                c = sr.get('m_Color') or {}
                color = [f(c.get(k, 1)) for k in 'rgb'] + [1.0]
                i_init = image(sr.get('m_Sprite'), color, zi)
                i_full = image(gms.get('fullSprite'), color, zi) if int(f(gms.get('initialState'))) == 1 else -1
                clips = per[zi][0]
                if i_init < 0 and i_full < 0:
                    continue
                m = sc.world(gid)
                sx = float(np.sign(m[0, 0]) * np.hypot(m[0, 0], m[1, 0]))
                sy = float(np.hypot(m[0, 1], m[1, 1]))
                clips[f'{n}i'] = dict(fps=12, loop_start=0, wrap=6, ids=[i_init if i_init >= 0 else i_full])
                if i_full >= 0:
                    clips[f'{n}f'] = dict(fps=12, loop_start=0, wrap=6, ids=[i_full])
                # mappedIfAllMapped: mapped once all these are (their scenes)
                also = []
                for r in gms.get('mappedIfAllMapped') or []:
                    comp = sc.objs.get((r or {}).get('fileID'))
                    if comp:
                        also.append(str(sc.go.get(comp[1]['m_GameObject']['fileID'], {}).get('m_Name')))
                lines.append(f"P {zi} {scene} {int(f(gms.get('initialState')))} {m[0, 3]:.3f} {m[1, 3]:.3f} "
                             f"{sx:.3f} {sy:.3f} {','.join(also) or '-'}")
                n += 1
    os.makedirs(os.path.join(out, 'ui'), exist_ok=True)
    for zi, (clips, frames, images, _, _) in per.items():
        if frames:
            write_set(os.path.join(out, 'ui', f'map_{zi}.sss'), clips, frames, images, ctrtex.FORMATS['rgba4'])
    os.makedirs(os.path.join(out, 'text'), exist_ok=True)
    open(os.path.join(out, 'text', 'map.txt'), 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
    print(f'{n} map pieces, {sum(1 for l in lines if l[0] == "Z")} zone parents')


if __name__ == '__main__':
    main()
