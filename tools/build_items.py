#!/usr/bin/env python3
"""Sprites the engine needs that no tk2d animation library holds:

- sprites/rosary.sss: the rosaries enemies drop (prefabs Geo Small / Med / Large), from their Unity animator clips:
  "<Size> <State>" for Size in Small, Med, Large and State in Air, Idle, Land, Gleam.
- sprites/hud_spool.sss: the silk spool's static pieces from the HUD collection (rod, bind notch), one-frame clips
  named after the sprites.

  python tools/build_items.py <romfs dir>
Also adds the sounds they play (rosary bounce and collect) to sfx/ (as tools/build_fx.py does)."""
import os, sys, re, subprocess
import numpy as np
from PIL import Image
from sscommon import EX, guidmap, yaml_file, load_yaml_docs
import tk2d, ctrtex
from build_room import sprite_image
from build_sprites import add_image, write_set

SCALE = 28 / 64
RATE = 22050


def scaled(img, quad):
    h, w = img.shape[:2]
    img = np.asarray(Image.fromarray(img).resize((max(1, round(w * SCALE)), max(1, round(h * SCALE))), Image.LANCZOS))
    return img, quad


def controller_clips(path):
    """an AnimatorController or override -> {state name: .anim path}"""
    gm = guidmap()
    docs = load_yaml_docs(path)
    out = {}
    for t, v in docs.values():
        if t == 'AnimatorOverrideController':
            base = controller_clips(gm[v['m_Controller']['guid']])
            orig = {}
            for c in v.get('m_Clips', []):
                o, n = gm.get(c['m_OriginalClip'].get('guid')), gm.get((c.get('m_OverrideClip') or {}).get('guid'))
                if o and n:
                    orig[o] = n
            return {k: orig.get(p, p) for k, p in base.items()}
    for t, v in docs.values():
        if t == 'AnimatorState':
            m = (v.get('m_Motion') or {}).get('guid')
            if m and gm.get(m):
                out[v['m_Name']] = gm[m]
    return out


def anim_frames(path):
    """an .anim -> (fps, loop, [(time, sprite guid)]) from its sprite curve"""
    for t, v in yaml_file(path).values():
        if t != 'AnimationClip':
            continue
        keys = []
        for cv in v.get('m_PPtrCurves') or []:
            if cv.get('attribute') == 'm_Sprite':
                keys = [(float(k['time']), k['value'].get('guid')) for k in cv['curve']]
        loop = (v.get('m_AnimationClipSettings') or {}).get('m_LoopTime', 0)
        return float(v.get('m_SampleRate') or 12), int(loop), keys
    return 12, 0, []


def rosaries(out):
    clips, frames, images, seen, cache = {}, [], [], {}, {}
    for size, prefab in (('Small', 'Geo Small'), ('Med', 'Geo Med'), ('Large', 'Geo Large')):
        docs = load_yaml_docs(os.path.join(EX, 'Prefabs', 'Items', prefab + '.prefab'))
        go = {f: v.get('m_Name') for f, (t, v) in docs.items() if t == 'GameObject'}
        ctrl = None
        for t, v in docs.values():
            if t == 'Animator' and go.get(v['m_GameObject']['fileID']) == prefab:
                ctrl = guidmap().get(v['m_Controller']['guid'])
        states = controller_clips(ctrl)
        for st, ap in states.items():
            fps, loop, keys = anim_frames(ap)
            if not keys:
                continue
            # one frame per key, held until the next (the engine plays frames at a fixed rate: resample at fps)
            end = max(keys[-1][0], keys[0][0] + 1 / fps)
            ids = []
            n = max(1, round(end * fps))
            for i in range(n):
                tm = i / fps
                g = [k for k in keys if k[0] <= tm + 1e-4][-1][1] if any(k[0] <= tm + 1e-4 for k in keys) else keys[0][1]
                if g not in cache:
                    got = sprite_image(g, 21300000)
                    cache[g] = add_image(*scaled(*got), frames, images, seen) if got else -1
                ids.append(cache[g])
            clips[f'{size} {st}'] = dict(fps=fps, loop_start=0, wrap=0 if loop else 2, ids=ids)
    # shell shards (prefab Shell Shard 01): one sprite each, picked at random; clip "Shard", one frame per sprite
    import pickle
    from sscommon import ROOT
    spr, _ = pickle.load(open(os.path.join(ROOT, 'metamaps.pkl'), 'rb'))
    byname = {os.path.basename(p.replace('\\', '/')): g for g, p in spr.items()}
    ids = []
    for i in range(16):
        g = byname.get(f'shell_shards{i:04d}.asset')
        got = sprite_image(g, 21300000) if g else None
        if got:
            ids.append(add_image(*scaled(*got), frames, images, seen))
    clips['Shard'] = dict(fps=12, loop_start=0, wrap=6, ids=ids)
    write_set(os.path.join(out, 'sprites', 'rosary.sss'), clips, frames, images, ctrtex.FORMATS['rgba4'])


def spool(out):
    coll = tk2d.collection_by_guid('89b709345ecca6342b2750a166656645')      # HUD Cln
    names = {s['name']: i for i, s in enumerate(coll['sprites'])} if isinstance(coll, dict) and 'sprites' in coll else \
        {s['name']: i for i, s in enumerate(coll)}
    clips, frames, images, seen = {}, [], [], {}
    for n in ('Hornet_Spool_0002_rod', 'Hornet_Spool_0001_bind_notch', 'Hornet_Spool_0000_cap', 'HUD_coin_v020004',
              'shell_shard_icon0004'):
        got = tk2d.cut_sprite(coll, names[n]) if n in names else None
        if not got:
            print('missing', n)
            continue
        i = add_image(*scaled(*got), frames, images, seen)
        clips[n] = dict(fps=12, loop_start=0, wrap=2, ids=[i])
    write_set(os.path.join(out, 'sprites', 'hud_spool.sss'), clips, frames, images, ctrtex.FORMATS['rgba4'])


def sounds(out):
    gm = guidmap()
    want = []
    for p in gm.values():
        b = os.path.basename(p)
        if re.match(r'(rosary_(bounce|collect)|shell_shard_pickup)_\d\.ogg$', b) or b in ('hornet_slide_after_sprint.ogg', 'hero_dash.ogg', 'hornet_bind_pt_1.ogg', 'bind1.ogg', 'bind3.ogg', 'bind4.ogg',
                                                                    'hero_wall_slide.ogg', 'bench_rest.ogg'):
            want.append(p)
    rev = {v: k for k, v in gm.items()}
    idx_path = os.path.join(out, 'sfx', 'index.txt')
    have = {l.split('\t')[0] for l in open(idx_path, encoding='utf-8')}
    added = 0
    with open(idx_path, 'a', encoding='utf-8') as idx:
        for src in sorted(set(want)):
            g = rev[src]
            if g in have:
                continue
            stem = os.path.splitext(os.path.basename(src))[0]
            dst = os.path.join(out, 'sfx', stem + '.pcm')
            if not os.path.exists(dst):
                r = subprocess.run(['ffmpeg', '-v', 'fatal', '-y', '-i', src, '-ac', '1', '-ar', str(RATE), '-f', 's16le',
                                    '-acodec', 'pcm_s16le', dst])
                if r.returncode:
                    continue
            idx.write(f'{g}\t{stem}.ogg\n')
            added += 1
    print(f'{added} sounds added')


def main():
    out = sys.argv[1]
    rosaries(out)
    spool(out)
    sounds(out)


if __name__ == '__main__':
    main()
