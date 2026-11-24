#!/usr/bin/env python3
"""Indexes of the AssetRipper export that the converters read (run once after tools/export_game.sh).
Adapted from HK3DS's index_export.py; Silksong's export nests assets in subfolders, so every search is recursive.
  metamaps.pkl        (sprite guid -> sprite .asset, texture guid -> texture .png), paths relative to ex/
  tex2sprites.pkl     texture guid -> [(sprite name, [x, y, w, h] rect in the texture, sprite guid), ...]
  meta_AudioClip.pkl  audio clip guid -> the clip's file
  scenes.pkl          scene name -> its .unity file
And a summary of what the export holds (counts per asset folder, PlayMaker FSMs, tk2d collections) in
work/export_summary.json.
   python tools/index_export.py"""
import os, re, sys, glob, json, pickle, collections
from sscommon import ROOT, EX, WORK


def guid_of(meta):
    m = re.search(r'^guid: ([0-9a-f]+)', open(meta, errors='replace').read(4000), re.M)
    return m.group(1) if m else None


def rglob(pattern):
    return glob.glob(f'{EX}/**/{pattern}', recursive=True)


def main():
    if not os.path.isdir(EX):
        raise SystemExit(f'no export in {EX} (run tools/export_game.sh first, or set SSDATA)')
    spr, tex, t2s, audio, scenes = {}, {}, {}, {}, {}
    for meta in rglob('*.png.meta'):
        g = guid_of(meta)
        if g: tex[g] = os.path.relpath(meta[:-5], EX)
    for meta in rglob('*.asset.meta'):
        path = meta[:-5]
        head = open(path, errors='replace').read(400)
        if '\nSprite:' not in head:
            continue
        g = guid_of(meta)
        if not g: continue
        spr[g] = os.path.relpath(path, EX)
        s = open(path, errors='replace').read()
        name = re.search(r'^\s*m_Name:[ \t]*(.*)$', s, re.M)
        rect = re.search(r'm_Rect:\s*\n\s*serializedVersion: \d+\s*\n\s*x: ([-0-9.e]+)\s*\n\s*y: ([-0-9.e]+)\s*\n\s*width: ([-0-9.e]+)\s*\n\s*height: ([-0-9.e]+)', s)
        tg = re.search(r'^\s*texture: \{fileID: \d+, guid: ([0-9a-f]+)', s, re.M)
        if name and tg:
            t2s.setdefault(tg.group(1), []).append((name.group(1).strip() or '?', [float(v) for v in rect.groups()] if rect else None, g))
    for ext in ('wav', 'ogg', 'mp3'):
        for meta in rglob(f'*.{ext}.meta'):
            g = guid_of(meta)
            if g: audio[g] = os.path.abspath(meta[:-5])
    for p in rglob('*.unity'):
        scenes[os.path.basename(p)[:-6]] = os.path.relpath(p, EX)
    pickle.dump(audio, open(f'{ROOT}/meta_AudioClip.pkl', 'wb'))
    pickle.dump((spr, tex), open(f'{ROOT}/metamaps.pkl', 'wb'))
    pickle.dump(t2s, open(f'{ROOT}/tex2sprites.pkl', 'wb'))
    pickle.dump(scenes, open(f'{ROOT}/scenes.pkl', 'wb'))

    folders = collections.Counter()
    for dp, _, fs in os.walk(EX):
        folders[os.path.relpath(dp, EX).replace('\\', '/').split('/')[0]] += sum(1 for f in fs if not f.endswith('.meta'))
    summary = {'sprites': len(spr), 'textures': len(tex), 'textures_with_sprites': len(t2s), 'audio': len(audio),
               'scenes': len(scenes), 'folders': dict(folders.most_common())}
    os.makedirs(WORK, exist_ok=True)
    json.dump(summary, open(f'{WORK}/export_summary.json', 'w'), indent=1)
    print(len(spr), 'sprites,', len(tex), 'textures,', len(t2s), 'textures with sprites,', len(audio), 'audio clips,',
          len(scenes), 'scenes')


if __name__ == '__main__':
    main()
