# The live objects of a room (enemies, breakables, NPCs and interactables, animated things, objects driven by
# PlayMaker), for build_room.py: what they are, where, their tk2d animation and their numbers (HP, damage).
# Written to romfs/rooms/<Scene>.sso (format: docs/FORMATS.md), readable copy in work/rooms/<Scene>.objs.json.
import os, re, struct
import numpy as np
from sscommon import guidmap, f
import tk2d

ENEMY, BREAKABLE, INTERACT, ANIMATED, FSM, HAZARD, INACTIVE = 1, 2, 4, 8, 16, 32, 64
INTERACT_RE = re.compile(r'NPC|Interact|Bench|Lever|Door|Gate|Collectable|Pickup|Shop|Chest|Switch|Toll|Item', re.I)


def safe(name):
    """sprite set file name for a tk2d library (romfs/sprites/<safe>.sss)"""
    return re.sub(r'[^a-z0-9_]+', '_', (name or '').lower()).strip('_')


def library_info(guid, clip_id):
    p = guidmap().get(guid)
    if not p or not p.endswith('.prefab'):
        return '', ''
    lib = tk2d.load_library(p)
    name = os.path.basename(p)[:-7]
    clips = list(lib) if lib else []
    return name, clips[clip_id] if 0 <= clip_id < len(clips) else ''


def gather_objects(sc, scripts, collider_shapes, rect_of):
    """-> [dict] for every game object with one of the components above, in the scene's order"""
    out, index = [], {}
    for gid, comps in sc.comps.items():
        monos = [(scripts.get((v.get('m_Script') or {}).get('guid'), ''), v) for t, v in comps if t == 'MonoBehaviour']
        names = {n for n, _ in monos}
        flags, hp, dmg, lib, clip, coll, sid, fsms = 0, 0, 0, '', '', '', -1, []
        for n, v in monos:
            if n == 'HealthManager':
                flags |= ENEMY
                hp = int(f(v.get('hp')))
            elif n == 'Breakable':
                flags |= BREAKABLE
            elif n == 'DamageHero':
                flags |= HAZARD
                dmg = int(f(v.get('damageDealt')))
            elif n == 'PlayMaker' and isinstance(v.get('fsm'), dict):
                flags |= FSM
                fsms.append(v['fsm'].get('name') or '')
            elif n == 'TeamCherry.TK2D' and 'library' in v and 'defaultClipId' in v:
                g = (v.get('library') or {}).get('guid')
                if g:
                    flags |= ANIMATED
                    lib, clip = library_info(g, int(f(v.get('defaultClipId'))))
            elif n == 'TeamCherry.TK2D' and 'collection' in v and '_spriteId' in v and not coll:
                c = tk2d.collection_by_guid((v.get('collection') or {}).get('guid'))
                coll, sid = (c['name'] if c else ''), int(f(v.get('_spriteId')))
            if INTERACT_RE.search(n):
                flags |= INTERACT
        if not flags & (ENEMY | BREAKABLE | INTERACT | ANIMATED | FSM | HAZARD):
            continue
        if not sc.active(gid):
            flags |= INACTIVE
        m = sc.world(gid)
        shapes = [s for t, v in comps if t.endswith('Collider2D') for s in collider_shapes(sc, gid, t, v)]
        rect = rect_of(shapes) if shapes else [float(m[0, 3]), float(m[1, 3]), 0.0, 0.0]
        index[gid] = len(out)
        out.append(dict(gid=gid, name=sc.name(gid), flags=flags, pos=[float(m[0, 3]), float(m[1, 3]), float(m[2, 3])],
                        scale=[float(np.sign(m[0, 0]) * np.hypot(m[0, 0], m[1, 0])), float(np.hypot(m[0, 1], m[1, 1]))],
                        rect=rect, hp=hp, damage=dmg, library=lib, sprite_set=safe(lib) if lib else '', clip=clip,
                        collection=coll, sprite_id=sid, fsms=fsms,
                        scripts=sorted(n for n in names if n and n not in ('PlayMaker', 'TeamCherry.TK2D'))))
    # the nearest listed ancestor of each object (-1: none), so a game can keep an enemy's parts together
    for o in out:
        p, parent = sc.tr.get(o['gid']), -1
        while p is not None:
            father = sc.tr_id.get((p.get('m_Father') or {}).get('fileID'))
            if father is None:
                break
            fg = father['m_GameObject']['fileID']
            if fg in index:
                parent = index[fg]
                break
            p = father
        o['parent'] = parent
    return out


def s(txt, n):
    # names can come out of YAML as other types ("Yes" -> True, "1" -> 1): write them as text
    txt = '' if txt is None else str(txt)
    return txt.encode('utf-8')[:n - 1].ljust(n, b'\0')


def write(path, objs):
    b = bytearray(b'SSOB' + struct.pack('<HH', 1, len(objs)))
    for o in objs:
        b += struct.pack('<HhfffffffffiiH2x', o['flags'], o['parent'], *o['pos'], *o['scale'], *o['rect'],
                         o['hp'], o['damage'], max(0, o['sprite_id']) if o['sprite_id'] >= 0 else 0xFFFF)
        b += s(o['name'], 32) + s(o['sprite_set'], 32) + s(o['clip'], 32) + s(o['collection'], 32) + \
            s(','.join(o['fsms']), 64)
    with open(path, 'wb') as fh:
        fh.write(b)
