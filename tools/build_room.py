#!/usr/bin/env python3
"""Convert rooms (scenes) of the AssetRipper export into 3DS room files (format: docs/FORMATS.md):
  <out>/rooms/<Scene>.ssr   collision grid, gates (TransitionPoint), triggers (hazards, camera locks, respawn points),
                            and the table of the scenery layers
  <out>/rooms/<Scene>.ssl   the scenery layers' tiles, GPU-ready and zlib-compressed
  <out>/rooms/<Scene>.sso   the room's live objects: enemies, breakables, NPCs, interactables, animated objects
  <out>/rooms/<Scene>.spt   the room's particle emitters (textures: tools/build_particles.py)
  <out>/fsm/<Scene>.json    the room's PlayMaker FSMs (states, transitions, decoded actions; templates resolved)
  <out>/fsm/templates/<guid>.json   the FSM templates that RunFSM actions start
  work/rooms/<Scene>.json, <Scene>.objs.json   readable copies (for checking and other tools; not in the RomFS)

  python tools/build_room.py <out_dir> Tut_01 [Tut_02 ...]      (or --list tools/data/all_rooms.txt)
Options:
  --ppu N        scenery pixels per world unit across (default 28: the 800 wide top screen shows 27.6)
  --aspect A     pixels a unit down / across (default 0.5: the top screen has 240 lines, half as many a unit)
  --cpu N        collision cells per world unit (default 4)
  --fmt F        scenery texture format, rgba8 or rgba4 (default rgba4: half the size)
  --no-art       no scenery (fast)
  --no-objects   no objects, particles or FSMs
  --preview PNG  also draw the room's geometry (solid cells, gates, triggers, layer tiles) to a PNG, for checking

Scenery: every enabled SpriteRenderer and tk2dSprite of an active object is drawn, in Unity's order (sorting layer,
order in layer, then far to near), into layers by depth: near the gameplay plane (z ~ 0), in front (z < 0) and four
background depths. Each layer is drawn flat at its median depth; the game scrolls it with the parallax of that depth
(its "parallax" field: d / (d + z) for a camera d units in front of the gameplay plane). Additive and other special
materials are drawn as normal alpha for now."""
import os, sys, re, json, math, struct, zlib, argparse, collections
import numpy as np
from PIL import Image, ImageDraw
from sscommon import ROOT, EX, WORK, guidmap, yaml_file, load_yaml_docs, f
import tk2d, ctrtex, objects, fsm_extract, particles

Image.MAX_IMAGE_PIXELS = None          # our own canvases can be big; their size is capped below (MAX_PIXELS)
TERRAIN_LAYER = 8
CAM_Z = 38.1
VIEW = (16.0, 9.0)                      # half the camera's view, in units (with some margin)
MAX_PIXELS = 60_000_000                 # a layer bigger than this is drawn at a lower resolution
TILE = 256
# layer depth ranges (z): front, gameplay plane, backgrounds
LAYERS = [('front', -1e9, -0.5), ('main', -0.5, 0.5), ('bg1', 0.5, 4), ('bg2', 4, 12), ('bg3', 12, 35), ('bg4', 35, 1e9)]


# ---------------------------------------------------------------- scene graph
def quat_mat(q):
    x, y, z, w = (f(q.get(k)) for k in 'xyzw')
    if x == y == z == 0 and w == 0:
        w = 1
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


class Scene:
    def __init__(self, path):
        self.objs = load_yaml_docs(path)
        self.go = {k: v for k, (t, v) in self.objs.items() if t == 'GameObject'}
        self.tr = {}                    # game object id -> transform dict
        self.tr_id = {}                 # transform id -> transform dict
        for k, (t, v) in self.objs.items():
            if t in ('Transform', 'RectTransform'):
                self.tr[v['m_GameObject']['fileID']] = v
                self.tr_id[k] = v
        self.comps = collections.defaultdict(list)   # game object id -> [(type, dict)]
        for k, (t, v) in self.objs.items():
            if t not in ('GameObject', 'Transform') and isinstance(v, dict) and 'm_GameObject' in v:
                self.comps[v['m_GameObject']['fileID']].append((t, v))
        self._world, self._active = {}, {}

    def world(self, gid):
        """4x4 world matrix of a game object"""
        if gid in self._world:
            return self._world[gid]
        t = self.tr.get(gid)
        if t is None:
            return np.eye(4)
        m = np.eye(4)
        m[:3, :3] = quat_mat(t.get('m_LocalRotation') or {}) * [f(t['m_LocalScale'].get(k)) for k in 'xyz']
        m[:3, 3] = [f(t['m_LocalPosition'].get(k)) for k in 'xyz']
        father = self.tr_id.get((t.get('m_Father') or {}).get('fileID'))
        if father is not None:
            m = self.world(father['m_GameObject']['fileID']) @ m
        self._world[gid] = m
        return m

    def active(self, gid):
        if gid in self._active:
            return self._active[gid]
        g = self.go.get(gid, {})
        ok = bool(g.get('m_IsActive', 1))
        t = self.tr.get(gid)
        father = self.tr_id.get((t or {}).get('m_Father', {}).get('fileID')) if t else None
        if ok and father is not None:
            ok = self.active(father['m_GameObject']['fileID'])
        self._active[gid] = ok
        return ok

    def name(self, gid):
        return self.go.get(gid, {}).get('m_Name') or ''


def script_names():
    """script guid -> class name (from the .cs files of the export; the DLL plugins resolve to their DLL name)"""
    out = {}
    for g, p in guidmap().items():
        if p.endswith('.cs'):
            out[g] = os.path.basename(p)[:-3]
        elif p.endswith('.dll'):
            out[g] = os.path.basename(p)[:-4]
    return out


def apply(m, pts):
    pts = np.asarray(pts, float).reshape(-1, 2)
    return pts @ m[:2, :2].T + m[:2, 3]


# ---------------------------------------------------------------- collisions, gates, triggers
def collider_shapes(sc, gid, t, v):
    """world-space outlines of a 2D collider: [('poly', pts) | ('line', pts)]"""
    m = sc.world(gid)
    off = np.array([f((v.get('m_Offset') or {}).get('x')), f((v.get('m_Offset') or {}).get('y'))])
    if t == 'BoxCollider2D':
        sx, sy = f(v['m_Size']['x']) / 2, f(v['m_Size']['y']) / 2
        return [('poly', apply(m, off + [[-sx, -sy], [sx, -sy], [sx, sy], [-sx, sy]]))]
    if t == 'PolygonCollider2D':
        paths = (v.get('m_Points') or {}).get('m_Paths') or []
        return [('poly', apply(m, off + np.array([[f(p['x']), f(p['y'])] for p in path])))
                for path in paths if len(path) >= 3]
    if t == 'EdgeCollider2D':
        pts = [[f(p['x']), f(p['y'])] for p in (v.get('m_Points') or [])]
        if len(pts) < 2:
            return []
        w = apply(m, off + np.array(pts))
        # a closed edge loop (the terrain "Chunk" objects) outlines solid ground: fill it
        return [('poly' if len(w) >= 4 and np.allclose(w[0], w[-1], atol=0.05) else 'line', w)]
    if t == 'CircleCollider2D':
        r = f(v.get('m_Radius'))
        a = np.linspace(0, 2 * math.pi, 16, endpoint=False)
        return [('poly', apply(m, off + np.c_[np.cos(a), np.sin(a)] * r))]
    return []


def rect_of(shapes):
    pts = np.concatenate([p for _, p in shapes]) if shapes else np.zeros((1, 2))
    (x0, y0), (x1, y1) = pts.min(0), pts.max(0)
    return [float(x0), float(y0), float(x1 - x0), float(y1 - y0)]


def scripted(sc, scripts):
    """game objects with an FSM or a Breakable: their walls can open (and their children's)"""
    out = set()
    for gid, comps in sc.comps.items():
        for t, v in comps:
            if t != 'MonoBehaviour':
                continue
            n = scripts.get((v.get('m_Script') or {}).get('guid'), '')
            if n == 'Breakable' or (n == 'PlayMaker' and isinstance(v.get('fsm'), dict)):
                out.add(gid)
    return out


def parent_gid(sc, gid):
    t = sc.tr.get(gid)
    father = sc.tr_id.get(((t or {}).get('m_Father') or {}).get('fileID')) if t else None
    return father['m_GameObject']['fileID'] if father else None


def gather(sc, scripts):
    solids, gates, triggers = [], [], []
    dyn_owner = scripted(sc, scripts)
    sc.dyn_solids = []      # walls that FSMs open, close or move: [gid, x0, y0, x1, y1], out of the grid
    for gid, comps in sc.comps.items():
        layer = sc.go.get(gid, {}).get('m_Layer')
        cols = [(t, v) for t, v in comps if t.endswith('Collider2D') and v.get('m_Enabled', 1)]
        if layer == TERRAIN_LAYER and cols:
            dyn = not sc.active(gid) or gid in dyn_owner or parent_gid(sc, gid) in dyn_owner
            if dyn:
                for t, v in cols:
                    if not v.get('m_IsTrigger'):
                        for k, p in collider_shapes(sc, gid, t, v):
                            sc.dyn_solids.append([gid, float(p[:, 0].min()), float(p[:, 1].min()),
                                                  float(p[:, 0].max()), float(p[:, 1].max())])
        if not sc.active(gid):
            continue
        monos = {scripts.get((v.get('m_Script') or {}).get('guid'), ''): v for t, v in comps if t == 'MonoBehaviour'}
        trig = [s for t, v in cols if v.get('m_IsTrigger') for s in collider_shapes(sc, gid, t, v)]
        if layer == TERRAIN_LAYER and not (gid in dyn_owner or parent_gid(sc, gid) in dyn_owner):
            for t, v in cols:
                if not v.get('m_IsTrigger'):
                    cid = len(solids)
                    solids += [(k, p, cid) for k, p in collider_shapes(sc, gid, t, v)]
        if 'TransitionPoint' in monos:
            mv = monos['TransitionPoint']
            eo = mv.get('entryOffset') or {}
            gates.append(dict(name=sc.name(gid), target=mv.get('targetScene') or '', entry=mv.get('entryPoint') or '',
                              rect=rect_of(trig) if trig else list(sc.world(gid)[:2, 3]) + [0, 0],
                              offset=[f(eo.get('x')), f(eo.get('y'))],
                              enter_left=int(mv.get('alwaysEnterLeft') or 0), enter_right=int(mv.get('alwaysEnterRight') or 0)))
        if 'HazardRespawnMarker' in monos:
            p = sc.world(gid)[:2, 3]
            triggers.append(dict(kind='respawn', name=sc.name(gid), rect=[float(p[0]), float(p[1]), 0, 0]))
        if 'DamageHero' in monos and trig:
            mv = monos['DamageHero']
            triggers.append(dict(kind='hazard', name=sc.name(gid), rect=rect_of(trig),
                                 damage=int(mv.get('damageDealt') or 1), hazard=int(mv.get('hazardType') or 0)))
        if 'CameraLockArea' in monos and trig:
            mv = monos['CameraLockArea']
            triggers.append(dict(kind='camlock', name=sc.name(gid), rect=rect_of(trig),
                                 cam=[f(mv.get(k)) for k in ('cameraXMin', 'cameraXMax', 'cameraYMin', 'cameraYMax')]))
    return solids, gates, triggers


def collision_grid(solids, cpu, margin=2.0):
    if not solids:
        return 0.0, 0.0, 8, 8, np.zeros((8, 8), bool)
    pts = np.concatenate([p for _, p, _ in solids])
    x0, y0 = np.floor(pts.min(0) - margin)
    x1, y1 = np.ceil(pts.max(0) + margin)
    gw, gh = int((x1 - x0) * cpu), int((y1 - y0) * cpu)
    to = lambda p: [((x - x0) * cpu, (y1 - y) * cpu) for x, y in p]     # image rows top-down
    grid = np.zeros((gh, gw), bool)
    edges = Image.new('1', (gw, gh), 0)
    de = ImageDraw.Draw(edges)
    groups = collections.defaultdict(list)
    for kind, p, cid in solids:
        groups[cid].append((kind, p))
    for shapes in groups.values():
        # one collider's loops combine even-odd (a loop inside another is a hole)
        acc = None
        for kind, p in shapes:
            if kind == 'poly':
                im = Image.new('1', (gw, gh), 0)
                ImageDraw.Draw(im).polygon(to(p), fill=1, outline=1)
                a_ = np.asarray(im, bool)
                acc = a_ if acc is None else acc ^ a_
            de.line(to(p), fill=1, width=1)
        if acc is not None:
            grid |= acc
    grid |= np.asarray(edges, bool)
    grid = grid[::-1]                                                      # row 0 = bottom
    return float(x0), float(y0), gw, gh, grid


# ---------------------------------------------------------------- scenery
@__import__('functools').lru_cache(64)
def _texture(path):
    return np.asarray(Image.open(path).convert('RGBA'))


def sprite_image(guid, fileid):
    """Unity Sprite -> (RGBA array, local quad (x0, y0, x1, y1) in units), or None"""
    p = guidmap().get(guid)
    if not p or not p.endswith('.asset'):
        return None
    s = None
    for t, v in yaml_file(p).values():
        if t == 'Sprite':
            s = v
            break
    if not s:
        return None
    rd = s.get('m_RD') or {}
    tex = guidmap().get((rd.get('texture') or {}).get('guid'))
    if not tex or not os.path.exists(tex):
        return None
    r = rd.get('textureRect') or s.get('m_Rect')
    rx, ry, rw, rh = (f(r.get(k)) for k in ('x', 'y', 'width', 'height'))
    if rw < 1 or rh < 1:
        return None
    a = _texture(tex)
    th = a.shape[0]
    top = int(round(th - ry - rh))
    crop = a[max(0, top):int(round(th - ry)), int(round(rx)):int(round(rx + rw))]
    rot = (int(rd.get('settingsRaw') or 0) >> 2) & 0xF
    if rot == 1:
        crop = crop[:, ::-1]
    elif rot == 2:
        crop = crop[::-1]
    elif rot == 3:
        crop = crop[::-1, ::-1]
    elif rot == 4:
        crop = np.rot90(crop, 1)
    ppu = f(s.get('m_PixelsToUnits')) or 100.0
    full = s.get('m_Rect')
    fw, fh = f(full['width']), f(full['height'])
    pv = s.get('m_Pivot') or {}
    tro = rd.get('textureRectOffset') or {}
    x0 = (f(tro.get('x')) - f(pv.get('x')) * fw) / ppu
    y0 = (f(tro.get('y')) - f(pv.get('y')) * fh) / ppu
    return crop, (x0, y0, x0 + crop.shape[1] / ppu, y0 + crop.shape[0] / ppu)


BLEND_NORMAL, BLEND_ADD, BLEND_MUL = 0, 1, 2
BLEND_SUFFIX = {BLEND_NORMAL: '', BLEND_ADD: '+', BLEND_MUL: '*'}


@__import__('functools').lru_cache(None)
def material_blend(guid):
    """How a material blends, from its shader's name: additive / screen (glows, light shafts) -> BLEND_ADD,
    multiply / darkness -> BLEND_MUL, anything else -> BLEND_NORMAL"""
    p = guidmap().get(guid)
    if not p or not p.endswith('.mat'):
        return BLEND_NORMAL
    for t, v in yaml_file(p).values():
        if t == 'Material':
            sp = guidmap().get((v.get('m_Shader') or {}).get('guid'))
            name = (v.get('m_Name') or '') + ' '
            if sp and os.path.exists(sp):
                with open(sp, errors='replace') as fh:
                    m = re.search(r'Shader "([^"]+)"', fh.read(600))
                name += m.group(1) if m else os.path.basename(sp)
            n = name.lower()
            if re.search(r'additive|screen|add|glow', n):
                return BLEND_ADD
            if re.search(r'multiply|darkness', n):
                return BLEND_MUL
    return BLEND_NORMAL


def blend_of(v):
    ms = v.get('m_Materials') or []
    g = (ms[0] or {}).get('guid') if ms else None
    return material_blend(g) if g else BLEND_NORMAL


MASK_SCRIPTS = {'Unmasker', 'Remasker'}


def under_unmasker(sc, scripts, gid):
    """True for the masks of hidden areas and breakable walls: under an Unmasker / Remasker (which fade them when the
    hero walks in or out), in a "masks" group, or msk_* / Mask * sprites"""
    for _ in range(64):
        if any(t == 'MonoBehaviour' and scripts.get((v.get('m_Script') or {}).get('guid')) in MASK_SCRIPTS
               for t, v in sc.comps.get(gid, [])):
            return True
        n = str(sc.name(gid)).lower()
        if n in ('masks', 'mask') or n.startswith(('msk_', 'mask ', 'remasker')):
            return True
        t = sc.tr.get(gid)
        father = sc.tr_id.get(((t or {}).get('m_Father') or {}).get('fileID'))
        if father is None:
            return False
        gid = father['m_GameObject']['fileID']
    return False


def renderables(sc, scripts, sorting):
    out = []
    for gid, comps in sc.comps.items():
        if not sc.active(gid):
            continue
        # hidden areas' masks are left out: the engine doesn't run the Unmasker that fades them yet, and a black
        # mask over a room is worse than a secret shown
        if under_unmasker(sc, scripts, gid):
            continue
        m = sc.world(gid)
        # a tk2d sprite with an animator is an object the engine draws and animates itself (enemies, NPCs...): baked
        # too, it would leave a still copy of it in the scenery
        animated = any(t == 'MonoBehaviour' and scripts.get((v.get('m_Script') or {}).get('guid')) == 'TeamCherry.TK2D'
                       and 'library' in v and 'defaultClipId' in v for t, v in comps)
        for t, v in comps:
            if not v.get('m_Enabled', 1):
                continue
            if animated and t == 'MonoBehaviour' and 'collection' in v and '_spriteId' in v:
                continue
            if t == 'SpriteRenderer':
                sp = v.get('m_Sprite') or {}
                if not sp.get('guid'):
                    continue
                c = v.get('m_Color') or {}
                col = [f(c.get(k, 1)) for k in 'rgba']
                if col[3] <= 0.01:
                    continue
                out.append(dict(kind='sprite', gid=gid, m=m, z=m[2, 3], src=(sp['guid'], sp.get('fileID')), color=col,
                                flip=(int(v.get('m_FlipX') or 0), int(v.get('m_FlipY') or 0)),
                                size=(f((v.get('m_Size') or {}).get('x')), f((v.get('m_Size') or {}).get('y'))),
                                draw=int(v.get('m_DrawMode') or 0), blend=blend_of(v),
                                sort=(sorting.get(int(v.get('m_SortingLayerID') or 0), 0), int(v.get('m_SortingOrder') or 0))))
            elif t == 'MonoBehaviour' and scripts.get((v.get('m_Script') or {}).get('guid')) == 'TeamCherry.TK2D' \
                    and 'collection' in v and '_spriteId' in v:
                mr = next((x for tt, x in comps if tt == 'MeshRenderer'), {})
                if mr and not mr.get('m_Enabled', 1):
                    continue
                c = v.get('_color') or {}
                col = [f(c.get(k, 1)) for k in 'rgba']
                sc_ = v.get('_scale') or {}
                if col[3] <= 0.01:
                    continue
                out.append(dict(kind='tk2d', gid=gid, m=m, z=m[2, 3], src=((v.get('collection') or {}).get('guid'),
                                int(v.get('_spriteId') or 0)), color=col,
                                scale=(f(sc_.get('x', 1)) or 1, f(sc_.get('y', 1)) or 1), blend=blend_of(mr),
                                sort=(sorting.get(int(mr.get('m_SortingLayerID') or 0), 0), int(mr.get('m_SortingOrder') or 0))))
    # Unity's order for transparent sprites: sorting layer, order in layer, then far to near
    out.sort(key=lambda r: (r['sort'][0], r['sort'][1], -r['z']))
    return out


def image_of(r):
    """(RGBA float array, local quad, 2x3 local->world) for a renderable, or None"""
    m = r['m'][:2, [0, 1, 3]].copy()
    if r['kind'] == 'sprite':
        got = sprite_image(*r['src'])
        if not got:
            return None
        img, (x0, y0, x1, y1) = got
        if r['draw'] in (1, 2) and r['size'][0] > 0 and r['size'][1] > 0:       # sliced / tiled: stretched to size
            w, h = r['size']
            cx, cy = (x0 + x1) / 2 / max(1e-6, x1 - x0) * w, (y0 + y1) / 2 / max(1e-6, y1 - y0) * h
            x0, x1, y0, y1 = cx - w / 2, cx + w / 2, cy - h / 2, cy + h / 2
        if r['flip'][0]:
            img, x0, x1 = img[:, ::-1], -x1, -x0
        if r['flip'][1]:
            img, y0, y1 = img[::-1], -y1, -y0
    else:
        coll = tk2d.collection_by_guid(r['src'][0])
        got = tk2d.cut_sprite(coll, r['src'][1]) if coll else None
        if not got:
            return None
        img, (x0, y0, x1, y1) = got
        sx, sy = r['scale']
        x0, x1 = sorted((x0 * sx, x1 * sx))
        y0, y1 = sorted((y0 * sy, y1 * sy))
        if sx < 0:
            img = img[:, ::-1]
        if sy < 0:
            img = img[::-1]
    return img, (x0, y0, x1, y1), m


def bake_layer(items, z_l, ppu, stats, clip=None, aspect=1.0):
    """Draw a layer's renderables onto one canvas -> (RGBA array, world x of its left edge, world y of its top edge,
    the ppu used). clip: (x0, y0, x1, y1), the part of the layer the camera can show; sprites outside are left out.
    aspect: pixels a unit down / across (0.5 for the 800 wide top screen, whose pixels are twice as tall as wide)."""
    k_of = lambda z: (CAM_Z + z_l) / (CAM_Z + z)
    prepared, boxes = [], []
    for r in items:
        got = image_of(r)
        if not got:
            stats['missing'] += 1
            continue
        img, (x0, y0, x1, y1), m = got
        k = k_of(r['z'])
        A = m[:, :2] * k
        corners = np.array([[x0, y0], [x1, y0], [x1, y1], [x0, y1]]) @ A.T + m[:, 2]
        # Giant black-tinted sprites (rain sheets, fades, veils) get their real colour from a script at run time:
        # baked as they are in the scene they would cover the room in black.
        if max(r['color'][:3]) < 0.06 and max(np.ptp(corners[:, 0]), np.ptp(corners[:, 1])) > 60:
            stats['veils'] += 1
            continue
        prepared.append((img, (x0, y0, x1, y1), A, m[:, 2], r['color'], corners))
        boxes.append(corners)
    if not prepared:
        return None
    if clip:
        keep = [i for i, c in enumerate(boxes) if c[:, 0].max() >= clip[0] and c[:, 0].min() <= clip[2]
                and c[:, 1].max() >= clip[1] and c[:, 1].min() <= clip[3]]
        stats['clipped'] += len(prepared) - len(keep)
        prepared, boxes = [prepared[i] for i in keep], [boxes[i] for i in keep]
        if not prepared:
            return None
    allc = np.concatenate(boxes)
    wx0, wy0 = allc.min(0)
    wx1, wy1 = allc.max(0)
    if clip:
        wx0, wy0, wx1, wy1 = max(wx0, clip[0]), max(wy0, clip[1]), min(wx1, clip[2]), min(wy1, clip[3])
    area = (wx1 - wx0) * (wy1 - wy0) * ppu * ppu * aspect
    if area > MAX_PIXELS:
        ppu = ppu * math.sqrt(MAX_PIXELS / area)
        stats['lowres'] += 1
    ppy = ppu * aspect
    W, H = int(math.ceil((wx1 - wx0) * ppu)) + 2, int(math.ceil((wy1 - wy0) * ppy)) + 2
    canvas = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    for img, (x0, y0, x1, y1), A, t, col, corners in prepared:
        # canvas pixel (cx, cy) -> world (wx0 + cx/ppu, wy1 - cy/ppu) -> local -> sprite pixel
        cxs = (corners[:, 0] - wx0) * ppu
        cys = (wy1 - corners[:, 1]) * ppy
        bx0, by0 = int(max(0, math.floor(cxs.min()))), int(max(0, math.floor(cys.min())))
        bx1, by1 = int(min(W, math.ceil(cxs.max()))), int(min(H, math.ceil(cys.max())))
        if bx1 <= bx0 or by1 <= by0:
            continue
        try:
            Ai = np.linalg.inv(A)
        except np.linalg.LinAlgError:
            continue
        ih, iw = img.shape[:2]
        sxu, syu = iw / (x1 - x0), ih / (y1 - y0)
        # sprite px = sxu * (lx - x0); sprite py = syu * (y1 - ly); local = Ai @ (world - t)
        # world x = wx0 + (bx0 + u) / ppu ; world y = wy1 - (by0 + v) / ppu   (u, v: pixel in the bbox image)
        M = np.array([[1 / ppu, 0, wx0 + bx0 / ppu - t[0]], [0, -1 / ppy, wy1 - by0 / ppy - t[1]]])
        L = Ai @ M                                       # (u, v, 1) -> local (lx, ly)
        S = np.array([[sxu, 0, -sxu * x0], [0, -syu, syu * y1]])
        T = S @ np.vstack([L, [0, 0, 1]])                # (u, v, 1) -> sprite pixel
        src = Image.fromarray(img)
        if col != [1, 1, 1, 1]:
            a = img.astype(np.float32) * np.array(col, np.float32)
            src = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))
        # shrunk a lot (several sprite pixels to a canvas pixel): first a proper Lanczos shrink, which the affine
        # transform's bilinear sampling isn't (it would skip pixels and leave the picture grainy)
        kx, ky = math.hypot(T[0, 0], T[0, 1]), math.hypot(T[1, 0], T[1, 1])
        if kx > 1.25 or ky > 1.25:
            nw = max(1, int(round(iw / max(1.0, kx))))
            nh = max(1, int(round(ih / max(1.0, ky))))
            src = src.resize((nw, nh), Image.LANCZOS)
            T = np.diag([nw / iw, nh / ih]) @ T
        part = src.transform((bx1 - bx0, by1 - by0), Image.AFFINE, data=tuple(T.reshape(-1)), resample=Image.BILINEAR)
        canvas.alpha_composite(part, (bx0, by0))
        stats['drawn'] += 1
    return np.asarray(canvas), float(wx0), float(wy1), ppu, ppy


def tiles_of(arr, fmt):
    H, W = arr.shape[:2]
    out = []
    for ty in range(0, H, TILE):
        for tx in range(0, W, TILE):
            t = arr[ty:ty + TILE, tx:tx + TILE]
            if t[..., 3].max() == 0:
                continue
            if t.shape[0] < TILE or t.shape[1] < TILE:
                t = np.pad(t, ((0, TILE - t.shape[0]), (0, TILE - t.shape[1]), (0, 0)))
            # a tile with no transparency: RGB565 (smoother colour), flagged in its x index; else the layer's format,
            # 4-bit colour dithered against banding
            if fmt == ctrtex.FMT_RGBA4 and t[..., 3].min() == 255:
                out.append(((tx // TILE) | 0x8000, ty // TILE, zlib.compress(ctrtex.encode(t, ctrtex.FMT_RGB565, True), 9)))
            else:
                out.append((tx // TILE, ty // TILE, zlib.compress(ctrtex.encode(t, fmt, True), 9)))
    return out, (W + TILE - 1) // TILE, (H + TILE - 1) // TILE


def sorting_layers():
    """sorting layer unique id -> its rank (TagManager of the project export)"""
    p = os.path.join(ROOT, 'project', 'ExportedProject', 'ProjectSettings', 'TagManager.asset')
    if not os.path.exists(p):
        return {}
    for t, v in load_yaml_docs(p).values():
        if 'm_SortingLayers' in v:
            return {int(l.get('uniqueID') or 0): i for i, l in enumerate(v['m_SortingLayers'])}
    return {}


# ---------------------------------------------------------------- output
def s(txt, n):
    txt = '' if txt is None else str(txt)       # YAML may give a name as a bool or a number
    return txt.encode('utf-8')[:n - 1].ljust(n, b'\0')


def find_scene(name):
    import glob
    hits = glob.glob(f'{EX}/Scenes/**/{name}.unity', recursive=True) or glob.glob(f'{EX}/**/{name}.unity', recursive=True)
    return hits[0] if hits else None


def build(name, out, a, scripts, sorting):
    path = find_scene(name)
    if not path:
        print(f'{name}: no scene in the export')
        return False
    sc = Scene(path)
    # the title screen (MenuStyles): its first style (the one a new save shows) switched on, the others stay off
    for gid, comps in sc.comps.items():
        for t, v in comps:
            if t == 'MonoBehaviour' and isinstance(v.get('Styles'), list) and v['Styles']:
                k = v.get('StyleDefault')
                k = int(k) if isinstance(k, int) and 0 <= k < len(v['Styles']) else 0
                so = (v['Styles'][k].get('StyleObject') or {}).get('fileID')
                if so in sc.go:
                    sc.go[so]['m_IsActive'] = 1
                    sc._active.clear()
    solids, gates, triggers = gather(sc, scripts)
    gx, gy, gw, gh, grid = collision_grid(solids, a.cpu)
    spawn = next((t['rect'][:2] for t in triggers if t['kind'] == 'respawn'), None) or \
        (gates[0]['rect'][:2] if gates else [gx + gw / a.cpu / 2, gy + gh / a.cpu / 2])

    layers, stats = [], collections.Counter()
    if not a.no_art:
        rs = renderables(sc, scripts, sorting)
        for (base_name, z0, z1), blend in ((L, bl) for L in LAYERS for bl in (BLEND_NORMAL, BLEND_ADD, BLEND_MUL)):
            # each depth gives up to three layers: normal sprites, then additive ('+'), then multiply ('*')
            lname = base_name + BLEND_SUFFIX[blend]
            items = [r for r in rs if z0 <= r['z'] < z1 and r.get('blend', 0) == blend]
            if not items:
                continue
            z_l = 0.0 if base_name == 'main' else float(np.median([r['z'] for r in items]))
            par = CAM_Z / (CAM_Z + z_l)
            # what the camera can show of this layer while it stays in the room: room +- view / parallax
            rx0, ry0 = gx, gy
            rx1, ry1 = gx + gw / a.cpu, gy + gh / a.cpu
            clip = (rx0 - VIEW[0] / par, ry0 - VIEW[1] / par, rx1 + VIEW[0] / par, ry1 + VIEW[1] / par)                 if solids and not name.startswith('Menu_') else None
            # as many pixels a unit as the screen shows of this layer (its parallax scales it on screen)
            got = bake_layer(items, z_l, max(4.0, a.ppu * min(par, 1.6)), stats, clip, a.aspect)
            if not got:
                continue
            arr, ox, oy, lppu, lppy = got
            tiles, ntx, nty = tiles_of(arr, ctrtex.FORMATS[a.fmt])
            layers.append(dict(name=lname, blend=blend, z=z_l, parallax=par, ox=ox, oy=oy, ppu=lppu, ppy=lppy,
                               ntx=ntx, nty=nty, tiles=tiles, count=len(items)))

    os.makedirs(os.path.join(out, 'rooms'), exist_ok=True)
    os.makedirs(os.path.join(WORK, 'rooms'), exist_ok=True)
    base = os.path.join(out, 'rooms', name)
    info_base = os.path.join(WORK, 'rooms', name)        # readable copies, kept out of the RomFS

    # live objects, and the PlayMaker FSMs (templates resolved) of the room
    objs = [] if a.no_objects else objects.gather_objects(sc, scripts, collider_shapes, rect_of)
    if objs:
        objects.write(base + '.sso', objs)
        json.dump(objs, open(info_base + '.objs.json', 'w'), indent=1)
    if not a.no_objects:
        # the scene's whole hierarchy, for the FSM compiler (tools/build_fsm.py): the FSMs name objects by these ids
        tree = []
        for gid in sc.go:
            t = sc.tr.get(gid)
            father = sc.tr_id.get(((t or {}).get('m_Father') or {}).get('fileID')) if t else None
            m = sc.world(gid)
            comps = sc.comps.get(gid, [])
            shapes = [s for t, v in comps if t.endswith('Collider2D') for s in collider_shapes(sc, gid, t, v)]
            rect = [round(x, 3) for x in rect_of(shapes)] if shapes else []
            rb = next((v for t, v in comps if t == 'Rigidbody2D'), None)
            body = [int(f(rb.get('m_BodyType'))), round(float(f(rb.get('m_GravityScale', 1))), 3)] if rb else []
            sx = float(np.sign(m[0, 0]) * np.hypot(m[0, 0], m[1, 0]))
            tree.append([gid, str(sc.name(gid)), father['m_GameObject']['fileID'] if father else 0,
                         round(float(m[0, 3]), 3), round(float(m[1, 3]), 3), int(bool(sc.go[gid].get('m_IsActive', 1))),
                         rect, body, round(sx, 3)])
        json.dump(tree, open(info_base + '.tree.json', 'w'), separators=(',', ':'))
        json.dump(getattr(sc, 'dyn_solids', []), open(info_base + '.dyn.json', 'w'), separators=(',', ':'))
    parts = [] if a.no_objects else particles.gather_particles(sc, material_blend)
    if parts:
        particles.write(base + '.spt', parts)
        json.dump(parts, open(info_base + '.parts.json', 'w'), indent=1)
    nfsm = 0
    if not a.no_objects:
        try:
            fsms, _ = fsm_extract.extract(path)
        except Exception as ex:
            fsms = []
            print(f'{name}: FSMs not read ({ex})')
        if fsms:
            os.makedirs(os.path.join(out, 'fsm', 'templates'), exist_ok=True)
            with open(os.path.join(out, 'fsm', name + '.json'), 'w', encoding='utf-8') as fh:
                json.dump(fsms, fh, separators=(',', ':'), default=str)
            nfsm = len(fsms)
            # the templates their RunFSM actions start (and the ones those start): fsm/templates/<guid>.json, once
            todo, seen = list(fsm_extract.template_refs(fsms)), set()
            while todo:
                g = todo.pop()
                if g in seen:
                    continue
                seen.add(g)
                tpath = os.path.join(out, 'fsm', 'templates', g + '.json')
                d = fsm_extract.decode_template(g)
                if d is None:
                    continue
                todo += list(fsm_extract.template_refs(d))
                if not os.path.exists(tpath):
                    tmp = f'{tpath}.{os.getpid()}'
                    with open(tmp, 'w', encoding='utf-8') as fh:
                        json.dump(d, fh, separators=(',', ':'), default=str)
                    os.replace(tmp, tpath)
    rowb = (gw + 7) // 8
    bits = np.packbits(np.pad(grid, ((0, 0), (0, rowb * 8 - gw))), axis=1, bitorder='little')
    b = bytearray()
    b += b'SSRM' + struct.pack('<HHffHHffHHHH', 1, a.cpu, gx, gy, gw, gh, float(spawn[0]), float(spawn[1]),
                                len(gates), len(triggers), len(layers), 0)
    b += bits.tobytes()
    for g in gates:
        b += s(g['name'], 16) + s(g['target'], 32) + s(g['entry'], 16) + struct.pack('<6fBB2x', *g['rect'], *g['offset'],
                                                                                    g['enter_left'], g['enter_right'])
    KIND = {'respawn': 1, 'hazard': 2, 'camlock': 3}
    for t in triggers:
        extra = t.get('cam') or [float(t.get('damage', 0)), float(t.get('hazard', 0)), 0, 0]
        b += struct.pack('<B3x4f4f', KIND[t['kind']], *t['rect'], *extra) + s(t['name'], 32)
    data, off = bytearray(), 0
    for L in layers:
        # the spare 2 bytes: the pixels a unit down, in 1/256 (0: as across)
        b += s(L['name'], 8) + struct.pack('<5fHHHHBBH', L['z'], L['parallax'], L['ox'], L['oy'], L['ppu'], TILE,
                                         L['ntx'], L['nty'], len(L['tiles']), ctrtex.FORMATS[a.fmt], L['blend'],
                                         min(65535, int(round(L.get('ppy', L['ppu']) * 256))))
        for tx, ty, blob in L['tiles']:
            b += struct.pack('<HHII', tx, ty, off, len(blob))
            data += blob
            off += len(blob)
    open(base + '.ssr', 'wb').write(b)
    if layers:
        open(base + '.ssl', 'wb').write(data)
    info = dict(scene=name, grid=dict(x=gx, y=gy, w=gw, h=gh, cells_per_unit=a.cpu, solid_cells=int(grid.sum())),
                spawn=[float(spawn[0]), float(spawn[1])], gates=gates, triggers=triggers,
                layers=[{k: v for k, v in L.items() if k != 'tiles'} | {'tiles': len(L['tiles'])} for L in layers],
                sprites=dict(drawn=stats['drawn'], missing=stats['missing']))
    info['objects'] = len(objs)
    info['particles'] = len(parts)
    info['fsms'] = nfsm
    json.dump(info, open(info_base + '.json', 'w'), indent=1)
    if a.preview:
        preview(info, grid, a.preview if len(a.scenes) == 1 else a.preview.replace('.png', f'_{name}.png'))
    print(f'{name}: {gw}x{gh} cells, {len(solids)} solid shapes, {len(gates)} gates, {len(triggers)} triggers, '
          f'{len(objs)} objects, {len(parts)} particle emitters, {nfsm} FSMs, '
          f'{len(layers)} layers / {sum(len(L["tiles"]) for L in layers)} tiles ({len(data) / 1048576:.1f} MB), '
          f'{stats["drawn"]} sprites drawn, {stats["missing"]} missing, {stats["clipped"]} out of view'
          + (f', {stats["lowres"]} layer(s) at lower resolution' if stats['lowres'] else '')
          + (f', {stats["veils"]} black veil(s) left out' if stats['veils'] else ''))
    return True


def preview(info, grid, path, scale=6):
    """geometry only: solid cells, gates (blue), triggers (red hazards, green respawn, yellow camera locks), layer tiles"""
    g = info['grid']
    cpu = g['cells_per_unit']
    W, H = g['w'] * scale // cpu * 1, g['h'] * scale // cpu
    sx = scale / cpu
    img = Image.fromarray(np.where(grid[::-1], 70, 235).astype(np.uint8)).resize((max(1, int(g['w'] * sx)), max(1, int(g['h'] * sx))), Image.NEAREST).convert('RGB')
    d = ImageDraw.Draw(img)
    X = lambda x: (x - g['x']) * scale
    Y = lambda y: img.height - (y - g['y']) * scale
    for L in info['layers']:
        for i in range(L['ntx'] + 1):
            pass
        x0, y0 = L['ox'], L['oy']
        tw = 256 / L['ppu']
        d.rectangle([X(x0), Y(y0), X(x0 + L['ntx'] * tw), Y(y0 - L['nty'] * tw)], outline=(150, 150, 255))
    colors = {'hazard': (220, 40, 40), 'respawn': (40, 180, 40), 'camlock': (220, 200, 0)}
    for t in info['triggers']:
        x, y, w, h = t['rect']
        d.rectangle([X(x), Y(y + h), X(x + w), Y(y)], outline=colors[t['kind']], width=2)
    for gt in info['gates']:
        x, y, w, h = gt['rect']
        d.rectangle([X(x), Y(y + h), X(x + w), Y(y)], outline=(30, 60, 230), width=3)
        d.text((X(x) + 3, Y(y + h) + 3), f"{gt['name']} > {gt['target']}", fill=(30, 60, 230))
    sx_, sy_ = info['spawn']
    d.ellipse([X(sx_) - 5, Y(sy_) - 5, X(sx_) + 5, Y(sy_) + 5], fill=(255, 0, 255))
    img.save(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('out')
    ap.add_argument('scenes', nargs='*')
    ap.add_argument('--list')
    ap.add_argument('--ppu', type=float, default=28)
    ap.add_argument('--cpu', type=int, default=4)
    ap.add_argument('--fmt', choices=list(ctrtex.FORMATS), default='rgba4')
    ap.add_argument('--aspect', type=float, default=0.5)   # pixels a unit down / across (the 800 wide top screen)
    ap.add_argument('--no-art', action='store_true')
    ap.add_argument('--no-objects', action='store_true')
    ap.add_argument('--preview')
    a = ap.parse_args()
    if a.list:
        a.scenes += [l.strip() for l in open(a.list) if l.strip() and not l.startswith('#')]
    if not a.scenes:
        ap.error('no scene given')
    scripts, sorting = script_names(), sorting_layers()
    bad = [n for n in a.scenes if not build(n, a.out, a, scripts, sorting)]
    if bad:
        print(f'{len(bad)} scene(s) not converted: {" ".join(bad[:20])}')
        sys.exit(1)


if __name__ == '__main__':
    main()
