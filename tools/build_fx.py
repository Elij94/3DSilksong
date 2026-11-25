#!/usr/bin/env python3
"""The game's hit and death effects for the engine: the profiles of Data Assets/Profiles/Hit Effects and Death Effects
(what a needle hit or a kill shows and plays, per kind of enemy), their effect prefabs' particle systems and their
sounds.

  python tools/build_fx.py <out_dir>          run before build_particles.py (it adds the fx textures)

Writes:
  <out>/fx/profiles.txt     one profile per line, tab-separated: kind (H hit, D death), name, flash (0/1),
                            effect prefabs (comma-separated, <out>/fx/<prefab>.spt), sound guids (comma-separated),
                            blood: min count, max count, min speed, max speed
  <out>/fx/<prefab>.spt     the prefab's particle systems, as a room's .spt (positions relative to the prefab)
  work/rooms/fx__<prefab>.parts.json   for build_particles.py
Sounds missing from <out>/sfx (index.txt) are converted there too (PCM, as build_music_pcm.py does).
"""
import os, sys, json, glob, subprocess
import yaml
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sscommon import EX, WORK, guidmap
import particles, fsm_extract
import build_room

RATE = 22050


def load_asset(path):
    txt = open(path, encoding='utf-8').read()
    body = txt.split('MonoBehaviour:', 1)[1] if 'MonoBehaviour:' in txt else ''
    return yaml.load('MonoBehaviour:' + body, Loader=fsm_extract._L).get('MonoBehaviour') or {}


def guids(lst, key=None):
    out = []
    for e in lst or []:
        r = e.get(key) if key else e
        if isinstance(r, dict) and r.get('guid'):
            out.append(r['guid'])
    return out


def blood(lst):
    if not lst:
        return [0, 0, 0, 0]
    b = lst[0]
    return [int(build_room.f(b.get('MinCount'))), int(build_room.f(b.get('MaxCount'))),
            float(build_room.f(b.get('MinSpeed'))), float(build_room.f(b.get('MaxSpeed')))]


def main():
    out = sys.argv[1]
    os.makedirs(os.path.join(out, 'fx'), exist_ok=True)
    gm = guidmap()
    profiles, prefabs, sounds = [], set(), set()
    base = os.path.join(EX, 'Data Assets', 'Profiles')
    for p in sorted(glob.glob(os.path.join(base, 'Hit Effects', '*.asset'))):
        v = load_asset(p)
        full = v.get('FullEffects') or {}
        fx = guids(full.get('SpawnEffectPrefabs'))
        snd = guids(full.get('DamageSounds'), 'Clip')
        profiles.append(['H', os.path.basename(p)[:-6], int(build_room.f(v.get('doHitFlash', 1))), fx, snd, blood(full.get('Blood'))])
    for p in sorted(glob.glob(os.path.join(base, 'Death Effects', '*.asset'))):
        v = load_asset(p)
        fx = guids(v.get('spawnEffectPrefabs'))
        snd = guids(v.get('deathSounds'), 'Clip')
        profiles.append(['D', os.path.basename(p)[:-6], 0, fx, snd, blood(v.get('blood'))])
    for pr in profiles:
        prefabs.update(pr[3])
        sounds.update(pr[4])

    # effect prefabs -> particle systems
    names = {}
    for g in sorted(prefabs):
        path = gm.get(g)
        if not path or not path.endswith('.prefab'):
            continue
        name = particles.safe(os.path.basename(path)[:-7])
        try:
            sc = build_room.Scene(path)
            parts = particles.gather_particles(sc, build_room.material_blend)
        except Exception as ex:   # noqa: BLE001
            print(f'{name}: {ex}')
            continue
        if not parts:
            continue
        for e in parts:           # a one-shot effect: plays when spawned, never loops
            e['flags'] = (e['flags'] | particles.PLAY_ON_AWAKE) & ~(particles.LOOP | particles.INACTIVE | particles.PREWARM)
        particles.write(os.path.join(out, 'fx', name + '.spt'), parts)
        json.dump(parts, open(os.path.join(WORK, 'rooms', 'fx__' + name + '.parts.json'), 'w'), indent=1)
        names[g] = name

    # sounds the sfx folder doesn't have yet
    idx_path = os.path.join(out, 'sfx', 'index.txt')
    have = {l.split('\t')[0] for l in open(idx_path, encoding='utf-8')} if os.path.exists(idx_path) else set()
    added = 0
    with open(idx_path, 'a', encoding='utf-8') as idx:
        for g in sorted(sounds - have):
            src = gm.get(g)
            if not src or not os.path.exists(src):
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

    with open(os.path.join(out, 'fx', 'profiles.txt'), 'w', encoding='utf-8') as fh:
        for kind, name, flash, fx, snd, bl in profiles:
            fh.write('\t'.join([kind, name, str(flash), ','.join(names[g] for g in fx if g in names), ','.join(snd),
                                ','.join(str(x) for x in bl)]) + '\n')
    print(f'{len(profiles)} profiles, {len(names)} effect prefabs, {added} sounds added')


if __name__ == '__main__':
    main()
