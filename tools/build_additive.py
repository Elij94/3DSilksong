#!/usr/bin/env python3
"""The scenes the game loads on top of a room (SceneAdditiveLoadConditional: boss arenas, cutscenes, the Bellway
station) -> romfs/text/additive.txt, one line each:

  <room> <scene> [<test> ...]      test: pd:<bool name>=<0|1>  or  pd:<int name><op><value> (op = < > ! =)

The tests are the component's first test group (all must pass); the engine loads <scene> with the room when they do.
  python tools/build_additive.py romfs
"""
import os, sys, glob
from sscommon import yaml_file

EX = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'ssdata', 'ex', 'Scenes')
NUM_OPS = {0: '=', 1: '!', 2: '<', 3: '>', 4: '<', 5: '>'}   # Equal, NotEqual, LessThan, MoreThan, (or equal)


def tests_of(v):
    out = []
    groups = ((v.get('tests') or {}).get('TestGroups') or [])
    for t in ((groups[0] or {}).get('Tests') or []) if groups else []:
        name = t.get('FieldName')
        if not name:
            continue
        if int(t.get('Type') or 0) == 0:
            out.append(f"pd:{name}={int(t.get('BoolValue') or 0)}")
        elif int(t.get('Type') or 0) == 1:
            out.append(f"pd:{name}{NUM_OPS.get(int(t.get('NumType') or 0), '=')}{int(t.get('IntValue') or 0)}")
    return out


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else 'romfs'
    lines = []
    for path in sorted(glob.glob(os.path.join(EX, '*', '*.unity'))):
        room = os.path.basename(path)[:-6]
        if not os.path.exists(os.path.join(out, 'rooms', room + '.ssr')):
            continue
        txt = open(path, encoding='utf-8', errors='replace').read()
        if 'sceneNameToLoad' not in txt:
            continue
        for t, v in yaml_file(path).values():
            if t == 'MonoBehaviour' and v.get('sceneNameToLoad'):
                scene = v['sceneNameToLoad']
                if not os.path.exists(os.path.join(out, 'rooms', scene + '.ssf')) and \
                        not os.path.exists(os.path.join(out, 'rooms', scene + '.sso')):
                    print(f'{room}: {scene} not converted')
                    continue
                lines.append(' '.join([room, scene] + tests_of(v)))
    os.makedirs(os.path.join(out, 'text'), exist_ok=True)
    open(os.path.join(out, 'text', 'additive.txt'), 'w', encoding='utf-8', newline='\n').write('\n'.join(lines) + '\n')
    print(f'{len(lines)} additive scenes')
    for l in lines:
        print(' ', l)


if __name__ == '__main__':
    main()
