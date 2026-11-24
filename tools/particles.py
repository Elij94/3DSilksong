# The particle emitters of a room (Unity ParticleSystem), for build_room.py: where, how many, how long, how fast,
# what size and colour, and which texture. Written to romfs/rooms/<Scene>.spt (format: docs/FORMATS.md); the
# textures are packed by tools/build_particles.py. A readable copy goes to work/rooms/<Scene>.parts.json.
# Curves are reduced to their range (min, max): enough to reproduce most dust, sparks, drips and glows.
import os, re, math, struct
from sscommon import guidmap, yaml_file, f

LOOP, PREWARM, PLAY_ON_AWAKE, WORLD_SPACE, INACTIVE = 1, 2, 4, 8, 16
SHAPES = {0: 'sphere', 4: 'cone', 5: 'box', 10: 'circle', 12: 'edge', 17: 'rectangle'}


def minmax(c, default=0.0):
    """Unity MinMaxCurve -> (min, max)"""
    if not isinstance(c, dict):
        return default, default
    st, sc, mn = int(f(c.get('minMaxState'))), f(c.get('scalar')), f(c.get('minScalar'))
    if st == 3:
        return min(mn, sc), max(mn, sc)
    if st in (1, 2):
        ks = [f(k.get('value')) for k in ((c.get('maxCurve') or {}).get('m_Curve') or [])] + \
             ([f(k.get('value')) for k in ((c.get('minCurve') or {}).get('m_Curve') or [])] if st == 2 else [])
        if ks:
            return sc * min(ks), sc * max(ks)
    return sc, sc


def color(c):
    return [f(c.get(k, 1)) for k in 'rgba'] if isinstance(c, dict) else [1.0, 1.0, 1.0, 1.0]


def gradient_ends(g):
    """Unity Gradient -> (first colour, last colour)"""
    if not isinstance(g, dict):
        return [1.0] * 4, [1.0] * 4
    n_c, n_a = int(f(g.get('m_NumColorKeys')) or 2), int(f(g.get('m_NumAlphaKeys')) or 2)
    key = lambda i: g.get(f'key{i}') or {}
    first, last = key(0), key(max(n_c, n_a) - 1)
    c0 = [f(first.get(k, 1)) for k in 'rgb'] + [f(key(0).get('a', 1))]
    c1 = [f(key(n_c - 1).get(k, 1)) for k in 'rgb'] + [f(key(n_a - 1).get('a', 1))]
    return c0, c1


def start_colors(c):
    if not isinstance(c, dict):
        return [1.0] * 4, [1.0] * 4
    st = int(f(c.get('minMaxState')))
    if st == 0:
        return color(c.get('maxColor')), color(c.get('maxColor'))
    if st == 2:
        return color(c.get('minColor')), color(c.get('maxColor'))
    return gradient_ends(c.get('maxGradient'))


def main_texture(mat_guid):
    p = guidmap().get(mat_guid)
    if not p or not p.endswith('.mat'):
        return None
    for t, v in yaml_file(p).values():
        if t != 'Material':
            continue
        envs = ((v.get('m_SavedProperties') or {}).get('m_TexEnvs')) or []
        items = list(envs.items()) if isinstance(envs, dict) else []      # a dict, or a list of entries
        for e in (envs if isinstance(envs, list) else []):
            if isinstance(e, dict) and 'first' in e:
                items.append((e['first'], e.get('second') or {}))
            elif isinstance(e, dict):
                items += list(e.items())
        for name, d in items:
            if name in ('_MainTex', '_BaseMap') and isinstance(d, dict):
                g = (d.get('m_Texture') or {}).get('guid')
                if g and guidmap().get(g):
                    return g
    return None


def safe(name):
    return re.sub(r'[^a-z0-9_]+', '_', (name or '').lower()).strip('_')


def gather_particles(sc, material_blend):
    out = []
    for gid, comps in sc.comps.items():
        ps = next((v for t, v in comps if t == 'ParticleSystem'), None)
        pr = next((v for t, v in comps if t == 'ParticleSystemRenderer'), None)
        if not ps or not pr or not pr.get('m_Enabled', 1):
            continue
        im = ps.get('InitialModule') or {}
        em = ps.get('EmissionModule') or {}
        sh = ps.get('ShapeModule') or {}
        col = ps.get('ColorModule') or {}
        size = ps.get('SizeModule') or {}
        uv = ps.get('UVModule') or {}
        mats = pr.get('m_Materials') or []
        mg = (mats[0] or {}).get('guid') if mats else None
        tex = main_texture(mg) if mg else None
        tex_name = os.path.basename(guidmap().get(tex, '')).rsplit('.', 1)[0] if tex else ''
        m = sc.world(gid)
        flags = (LOOP if f(ps.get('looping')) else 0) | (PREWARM if f(ps.get('prewarm')) else 0) | \
            (PLAY_ON_AWAKE if f(ps.get('playOnAwake', 1)) else 0) | \
            (WORLD_SPACE if int(f(ps.get('moveWithTransform'))) == 1 else 0) | (0 if sc.active(gid) else INACTIVE)
        bursts = em.get('m_Bursts') or []
        b0 = bursts[0] if bursts and isinstance(bursts[0], dict) else {}
        c0, c1 = start_colors(im.get('startColor'))
        end = gradient_ends((col.get('gradient') or {}).get('maxGradient'))[1] if f(col.get('enabled')) else [1.0] * 4
        end_size = minmax((size.get('curve') or {}))[1] if f(size.get('enabled')) else 1.0
        sc_ = sh.get('m_Scale') or {}
        out.append(dict(
            name=sc.name(gid), flags=flags, blend=material_blend(mg) if mg else 0,
            shape=int(f(sh.get('type'))) if f(sh.get('enabled')) else -1,
            pos=[float(m[0, 3]), float(m[1, 3]), float(m[2, 3])],
            rot=math.degrees(math.atan2(m[1, 0], m[0, 0])),
            duration=f(ps.get('lengthInSec')), rate=minmax(em.get('rateOverTime'))[1] if f(em.get('enabled', 1)) else 0.0,
            max_particles=int(f(im.get('maxNumParticles')) or 1000),
            burst_count=int(minmax(b0.get('countCurve'))[1]) if b0 else 0, burst_time=f(b0.get('time')) if b0 else 0.0,
            life=minmax(im.get('startLifetime'), 1), speed=minmax(im.get('startSpeed')), size=minmax(im.get('startSize'), 1),
            rotation=[math.degrees(x) for x in minmax(im.get('startRotation'))],
            gravity=minmax(im.get('gravityModifier'))[1], radius=f(sh.get('radius', {}).get('value') if isinstance(sh.get('radius'), dict) else sh.get('radius')),
            angle=f(sh.get('angle')), box=[f(sc_.get('x', 1)), f(sc_.get('y', 1))],
            color0=c0, color1=c1, end_color=end, end_size=end_size,
            tiles=[int(f(uv.get('tilesX')) or 1), int(f(uv.get('tilesY')) or 1)] if f(uv.get('enabled')) else [1, 1],
            texture=safe(tex_name), texture_guid=tex or ''))
    return out


def s(txt, n):
    txt = '' if txt is None else str(txt)
    return txt.encode('utf-8')[:n - 1].ljust(n, b'\0')


REC = '<HBb3ff f f HHf 2f2f2f2f f f f 2f 4f4f4f f BBH'


def write(path, parts):
    b = bytearray(b'SSPT' + struct.pack('<HH', 1, len(parts)))
    for p in parts:
        b += struct.pack(REC, p['flags'], p['blend'], p['shape'], *p['pos'], p['rot'], p['duration'], p['rate'],
                         min(65535, p['max_particles']), min(65535, p['burst_count']), p['burst_time'],
                         *p['life'], *p['speed'], *p['size'], *p['rotation'], p['gravity'], p['radius'], p['angle'],
                         *p['box'], *p['color0'], *p['color1'], *p['end_color'], p['end_size'],
                         min(255, p['tiles'][0]), min(255, p['tiles'][1]), 0)
        b += s(p['texture'], 32) + s(p['name'], 32)
    with open(path, 'wb') as fh:
        fh.write(b)
