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


if __name__ == '__main__':
    main()
