#!/usr/bin/env python3
"""Which music each room plays: <out>/music/rooms.txt, one line per scene, `Scene<TAB>track<TAB>ambience` (the names
of music/<track>.ogg, empty when none was found).

The music of a scene is its scene manager's musicCue; when it has none, the music cue its FSMs (ApplyMusicCue) and
music regions (enterMusicCue) name most often, battle tracks aside (an arena's FSM starts those). The ambience is the scene manager's atmosCue.

  python tools/music_map.py romfs
"""
import collections
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
from sscommon import EX, guidmap  # noqa: E402

GUID = re.compile(r'guid: ([0-9a-f]{32})')


def safe(name):
    return re.sub(r'[^a-z0-9_]+', '_', name.lower()).strip('_')


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else 'romfs'
    gm = guidmap()
    kind = {}                       # guid -> ('music' | 'atmos', track name)
    for g, p in gm.items():
        if p.endswith('.asset') and ('MusicCues' in p or 'AtmosCues' in p):
            kind[g] = ('atmos' if 'AtmosCues' in p else 'music', safe(os.path.basename(p)[:-6]))
    have = {f[:-4] for f in os.listdir(os.path.join(out, 'music')) if f.endswith('.ogg')}
    rooms = sorted(f[:-4] for f in os.listdir(os.path.join(out, 'rooms')) if f.endswith('.ssr'))
    lines, found = [], 0
    for scene in rooms:
        hits = glob.glob(f'{EX}/Scenes/**/{scene}.unity', recursive=True)
        if not hits:
            lines.append(f'{scene}\t\t')
            continue
        text = open(hits[0], errors='replace').read()
        music = atmos = ''
        m = re.search(r'\n  musicCue: \{[^}]*guid: ([0-9a-f]{32})', text)
        if m and m.group(1) in kind:
            music = kind[m.group(1)][1]
        m = re.search(r'\n  atmosCue: \{[^}]*guid: ([0-9a-f]{32})', text)
        if m and m.group(1) in kind:
            atmos = kind[m.group(1)][1]
        if not music:
            c = collections.Counter(kind[g][1] for g in GUID.findall(text) if g in kind and kind[g][0] == 'music'
                                    and not re.search(r'battle|boss|rip|rage|strive', kind[g][1]))
            music = c.most_common(1)[0][0] if c else ''
        music = music if music in have else ''
        atmos = atmos if atmos in have else ''
        found += bool(music)
        lines.append(f'{scene}\t{music}\t{atmos}')
    open(os.path.join(out, 'music', 'rooms.txt'), 'w').write('\n'.join(lines) + '\n')
    print(f'{found} of {len(rooms)} rooms with a music track -> {out}/music/rooms.txt')


if __name__ == '__main__':
    main()
