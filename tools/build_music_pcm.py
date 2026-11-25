#!/usr/bin/env python3
"""Music and ambience as raw PCM for the 3DS: <out>/music/<track>.pcm, 16-bit signed little-endian, mono, 22050 Hz.

The DSP plays PCM as it is, so the engine needs no Ogg Vorbis decoder: it streams these files from the RomFS. About
2.6 MB a minute. Needs ffmpeg on PATH.

  python tools/build_music_pcm.py romfs            every music/*.ogg that has no .pcm yet
  python tools/build_music_pcm.py romfs sfx        the same for the sound effects (sfx/*.ogg)
"""
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

RATE = 22050


def convert(src, dst):
    tmp = dst + '.tmp'
    r = subprocess.run(['ffmpeg', '-v', 'fatal', '-y', '-i', src, '-ac', '1', '-ar', str(RATE), '-f', 's16le',
                        '-acodec', 'pcm_s16le', tmp])
    if r.returncode == 0:
        os.replace(tmp, dst)
        return True
    return False


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else 'romfs'
    d = os.path.join(out, sys.argv[2] if len(sys.argv) > 2 else 'music')
    jobs = [(os.path.join(d, f), os.path.join(d, f[:-4] + '.pcm')) for f in sorted(os.listdir(d)) if f.endswith('.ogg')]
    jobs = [j for j in jobs if not os.path.exists(j[1])]
    with ThreadPoolExecutor(6) as ex:
        ok = sum(ex.map(lambda j: convert(*j), jobs))
    total = sum(os.path.getsize(os.path.join(d, f)) for f in os.listdir(d) if f.endswith('.pcm'))
    print(f'{ok} of {len(jobs)} converted; {os.path.basename(d)}/*.pcm: {total / 2**20:.0f} MB')


if __name__ == '__main__':
    main()
