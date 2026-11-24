#!/usr/bin/env python3
"""Music and ambience cues -> <out>/music/<cue>.ogg (Ogg Vorbis, 32 kHz stereo, for the 3DS's ndsp + a Vorbis
decoder), and optionally sound clips -> <out>/sfx/<clip>.ogg (mono).
A MusicCue / AtmosCue asset (Audio/MusicCues, Audio/AtmosCues) has several channels (layers the game fades in and
out); the cue is mixed down to the channels given (default: channel 0, the main layer).

  python tools/build_music.py <out_dir> [cue ...]               the cues (default: every cue in the export)
  python tools/build_music.py <out_dir> --channels 0,2 MossCave mix channels 0 and 2
  python tools/build_music.py <out_dir> --sfx LIST.txt          also the sound clips named in LIST.txt (one per line)
  python tools/build_music.py --list                            the cues, with their channels
Needs ffmpeg on PATH (or FFMPEG=...)."""
import os, re, sys, glob, argparse, subprocess
from sscommon import EX, guidmap, mono_fields

FFMPEG = os.environ.get('FFMPEG', 'ffmpeg')


def cues():
    out = {}
    for kind in ('MusicCues', 'AtmosCues'):
        for p in glob.glob(os.path.join(EX, '**', kind, '*.asset'), recursive=True):
            v = mono_fields(p)
            if 'channelInfos' not in v:
                continue
            clips = []
            for ch in v.get('channelInfos') or []:
                g = ((ch or {}).get('clip') or {}).get('guid')
                path = guidmap().get(g) if g else None
                clips.append(path if path and os.path.exists(path) else None)
            out[os.path.basename(p)[:-6]] = (kind[:-4].lower(), clips)
    return out


def safe(name):
    return re.sub(r'[^a-z0-9_]+', '_', name.lower()).strip('_')


def encode(inputs, dst, mono=False):
    cmd = [FFMPEG, '-v', 'error', '-y']
    for i in inputs:
        cmd += ['-i', i]
    if len(inputs) > 1:
        cmd += ['-filter_complex', f'amix=inputs={len(inputs)}:duration=longest:normalize=0']
    cmd += ['-ac', '1' if mono else '2', '-ar', '32000', '-c:a', 'libvorbis', '-q:a', '3', dst]
    return subprocess.run(cmd).returncode == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('out', nargs='?')
    ap.add_argument('cues', nargs='*')
    ap.add_argument('--channels', default='0')
    ap.add_argument('--sfx')
    ap.add_argument('--list', action='store_true')
    a = ap.parse_args()
    all_cues = cues()
    if a.list:
        for n, (kind, clips) in sorted(all_cues.items()):
            print(f'{kind:6s} {n:40s} ' + ' | '.join(os.path.basename(c) if c else '-' for c in clips))
        return
    if not a.out:
        ap.error('give an output folder (or --list)')
    want = [int(c) for c in a.channels.split(',')]
    os.makedirs(os.path.join(a.out, 'music'), exist_ok=True)
    done = empty = failed = 0
    for n in (a.cues or sorted(all_cues)):
        if n not in all_cues:
            print(f'no cue named {n}')
            failed += 1
            continue
        kind, clips = all_cues[n]
        inputs = [clips[i] for i in want if i < len(clips) and clips[i]] or [c for c in clips if c][:1]
        if not inputs:
            empty += 1
            continue
        if encode(inputs, os.path.join(a.out, 'music', safe(n) + '.ogg')):
            done += 1
        else:
            failed += 1
    print(f'music: {done} cues converted, {empty} without audio in the export, {failed} failed')
    if a.sfx:
        names = [l.strip() for l in open(a.sfx) if l.strip()]
        byname = {os.path.basename(p).rsplit('.', 1)[0]: p for p in guidmap().values()
                  if p.endswith(('.ogg', '.wav', '.mp3'))}
        os.makedirs(os.path.join(a.out, 'sfx'), exist_ok=True)
        ok = sum(encode([byname[n]], os.path.join(a.out, 'sfx', safe(n) + '.ogg'), mono=True) for n in names if n in byname)
        print(f'sfx: {ok} of {len(names)} clips converted')
    if failed:
        sys.exit(1)


if __name__ == '__main__':
    main()
