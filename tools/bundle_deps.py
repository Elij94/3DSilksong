#!/usr/bin/env python3
"""Which Addressables bundles a scene needs. Each bundle holds serialized files named CAB-<hash>, and each serialized
file lists the CAB files it points into; following those links from a scene's bundle gives every bundle it needs.
  python tools/bundle_deps.py Tut_01 [Tut_02 ...]          print the bundles (and their size)
  python tools/bundle_deps.py --depth 1 Tut_01             only what the scene points to directly
  ... --extra herocollections_ --extra hero               add bundles by name (Hornet's), with what they point to
  python tools/bundle_deps.py --subset DIR Tut_01 ...      also build DIR: a copy of the game's _Data folder holding
                                                           only those bundles (for a small AssetRipper test export)
The CAB map of every bundle is cached in work/cab_map.json (built once, a few minutes). Needs UnityPy."""
import os, sys, json, shutil, argparse
from concurrent.futures import ProcessPoolExecutor
from sscommon import game_data, bundle_dir, WORK

CACHE = os.path.join(WORK, 'cab_map.json')


def scan(path):
    """bundle -> ([its CAB names], [CAB names it points to])"""
    import UnityPy
    # the bundles' headers carry no engine version (stripped to 0.0.0): give UnityPy the game's
    UnityPy.config.FALLBACK_UNITY_VERSION = os.environ.get('SS_UNITY', '6000.0.50f1')
    own, ext = [], set()
    try:
        env = UnityPy.load(path)
        for bf in env.files.values():
            for name, sf in getattr(bf, 'files', {}).items():
                if not name.upper().startswith('CAB-') or name.endswith(('.resS', '.resource')):
                    continue
                own.append(name.lower())
                for e in getattr(sf, 'externals', []):
                    p = (getattr(e, 'path', '') or '').lower()
                    if 'cab-' in p:
                        ext.add(p.rsplit('/', 1)[-1])
    except Exception as e:
        return path, [], [], str(e)
    return path, own, sorted(ext), None


def cab_map(bdir):
    if os.path.exists(CACHE):
        return json.load(open(CACHE))
    paths = [os.path.join(dp, f) for dp, _, fs in os.walk(bdir) for f in fs if f.endswith('.bundle')]
    out, errors = {}, 0
    with ProcessPoolExecutor(max_workers=min(8, os.cpu_count() or 4)) as ex:
        for i, (p, own, ext, err) in enumerate(ex.map(scan, paths, chunksize=4)):
            errors += bool(err)
            out[os.path.relpath(p, bdir).replace('\\', '/')] = {'own': own, 'ext': ext}
            if i % 200 == 0:
                print(f'  {i}/{len(paths)} bundles read', file=sys.stderr)
    os.makedirs(WORK, exist_ok=True)
    json.dump(out, open(CACHE, 'w'))
    print(f'{len(out)} bundles mapped, {errors} unreadable', file=sys.stderr)
    return out


def closure(cmap, scenes, depth=None):
    owner = {cab: b for b, v in cmap.items() for cab in v['own']}
    todo = [b for b in cmap if b.startswith('scenes_scenes_scenes/') and
            os.path.basename(b)[:-7].lower() in {s.lower() for s in scenes}]
    if len(todo) != len(scenes):
        raise SystemExit(f'scene bundle not found for some of {scenes}')
    seen, level = set(), 0
    while todo and (depth is None or level <= depth):
        nxt = []
        for b in todo:
            if b not in seen:
                seen.add(b)
                nxt += [owner[c] for c in cmap[b]['ext'] if c in owner]
        todo, level = nxt, level + 1
    return sorted(seen)


def make_subset(data, bundles, dst):
    """DIR/<name>_Data: the game's own files (copied) and StreamingAssets/aa with only the given bundles."""
    out = os.path.join(dst, os.path.basename(data))
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(data):
        s, d = os.path.join(data, f), os.path.join(out, f)
        if f == 'StreamingAssets':
            continue
        if os.path.isdir(s):
            shutil.copytree(s, d, dirs_exist_ok=True)
        else:
            shutil.copy2(s, d)
    sa, osa = os.path.join(data, 'StreamingAssets'), os.path.join(out, 'StreamingAssets')
    for dp, _, fs in os.walk(sa):
        rel = os.path.relpath(dp, sa)
        if rel.replace('\\', '/').startswith('aa/StandaloneWindows64'):
            continue
        os.makedirs(os.path.join(osa, rel), exist_ok=True)
        for f in fs:
            shutil.copy2(os.path.join(dp, f), os.path.join(osa, rel, f))
    bdir, obdir = bundle_dir(data), bundle_dir(out)
    for b in bundles:
        os.makedirs(os.path.dirname(os.path.join(obdir, b)), exist_ok=True)
        shutil.copy2(os.path.join(bdir, b), os.path.join(obdir, b))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('scenes', nargs='+')
    ap.add_argument('--subset')
    ap.add_argument('--extra', action='append', default=[], help='also take the bundles whose path starts with this '
                    '(e.g. herocollections_ for Hornet), with their direct dependencies')
    ap.add_argument('--depth', type=int, help='follow links only this many bundles deep (1: what the scene points '
                    'to directly; default: all, which for most scenes is a large part of the game)')
    a = ap.parse_args()
    data = game_data()
    bdir = bundle_dir(data)
    cmap = cab_map(bdir)
    need = closure(cmap, a.scenes, a.depth)
    roots = [b for b in cmap for x in a.extra if b.startswith(x)]
    if roots:
        owner = {cab: b for b, v in cmap.items() for cab in v['own']}
        need = sorted(set(need) | set(roots) | {owner[c] for r in roots for c in cmap[r]['ext'] if c in owner})
    total = sum(os.path.getsize(os.path.join(bdir, b)) for b in need)
    for b in need:
        print(f'{os.path.getsize(os.path.join(bdir, b)) / 1048576:8.1f} Mo  {b}')
    print(f'{len(need)} bundles, {total / 1048576:,.0f} Mo')
    if a.subset:
        print('subset:', make_subset(data, need, a.subset))


if __name__ == '__main__':
    main()
