#!/usr/bin/env python3
"""Every sound the scripts can play that sfx/ doesn't have yet: the clips of the RandomAudioClipTable assets they use
(PlayRandomAudioClipTable, AudioPlayRandomVoiceFromTable...) and any sound reference left. Converted as the others
(16-bit mono PCM, 22050 Hz) and added to sfx/index.txt.

  python tools/build_sfx_missing.py <romfs dir> [--jobs 8]"""
import os, sys, json, glob, re, subprocess, argparse
from concurrent.futures import ThreadPoolExecutor
from sscommon import guidmap
import build_fsm

RATE = 22050


def refs(o, out):
    if isinstance(o, dict):
        if o.get('go') == 8300000 and o.get('guid'):
            out.add(o['guid'])
        for k, v in o.items():
            if k in ('Table', 'audioClipTable') and isinstance(v, dict):
                for c in build_fsm.table_clips(v):
                    out.add(c['guid'])
            else:
                refs(v, out)
    elif isinstance(o, list):
        for v in o:
            refs(v, out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--jobs', type=int, default=8)
    a = ap.parse_args()
    gm = guidmap()
    want = set()
    for p in glob.glob(os.path.join(a.out, 'fsm', '*.json')):
        refs(json.load(open(p, encoding='utf-8')), want)
    idx_path = os.path.join(a.out, 'sfx', 'index.txt')
    have = {l.split('\t')[0] for l in open(idx_path, encoding='utf-8')} if os.path.exists(idx_path) else set()
    todo = []
    for g in sorted(want - have):
        src = gm.get(g)
        if src and src.lower().endswith(('.ogg', '.wav')) and os.path.exists(src):
            todo.append((g, src))

    def conv(job):
        g, src = job
        stem = os.path.splitext(os.path.basename(src))[0]
        dst = os.path.join(a.out, 'sfx', stem + '.pcm')
        if not os.path.exists(dst):
            r = subprocess.run(['ffmpeg', '-v', 'fatal', '-y', '-i', src, '-ac', '1', '-ar', str(RATE), '-f', 's16le',
                                '-acodec', 'pcm_s16le', dst])
            if r.returncode:
                return None
        return f'{g}\t{stem}.ogg\n'

    with ThreadPoolExecutor(max_workers=a.jobs) as ex:
        lines = [l for l in ex.map(conv, todo) if l]
    with open(idx_path, 'a', encoding='utf-8') as fh:
        fh.writelines(lines)
    print(f'{len(want)} sounds referenced, {len(lines)} added')


if __name__ == '__main__':
    main()
