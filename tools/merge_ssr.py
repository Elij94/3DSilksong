#!/usr/bin/env python3
"""Takes the collision grid, gates and triggers of rooms rebuilt without their art (build_room.py --no-art, into
another folder) and puts them in the RomFS rooms, keeping those rooms' scenery layers (the .ssl tiles don't move).
Copies the new .sso and .spt too. For converter changes that touch collisions or objects only: no art rebake.

  python tools/merge_ssr.py <new_romfs> <romfs> [Scene ...]
"""
import os, struct, sys, shutil

HDR = '<HHffHHffHHHH'


def parts(d):
    ver, cpu, gx, gy, gw, gh, sx, sy, ng, nt, nl, _ = struct.unpack_from(HDR, d, 4)
    o = 36 + ((gw + 7) // 8) * gh + 92 * ng + 68 * nt
    return o, nl


def merge(new_dir, old_dir, scene):
    n = open(os.path.join(new_dir, 'rooms', scene + '.ssr'), 'rb').read()
    old_p = os.path.join(old_dir, 'rooms', scene + '.ssr')
    o = open(old_p, 'rb').read()
    no, _ = parts(n)
    oo, onl = parts(o)
    head = bytearray(n[:no])
    struct.pack_into('<H', head, 4 + 2 + 2 + 4 + 4 + 2 + 2 + 4 + 4 + 2 + 2, onl)   # the old room's layer count
    tmp = old_p + '.tmp'
    open(tmp, 'wb').write(bytes(head) + o[oo:])
    os.replace(tmp, old_p)
    for ext in ('.sso', '.spt'):
        src = os.path.join(new_dir, 'rooms', scene + ext)
        if os.path.exists(src):
            shutil.copyfile(src, os.path.join(old_dir, 'rooms', scene + ext))


def main():
    new_dir, old_dir = sys.argv[1], sys.argv[2]
    scenes = sys.argv[3:] or sorted(f[:-4] for f in os.listdir(os.path.join(new_dir, 'rooms')) if f.endswith('.ssr'))
    done = 0
    for sc in scenes:
        if os.path.exists(os.path.join(old_dir, 'rooms', sc + '.ssr')):
            merge(new_dir, old_dir, sc)
            done += 1
    print(f'{done} rooms merged')


if __name__ == '__main__':
    main()
