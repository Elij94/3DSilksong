# Shared helpers for the 3DSilksong tools: where things are, and reading the AssetRipper export (Unity YAML).
# Adapted from HK3DS's hkcommon.py (MIT). Paths come from the environment:
#   SILKSONG  the game's install folder (the one holding "Hollow Knight Silksong_Data", directly or under Content/)
#   SSDATA    the exports and indexes (default: ../ssdata next to the repository, else ./ssdata)
import os, re, glob, pickle, functools
TOP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.path.join(TOP, 'work')


def _default_ssdata():
    side = os.path.join(os.path.dirname(TOP), 'ssdata')
    return side if os.path.isdir(side) else os.path.join(TOP, 'ssdata')


ROOT = os.environ.get('SSDATA') or _default_ssdata()
EX = f'{ROOT}/ex'          # "Export Unity Project": ExportedProject/Assets (YAML scenes, prefabs, .meta guids)
PRIMARY = f'{ROOT}/pc'     # "Export Primary Content": Texture2D, Sprite, AudioClip... as plain files
SCRIPTS = f'{ROOT}/Assembly-CSharp'   # the decompiled game scripts (reading only: what a component does)


def game_data(install=None):
    """The game's _Data folder from its install folder (Steam: <install>/Hollow Knight Silksong_Data,
    Game Pass: <install>/Content/Hollow Knight Silksong_Data)."""
    install = install or os.environ.get('SILKSONG', '')
    for d in (install, os.path.join(install, 'Content')):
        for name in glob.glob(os.path.join(d, '*_Data')):
            if os.path.exists(os.path.join(name, 'globalgamemanagers')):
                return name
    raise SystemExit(f'no Silksong _Data folder under {install!r} (set SILKSONG to the game folder)')


def bundle_dir(data):
    return os.path.join(data, 'StreamingAssets', 'aa', 'StandaloneWindows64')


def load_yaml_docs(path):
    """A Unity YAML file -> {fileID: (type name, dict)}."""
    import yaml
    loader = getattr(yaml, 'CSafeLoader', yaml.SafeLoader)
    src = open(path, encoding='utf-8').read()
    parts = re.split(r'^--- !u!(\d+) &(-?\d+).*$', src, flags=re.M)
    objs = {}
    for i in range(1, len(parts), 3):
        try:
            d = yaml.load(parts[i + 2], Loader=loader)
        except Exception:
            continue
        if d:
            (typ, val), = d.items()
            objs[int(parts[i + 1])] = (typ, val)
    return objs


@functools.lru_cache(None)
def metamap(folder):
    """guid -> file, for one asset folder of the export (cached in SSDATA/meta_<folder>.pkl)."""
    cache = f'{ROOT}/meta_{folder}.pkl'
    if os.path.exists(cache):
        return pickle.load(open(cache, 'rb'))
    g = {}
    for m in glob.glob(f'{EX}/{folder}/**/*.meta', recursive=True):
        r = re.search(r'guid: ([0-9a-f]+)', open(m, errors='replace').read(2000))
        if r:
            g[r.group(1)] = m[:-5]
    pickle.dump(g, open(cache, 'wb'))
    return g


@functools.lru_cache(None)
def guidmap():
    """guid -> file, for every asset of the export (cached in SSDATA/guidmap.pkl; delete it after a new export)."""
    cache = f'{ROOT}/guidmap.pkl'
    if os.path.exists(cache):
        return pickle.load(open(cache, 'rb'))
    g = {}
    for dp, _, fs in (x for d in (EX, SCRIPTS) for x in os.walk(d)):
        for fn in fs:
            if fn.endswith('.meta'):
                p = os.path.join(dp, fn)
                with open(p, errors='replace') as fh:
                    r = re.search(r'guid: ([0-9a-f]+)', fh.read(600))
                if r:
                    g[r.group(1)] = p[:-5]
    pickle.dump(g, open(cache, 'wb'))
    return g


@functools.lru_cache(None)
def yaml_file(path):
    """load_yaml_docs, cached (prefabs and assets read many times)."""
    return load_yaml_docs(path)


def mono_fields(path):
    """The first MonoBehaviour of a .asset / .prefab file, as a dict (or {})."""
    for t, v in yaml_file(path).values():
        if t == 'MonoBehaviour':
            return v
    return {}


def f(v):
    try:
        return float(v)
    except (TypeError, ValueError):
        return 0.0
