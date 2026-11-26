#!/usr/bin/env python3
"""The prefabs the rooms' scripts spawn (SpawnObjectFromGlobalPool, CreateObject, FlingObjectsFromGlobalPool...):
projectiles, effects, summoned enemies. Each becomes a pack the engine can put in a room while it runs:
rooms/pf_<name>.sso (its objects), rooms/pf_<name>.ssf (its hierarchy and FSMs, by tools/build_fsm.py),
fx/pf_<name>.spt (its particles). Prefabs those spawn in turn are followed. Writes work/prefabs.txt (guid, name).

  python tools/build_prefabs.py <romfs dir> [--jobs 6]
Run tools/build_fsm.py and tools/build_sprites.py --objects after it."""
import os, sys, json, glob, re, argparse
from concurrent.futures import ProcessPoolExecutor
from sscommon import guidmap, WORK

SPAWNERS = {'SpawnObjectFromGlobalPool', 'SpawnObjectFromGlobalPoolDelay', 'CreateObject', 'FlingObjectsFromGlobalPool',
            'FlingObjectsFromGlobalPoolV2', 'FlingObjectsFromGlobalPoolV3', 'SpawnObjectFromGlobalPoolOverTime',
            'SpawnObjectFromGlobalPoolOverTimeV2', 'FlingObjectsFromGlobalPoolTime', 'FlingObjectsFromGlobalPoolVelTime'}


def pack_name(path):
    return 'pf_' + re.sub(r'[^a-z0-9_]+', '_', os.path.basename(path)[:-7].lower()).strip('_')


def spawned(fsms):
    out = set()
    for f in fsms:
        for s in f.get('states', []):
            for a in s.get('actions', []):
                if a.get('type') in SPAWNERS:
                    g = (a.get('params') or {}).get('gameObject')
                    if isinstance(g, dict) and g.get('guid'):
                        out.add(g['guid'])
    return out


def _one(job):
    out, name, path = job
    import build_room
    from argparse import Namespace
    a = Namespace(no_art=True, no_objects=False, cpu=4, preview=None, ppu=28, aspect=0.5, fmt='rgba4', scenes=[name])
    scripts, sorting = build_room.script_names(), build_room.sorting_layers()
    try:
        build_room.build(name, out, a, scripts, sorting, path=path, prefab=True)
    except Exception as ex:   # noqa: BLE001
        return f'{name}: {type(ex).__name__}: {ex}'
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--jobs', type=int, default=6)
    a = ap.parse_args()
    gm = guidmap()
    todo = set()
    for p in glob.glob(os.path.join(a.out, 'fsm', '*.json')):
        if not os.path.basename(p).startswith('pf_'):
            todo |= spawned(json.load(open(p, encoding='utf-8')))
    done, names = set(), {}
    while todo:
        jobs = []
        for g in sorted(todo - done):
            done.add(g)
            path = gm.get(g)
            if not path or not path.endswith('.prefab') or not os.path.exists(path):
                continue
            names[g] = pack_name(path)
            jobs.append((a.out, names[g], path))
        todo = set()
        with ProcessPoolExecutor(max_workers=max(1, a.jobs)) as ex:
            for e in ex.map(_one, jobs):
                if e:
                    print('failed:', e)
        # what the new packs spawn themselves
        for _, name, _ in jobs:
            p = os.path.join(a.out, 'fsm', name + '.json')
            if os.path.exists(p):
                todo |= spawned(json.load(open(p, encoding='utf-8'))) - done
    with open(os.path.join(WORK, 'prefabs.txt'), 'w', encoding='utf-8') as fh:
        for g, n in sorted(names.items(), key=lambda x: x[1]):
            fh.write(f'{g}\t{n}\n')
    print(f'{len(names)} prefabs')


if __name__ == '__main__':
    main()
