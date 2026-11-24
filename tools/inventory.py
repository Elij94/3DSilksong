#!/usr/bin/env python3
"""First look at an installed copy of Silksong, straight from its files (no export needed, a few seconds):
the Unity version, the scripting backend, the Addressables bundles by kind and size, and the scenes by zone.
Writes tools/data/all_rooms.txt (every scene name, one per line: the room list the converters walk) and
work/inventory.json.
   python tools/inventory.py "C:/XboxGames/Hollow Knight- Silksong"      (or set SILKSONG)"""
import os, re, sys, json, collections
from sscommon import game_data, bundle_dir, TOP, WORK

# The zone prefixes, as the scene names give them; "Bone_East_03" -> "Bone_East". Longest first.
MULTI = ('Bone_East', 'Dust_Maze', 'Song_Tower', 'Belltown_Room', 'Cradle_Destroyed', 'Memory_Silk', 'Cinematic_Ending',
         'Slab_Cell', 'Bellway_Centipede', 'Room_Pinstress')


def zone_of(scene):
    for p in sorted(MULTI, key=len, reverse=True):
        if scene.lower().startswith(p.lower()):
            return p
    return re.split(r'[_\d]', scene, maxsplit=1)[0] or scene


def main(install):
    data = game_data(install)
    bdir = bundle_dir(data)
    gg = open(os.path.join(data, 'globalgamemanagers'), 'rb').read(400000)
    m = re.search(rb'(?:20\d\d|6\d{3})\.\d+\.\d+[abfp]\d+', gg)
    unity = m.group().decode() if m else '?'
    mono = os.path.exists(os.path.join(data, 'Managed', 'Assembly-CSharp.dll'))

    cat = open(os.path.join(data, 'StreamingAssets', 'aa', 'catalog.bin'), 'rb').read()
    scenes = sorted(set(s.decode()[:-6] for s in re.findall(rb'[A-Za-z0-9_\-]{2,}\.unity', cat)))

    kinds = collections.defaultdict(lambda: [0, 0])
    sizes = {}
    for dp, _, fs in os.walk(bdir):
        for fn in fs:
            if not fn.endswith('.bundle'):
                continue
            p = os.path.join(dp, fn)
            rel = os.path.relpath(p, bdir).replace('\\', '/')
            kind = rel.split('/')[0] if '/' in rel else rel.split('_')[0]
            n = os.path.getsize(p)
            kinds[kind][0] += 1
            kinds[kind][1] += n
            sizes[rel] = n

    scene_bytes = {}
    for rel, n in sizes.items():
        if rel.startswith('scenes_scenes_scenes/'):
            scene_bytes[os.path.basename(rel)[:-7].lower()] = n
    zones = collections.defaultdict(list)
    for s in scenes:
        zones[zone_of(s)].append(s)

    os.makedirs(os.path.join(TOP, 'tools', 'data'), exist_ok=True)
    with open(os.path.join(TOP, 'tools', 'data', 'all_rooms.txt'), 'w', newline='\n') as fh:
        fh.write('\n'.join(scenes) + '\n')
    os.makedirs(WORK, exist_ok=True)
    inv = {'unity': unity, 'backend': 'Mono' if mono else 'IL2CPP', 'data': data,
           'bundles': {k: {'count': c, 'bytes': b} for k, (c, b) in sorted(kinds.items(), key=lambda kv: -kv[1][1])},
           'zones': {z: {'rooms': v, 'bytes': sum(scene_bytes.get(s.lower(), 0) for s in v)}
                     for z, v in sorted(zones.items(), key=lambda kv: -len(kv[1]))},
           'managed': sorted(os.listdir(os.path.join(data, 'Managed'))) if mono else []}
    json.dump(inv, open(os.path.join(WORK, 'inventory.json'), 'w'), indent=1)

    mb = lambda n: f'{n / 1048576:,.0f} Mo'
    print(f'Unity {unity}, {inv["backend"]}, {len(sizes)} bundles ({mb(sum(sizes.values()))}), {len(scenes)} scenes')
    print('\nBundles by kind (top 15):')
    for k, v in list(inv['bundles'].items())[:15]:
        print(f'  {k:28s} {v["count"]:5d}  {mb(v["bytes"]):>10s}')
    print(f'\n{len(zones)} zones:')
    for z, v in inv['zones'].items():
        print(f'  {z:22s} {len(v["rooms"]):3d} rooms  {mb(v["bytes"]):>8s}')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else None)
