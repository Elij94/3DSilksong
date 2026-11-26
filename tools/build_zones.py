#!/usr/bin/env python3
"""Each room's map zone (its SceneManager's mapZone, an enum of GlobalEnums.MapZone) -> <out>/text/zones.txt,
Scene<TAB>ZONE (the key of the "Map Zones" text sheet: the area's name). The save slots, the area titles and the map
use it.

  python tools/build_zones.py <romfs dir>
"""
import os, re, sys, glob
from sscommon import ROOT, EX


def zone_names():
    src = open(os.path.join(ROOT, 'Assembly-CSharp', 'GlobalEnums', 'MapZone.cs'), encoding='utf-8').read()
    return {int(v): k for k, v in re.findall(r'^\s*([A-Z_0-9]+)\s*=\s*(\d+)', src, re.M)}


def main():
    out = sys.argv[1]
    names = zone_names()
    rooms = sorted(f[:-4] for f in os.listdir(os.path.join(out, 'rooms')) if f.endswith('.ssr'))
    scenes = {os.path.basename(p)[:-6]: p for p in glob.glob(os.path.join(EX, 'Scenes', '**', '*.unity'), recursive=True)}
    lines = []
    for r in rooms:
        p = scenes.get(r)
        if not p:
            continue
        z = None
        with open(p, encoding='utf-8', errors='replace') as fh:
            for line in fh:
                m = re.match(r'\s*mapZone: (\d+)', line)
                if m:
                    z = names.get(int(m.group(1)))
                    break
        if z:
            lines.append(f'{r}\t{z}')
    os.makedirs(os.path.join(out, 'text'), exist_ok=True)
    open(os.path.join(out, 'text', 'zones.txt'), 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
    print(f'{len(lines)} rooms with a map zone')
    save_art(out, names)


def save_art(out, names):
    """the save slots' pictures (SaveSlotBackgrounds in Menu_Title: one sprite per MapZone, an Act 3 one) ->
    text/save_art.txt, ZONE<TAB>sprite<TAB>act 3 sprite (or -): clips of ui/area_art.sss (tools/build_ui.py)"""
    from build_room import Scene, find_scene
    from sscommon import guidmap
    sc = Scene(find_scene('Menu_Title'))
    rows = []
    for gid, comps in sc.comps.items():
        for t, v in comps:
            if t == 'MonoBehaviour' and 'areaBackgrounds' in v:
                def nm(ref):
                    p = guidmap().get((ref or {}).get('guid'))
                    return os.path.splitext(os.path.basename(p))[0] if p else '-'
                for i, b in enumerate(v['areaBackgrounds'] or []):
                    if i in names and (b or {}).get('BackgroundImage', {}).get('guid'):
                        rows.append(f"{names[i]}\t{nm(b.get('BackgroundImage'))}\t{nm(b.get('Act3BackgroundImage'))}")
    open(os.path.join(out, 'text', 'save_art.txt'), 'w', encoding='utf-8', newline='\n').write('\n'.join(rows) + '\n')
    print(f'{len(rows)} save slot pictures')


if __name__ == '__main__':
    main()
